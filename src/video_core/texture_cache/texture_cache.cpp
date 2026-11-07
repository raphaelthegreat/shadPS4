// SPDX-FileCopyrightText: Copyright 2024-2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <limits>
#include <xxhash.h>

#include "common/assert.h"
#include "common/debug.h"
#include "common/div_ceil.h"
#include "common/hash.h"
#include "core/emulator_settings.h"
#include "core/memory.h"
#include "video_core/amdgpu/liverpool.h"
#include "video_core/buffer_cache/buffer_cache.h"
#include "video_core/gpu_memory.h"
#include "video_core/renderer_vulkan/liverpool_to_vk.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_runtime.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/texture_cache/host_compatibility.h"
#include "video_core/texture_cache/texture_cache.h"
#include "video_core/texture_cache/tile_manager.h"

namespace VideoCore {

static constexpr u32 MAX_IMAGES = std::numeric_limits<u16>::max();
static constexpr u32 MAX_IMAGE_VIEWS = std::numeric_limits<u16>::max();
static constexpr u32 MAX_SAMPLERS = std::numeric_limits<u16>::max();

static u64 HashBackingMemory(VAddr virtual_addr, u64 size) {
    static constexpr std::array<u8, Core::GUEST_PAGE_SIZE> zeros{};
    auto* state = XXH3_createState();
    const auto& backing_pages = Core::Memory::Instance()->GetAddressSpace().BackingPages();
    XXH3_64bits_reset(state);
    while (size) {
        const u64 page = virtual_addr >> Core::GUEST_PAGE_BITS;
        const u64 offset_in_page = virtual_addr & (Core::GUEST_PAGE_SIZE - 1);
        const u64 hash_size = std::min<u64>(Core::GUEST_PAGE_SIZE - offset_in_page, size);
        if (auto* entry = backing_pages.find(page); entry && *entry) {
            XXH3_64bits_update(state, *entry + offset_in_page, hash_size);
        } else {
            XXH3_64bits_update(state, zeros.data(), hash_size);
        }
        size -= hash_size;
        virtual_addr += hash_size;
    }
    const u64 hash = XXH3_64bits_digest(state);
    XXH3_freeState(state);
    return hash;
}

TextureCache::TextureCache(const Vulkan::Instance& instance_, Vulkan::Scheduler& scheduler_,
                           Vulkan::Runtime& runtime_, AmdGpu::Liverpool* liverpool_,
                           BufferCache& buffer_cache_, GpuMemory& gpu_memory_)
    : instance{instance_}, scheduler{scheduler_}, runtime{runtime_}, liverpool{liverpool_},
      buffer_cache{buffer_cache_}, gpu_memory{gpu_memory_}, slot_images{MAX_IMAGES},
      slot_image_views{MAX_IMAGE_VIEWS}, slot_samplers{MAX_SAMPLERS},
      blit_helper{instance, scheduler},
      tile_manager{instance, scheduler, runtime, buffer_cache.GetStreamBuffer()},
      readback_linear_images{EmulatorSettings.IsReadbackLinearImagesEnabled()} {

    u32 max_samplers = instance.GetMaxSamplerAllocationCount();
    trigger_gc_samplers = max_samplers * 3 / 4;
    pressure_gc_samplers = max_samplers * 7 / 8;
    critical_gc_samplers = max_samplers * 15 / 16;

    // Set up garbage collection parameters.
    if (!instance.CanReportMemoryUsage()) {
        trigger_gc_memory = 0;
        pressure_gc_memory = DEFAULT_PRESSURE_GC_MEMORY;
        critical_gc_memory = DEFAULT_CRITICAL_GC_MEMORY;
        return;
    }

    const s64 device_local_memory = static_cast<s64>(instance.GetTotalMemoryBudget());
    const s64 min_spacing_expected = device_local_memory - 1_GB;
    const s64 min_spacing_critical = device_local_memory - 512_MB;
    const s64 mem_threshold = std::min<s64>(device_local_memory, TARGET_GC_THRESHOLD);
    const s64 min_vacancy_expected = (6 * mem_threshold) / 10;
    const s64 min_vacancy_critical = (2 * mem_threshold) / 10;
    const s64 min_pressure_floor =
        std::clamp<s64>(device_local_memory / 4, 256_MB, DEFAULT_PRESSURE_GC_MEMORY);
    const s64 min_critical_floor =
        std::clamp<s64>(device_local_memory / 2, 512_MB, DEFAULT_CRITICAL_GC_MEMORY);
    pressure_gc_memory = static_cast<u64>(
        std::max<s64>(std::min(device_local_memory - min_vacancy_expected, min_spacing_expected),
                      min_pressure_floor));
    critical_gc_memory = static_cast<u64>(
        std::max<s64>(std::min(device_local_memory - min_vacancy_critical, min_spacing_critical),
                      min_critical_floor));
    trigger_gc_memory = static_cast<u64>((device_local_memory - mem_threshold) / 2);
}

TextureCache::~TextureCache() = default;

void TextureCache::ProcessDownloadImages() {
    std::unique_lock lk{download_images_mutex};
    if (download_images.empty()) {
        return;
    }
    for (const ImageId image_id : download_images) {
        DownloadImageMemory(image_id);
    }
    download_images.clear();
    scheduler.Finish();
    scheduler.PopPendingOperations();
}

void TextureCache::DownloadImageMemory(ImageId image_id, DownloadMemoryFlags flags) {
    Image& image = slot_images[image_id];
    if (False(image.flags & ImageFlagBits::GpuModified)) {
        return;
    }
    const auto image_addr = image.info.guest_address;
    const auto image_size = image.info.guest_size;
    const auto image_mips = image.info.resources.levels;
    const auto is_sync = True(flags & DownloadMemoryFlags::Sync);
    const auto is_priority = True(flags & DownloadMemoryFlags::Priority);
    const auto discard_buffer_cache = True(flags & DownloadMemoryFlags::DiscardBufferCache);
    ASSERT(!(is_sync && is_priority));

    u32 copy_size = 0;
    boost::container::small_vector<vk::BufferImageCopy, 8> buffer_copies;
    for (u32 mip = 0; mip < image_mips; ++mip) {
        const u32 width = std::max(image.info.size.width >> mip, 1u);
        const u32 height = std::max(image.info.size.height >> mip, 1u);
        const u32 depth =
            image.info.props.is_volume ? std::max(image.info.size.depth >> mip, 1u) : 1u;
        const auto [mip_size, mip_pitch, mip_height, mip_offset, mip_slice_size] =
            image.info.mips_layout[mip];
        const u32 extent_width = mip_pitch ? std::min<u32>(mip_pitch, width) : width;
        const u32 extent_height = mip_height ? std::min<u32>(mip_height, height) : height;
        buffer_copies.push_back(vk::BufferImageCopy{
            .bufferOffset = mip_offset,
            .bufferRowLength = mip_pitch,
            .bufferImageHeight = mip_height,
            .imageSubresource{
                .aspectMask = image.aspect_mask & ~vk::ImageAspectFlagBits::eStencil,
                .mipLevel = mip,
                .baseArrayLayer = 0,
                .layerCount = image.info.resources.layers,
            },
            .imageOffset = {0, 0, 0},
            .imageExtent = {extent_width, extent_height, depth},
        });
        copy_size += mip_size;
    }
    if (buffer_copies.empty()) {
        return;
    }
    ASSERT(copy_size <= image.info.guest_size);
    const auto download =
        runtime.GetStagingPool().Request(copy_size, MemoryType::HostCached, 16, !is_sync);

    tile_manager.TileImage(image, buffer_copies, download.buffer, download.offset);

    const auto write_data = [this, image_id, image_addr, copy_size, download, discard_buffer_cache,
                             is_sync] {
        download.Invalidate();
        Core::Memory::Instance()->TryWriteBacking(std::bit_cast<u8*>(image_addr), download.mapped,
                                                  copy_size);
        ReleaseOwnership(image_id);
        if (discard_buffer_cache) {
            buffer_cache.UnmarkPagesAsGpuModified(image_addr, copy_size, true);
        }
        if (!is_sync) {
            runtime.GetStagingPool().FreeDeferred(download);
        }
    };

    if (is_priority) {
        scheduler.DeferPriorityOperation(std::move(write_data));
    } else if (is_sync) {
        scheduler.Finish();
        write_data();
    } else {
        scheduler.DeferOperation(std::move(write_data));
    }
}

void TextureCache::ClaimOwnership(ImageId image_id, bool assume_locked) {
    Image& image = slot_images[image_id];
    // The flag is only raised once the claim is recorded, so finding it already raised means
    // there is nothing left to do and the image does not have to be locked to find that out.
    // A buffer that took over part of the range clears it again, since the claim is no longer
    // whole and has to be written back over the whole range.
    constexpr ImageFlagBits Claimed =
        ImageFlagBits(ImageFlagBits::Owned | ImageFlagBits::Displaced);
    if ((image.flags & Claimed) == ImageFlagBits::Owned) {
        return;
    }
    std::unique_lock lk{image.mutex, std::defer_lock};
    if (!assume_locked) {
        lk.lock();
    }
    if ((image.flags & Claimed) == ImageFlagBits::Owned ||
        False(image.flags & ImageFlagBits::Registered)) {
        return;
    }
    gpu_memory.AssignOwner(image.info.guest_address, image.info.guest_size,
                           Owner{OwnerKind::Image, image_id.index});
    image.flags |= ImageFlagBits::Owned;
    image.flags &= ~ImageFlagBits::Displaced;
    if (lk.owns_lock()) {
        lk.unlock();
    }
    // An owner has to be watched for writes as well, so this also starts tracking the image if
    // something dropped it in the meantime.
    TrackImage(image_id, true, assume_locked);
}

void TextureCache::ReleaseOwnership(ImageId image_id, bool assume_locked) {
    Image& image = slot_images[image_id];
    // Images that never claimed anything, which is most of them, are turned away without being
    // locked. The flag outlives the claim it stands for, so seeing it lowered is conclusive.
    if (False(image.flags & ImageFlagBits::Owned)) {
        return;
    }
    std::unique_lock lk{image.mutex, std::defer_lock};
    if (!assume_locked) {
        lk.lock();
    }
    if (False(image.flags & ImageFlagBits::Owned)) {
        return;
    }
    gpu_memory.ReleaseOwner(image.info.guest_address, image.info.guest_size,
                            Owner{OwnerKind::Image, image_id.index});
    image.flags &= ~ImageFlagBits::Owned;
    const bool tracked = True(image.flags & ImageFlagBits::Tracked);
    if (lk.owns_lock()) {
        lk.unlock();
    }
    if (tracked) {
        TrackImage(image_id, false, assume_locked);
    }
}

void TextureCache::MarkAsMaybeDirty(ImageId image_id, Image& image) {
    if (image.cpu_hash == 0) {
        // Initialize hash
        const u8* addr = std::bit_cast<u8*>(image.info.guest_address);
        image.cpu_hash = XXH3_64bits(addr, image.info.guest_size);
    }
    image.MarkDirty(ImageFlagBits::MaybeCpuDirty);
    UntrackImage(image_id);
}

void TextureCache::InvalidateMemory(VAddr addr, size_t size) {
    const auto pages_start = GetPageAddr(addr);
    const auto pages_end = GetNextPageAddr(addr + size - 1);

    SmallVector<ImageId, 8> image_ids;
    ForEachImageInRegion(pages_start, pages_end - pages_start,
                         [&](ImageId image_id, Image&) { image_ids.push_back(image_id); });

    for (const auto image_id : image_ids) {
        Image& image = slot_images[image_id];
        std::scoped_lock lk{image.mutex};

        const auto image_begin = image.info.guest_address;
        const auto image_end = image.info.guest_address + image.info.guest_size;
        if (image.Overlaps(addr, size)) {
            // Modified region overlaps image, so the image was definitely accessed by this fault.
            // Untrack the image, so that the range is unprotected and the guest can write freely.
            image.MarkDirty(ImageFlagBits::CpuDirty, addr, addr + size);
            ReleaseOwnership(image_id, true);
            UntrackImage(image_id);
        } else if (pages_end < image_end) {
            // This page access may or may not modify the image.
            // We should not mark it as dirty now. If it really was modified
            // it will receive more invalidations on its other pages.
            // Remove tracking from this page only.
            UntrackImageHead(image_id);
        } else if (image_begin < pages_start) {
            // This page access does not modify the image but the page should be untracked.
            // We should not mark this image as dirty now. If it really was modified
            // it will receive more invalidations on its other pages.
            UntrackImageTail(image_id);
        } else {
            // Image begins and ends on this page so it can not receive any more invalidations.
            // We will check it's hash later to see if it really was modified.
            MarkAsMaybeDirty(image_id, image);
        }
    }
}

void TextureCache::ReadMemory(VAddr addr, size_t size, bool assume_locks) {
    SmallVector<ImageId, 8> image_ids;
    ForEachImageInRegion(addr, size, [&](ImageId image_id, Image& image) {
        if (True(image.flags & ImageFlagBits::Owned)) {
            image_ids.push_back(image_id);
        }
    });
    if (image_ids.empty()) {
        return;
    }
    const auto flush_images = [this, image_ids = std::move(image_ids)] {
        for (const ImageId image_id : image_ids) {
            if (slot_images[image_id].CanBeDownloaded()) {
                DownloadImageMemory(image_id, DownloadMemoryFlags::Sync);
            }
            // Dropped even when the image could not be written back, as leaving the claim in
            // place would fault the guest on every retry of the same read.
            ReleaseOwnership(image_id);
        }
    };
    if (assume_locks) {
        flush_images();
    } else {
        liverpool->SendCommand<true>(std::move(flush_images));
    }
}

void TextureCache::InvalidateMemoryFromGPU(VAddr address, size_t max_size) {
    // Mark the image at the written address, then follow images stored right after it
    // (e.g. the chroma planes after a luma plane).
    const VAddr end = address + max_size;
    VAddr next = address;
    while (next < end) {
        VAddr next_end = 0;
        ForEachEntryWithAddress(next, [&](const BucketEntry& entry) {
            Image& image = slot_images[entry.id];
            // Images are only aligned to 256 bytes, so sharing a key is not sharing an address.
            if (image.info.guest_address != next) {
                return;
            }
            // Ensure image is reuploaded when accessed again.
            image.MarkDirty(ImageFlagBits::GpuDirty);
            ReleaseOwnership(entry.id);
            next_end = std::max<VAddr>(next_end, next + image.info.guest_size);
        });
        next = next_end > next ? next_end : end;
    }
}

void TextureCache::MarkAsMaybeGpuDirty(VAddr addr, size_t size) {
    ForEachImageInRegion(addr, size, [&](ImageId image_id, Image& image) {
        if (image.gpu_hash == 0) {
            // Read backing pages directly, without faulting through the guest readback path.
            image.gpu_hash = HashBackingMemory(image.info.guest_address, image.info.guest_size);
        }
        // The buffer is about to claim this range, which leaves whatever the image still owns
        // around it intact but means the claim no longer covers the whole image.
        image.flags |= ImageFlagBits::Displaced;
    });
}

void TextureCache::UnmapMemory(VAddr cpu_addr, size_t size) {
    SmallVector<ImageId, 16> deleted_images;
    ForEachImageInRegion(cpu_addr, size, [&](ImageId id, Image&) { deleted_images.push_back(id); });
    for (const ImageId id : deleted_images) {
        // TODO: Download image data back to host.
        FreeImage(id);
    }
}

ImageId TextureCache::FindImage(ImageDesc& desc, bool exact_fmt) {
    const auto& info = desc.info;

    ASSERT(info.guest_address != 0);

    // An image laid out exactly as requested needs nothing asked of it, and is found by scanning
    // the one bucket it has to be in without reading any image registered alongside it.
    ImageId image_id{};
    ForEachEntryWithAddress(info.guest_address, [&, key = info.Key()](const BucketEntry& entry) {
        if (entry.key == key) {
            image_id = entry.id;
        }
    });

    // Otherwise look for one the request is a plain view of, consulting only the properties a
    // view cannot paper over. Nothing else in the cache can serve it, so anything short of that
    // means a new image, which the contents of the old ones are then copied into according to
    // who owns the memory they share.
    SubresourceBase view_base;
    if (!image_id) {
        SmallVector<ImageId, 8> image_ids;
        ForEachImageInRegion(info.guest_address, info.guest_size,
                             [&](ImageId id, Image&) { image_ids.push_back(id); });
        for (const ImageId cache_id : image_ids) {
            const auto& cache_info = slot_images[cache_id].info;
            // Guest size is not part of the identity of an image. It follows the height the
            // guest declared for the allocation, which is padded inconsistently and sometimes
            // outright wrong, while the layout the texels actually sit in is what decides
            // reusability.
            if (!IsLayoutCompatible(cache_info, info) || cache_info.pitch != info.pitch) {
                continue;
            }
            // A volume and an array over the same memory are laid out alike but cannot be views
            // of one another, so the requested interpretation has to be materialised separately.
            if (cache_info.type != info.type && info.size != Extent3D{1, 1, 1}) {
                continue;
            }
            // Depth and colour are distinct kinds of image to the host even when they alias the
            // same texels, which the format classes already say, so an aliasing pair ends up as
            // two images whose contents the owner driven validation carries between them.
            const bool format_matches =
                exact_fmt ? cache_info.pixel_format == info.pixel_format
                          : IsVulkanFormatCompatible(cache_info.pixel_format, info.pixel_format);
            if (!format_matches) {
                continue;
            }
            if (cache_info.guest_address == info.guest_address) {
                // The same allocation seen the same way. Extra mips and layers are fine because
                // the view picks out the ones asked for, but the extent has to match, as
                // sampling a wider or taller image normalises the coordinates against the wrong
                // size. The guest size is deliberately not consulted; guests pad the height of
                // an allocation inconsistently between the views they take of it.
                if (cache_info.size == info.size && cache_info.resources.Contains(info.resources)) {
                    image_id = cache_id;
                    break;
                }
                continue;
            }
            // Starting further into the allocation, usable when what was asked for is a whole
            // run of its subresources rather than a subrect of them.
            SubresourceRange range;
            if (IsViewOf(cache_info, info, range)) {
                image_id = cache_id;
                view_base = range.base;
                break;
            }
        }
    }

    if (!image_id) {
        const bool needs_storage = desc.type == BindingType::Storage;
        image_id = slot_images.Insert(instance, runtime, slot_image_views, info, needs_storage);
        RegisterImage(image_id);
    }

    Image& image = slot_images[image_id];
    image.tick_accessed_last = scheduler.CurrentTick();
    TouchImage(image);

    // If the image requested is a subresource of the image from cache record its location.
    if (view_base.level > 0) {
        desc.view_info.range.base.level = view_base.level;
    }
    if (view_base.layer > 0) {
        desc.view_info.range.base.layer = view_base.layer;
    }

    return image_id;
}

ImageId TextureCache::FindImageFromRange(VAddr address, size_t size, bool ensure_valid) {
    SmallVector<ImageId, 4> image_ids;
    ForEachEntryWithAddress(address, [&](const BucketEntry& entry) {
        if (ensure_valid && !slot_images[entry.id].HasGpuValidData()) {
            return;
        }
        image_ids.push_back(entry.id);
    });
    if (image_ids.size() == 1) {
        // Sometimes image size might not exactly match with requested buffer size
        // If we only found 1 candidate image use it without too many questions.
        Image& image = slot_images[image_ids[0]];
        TouchImage(image);
        return image_ids.back();
    }
    if (!image_ids.empty()) {
        for (s32 i = 0; i < image_ids.size(); ++i) {
            Image& image = slot_images[image_ids[i]];
            if (image.info.guest_size == size) {
                TouchImage(image);
                return image_ids[i];
            }
        }
        LOG_WARNING(Render_Vulkan,
                    "Failed to find exact image match for copy addr={:#x}, size={:#x}", address,
                    size);
    }
    return {};
}

void TextureCache::EnsureStorageUsage(ImageId image_id) {
    Image& image = slot_images[image_id];
    image.usage.storage = 1u;
    runtime.AddImageStorageUsage(&image);
}

ImageView& TextureCache::FindTexture(ImageId image_id, const ImageDesc& desc) {
    Image& image = slot_images[image_id];
    const bool is_storage = desc.type == BindingType::Storage;
    image.usage.texture |= !is_storage;
    if (is_storage) {
        image.flags |= ImageFlagBits::GpuModified;
        EnsureStorageUsage(image_id);
        if (readback_linear_images && (!image.info.props.is_tiled || image.info.size.width <= 8) &&
            image.info.guest_address != 0) {
            std::unique_lock lk{download_images_mutex};
            download_images.emplace(image_id);
        }
    }
    UpdateImage(image_id, is_storage);
    return image.FindView(desc.view_info);
}

void TextureCache::RegisterMeta(VAddr address, MetaDataInfo info) {
    const auto it = surface_metas.find(address);
    if (it == surface_metas.end()) {
        surface_metas.emplace(address, info);
        return;
    }

    auto& registered = it->second;
    if (registered.type != info.type || registered.owner_uid != info.owner_uid) {
        registered = info;
        return;
    }

    // Preserve the current clear state when the same render target is rebound.
    registered.owner_id = info.owner_id;
    registered.num_samples = info.num_samples;
}

void TextureCache::UnregisterMeta(VAddr address, u64 owner_uid) {
    const auto it = surface_metas.find(address);
    if (it != surface_metas.end() && it->second.owner_uid == owner_uid) {
        surface_metas.erase(address);
    }
}

ImageView& TextureCache::FindRenderTarget(ImageId image_id, const ImageDesc& desc) {
    Image& image = slot_images[image_id];
    image.flags |= ImageFlagBits::GpuModified;
    if (readback_linear_images && (!image.info.props.is_tiled || image.info.size.width <= 8)) {
        std::unique_lock lk{download_images_mutex};
        download_images.emplace(image_id);
    }
    image.usage.render_target = 1u;
    UpdateImage(image_id, true);

    // Register meta data for this color buffer
    const VAddr cmask_addr = desc.info.meta_info.cmask_addr;
    if (image.info.meta_info.cmask_addr != cmask_addr) {
        UnregisterMeta(image.info.meta_info.cmask_addr, image.image_uid);
        image.info.meta_info.cmask_addr = cmask_addr;
    }
    if (cmask_addr) {
        RegisterMeta(cmask_addr, MetaDataInfo{
                                     .type = MetaType::CMask,
                                     .owner_id = image_id,
                                     .owner_uid = image.image_uid,
                                     .num_samples = image.info.num_samples,
                                 });
    }

    const VAddr fmask_addr = desc.info.meta_info.fmask_addr;
    if (image.info.meta_info.fmask_addr != fmask_addr) {
        UnregisterMeta(image.info.meta_info.fmask_addr, image.image_uid);
        image.info.meta_info.fmask_addr = fmask_addr;
    }
    if (fmask_addr) {
        RegisterMeta(fmask_addr, MetaDataInfo{
                                     .type = MetaType::FMask,
                                     .owner_id = image_id,
                                     .owner_uid = image.image_uid,
                                     .num_samples = image.info.num_samples,
                                 });
    }

    return image.FindView(desc.view_info, false);
}

ImageView& TextureCache::FindDepthTarget(ImageId image_id, const ImageDesc& desc) {
    Image& image = slot_images[image_id];
    image.flags |= ImageFlagBits::GpuModified;
    image.usage.depth_target = 1u;
    UpdateImage(image_id, true);

    // Register meta data for this depth buffer
    const VAddr htile_addr = desc.info.meta_info.htile_addr;
    if (image.info.meta_info.htile_addr != htile_addr) {
        UnregisterMeta(image.info.meta_info.htile_addr, image.image_uid);
        image.info.meta_info.htile_addr = htile_addr;
    }
    if (htile_addr) {
        RegisterMeta(htile_addr, MetaDataInfo{
                                     .type = MetaType::HTile,
                                     .owner_id = image_id,
                                     .owner_uid = image.image_uid,
                                     .num_samples = image.info.num_samples,
                                     .clear_mask = image.info.meta_info.htile_clear_mask,
                                 });
    }

    AssociateStencil(image_id, desc.info);

    return image.FindView(desc.view_info, false);
}

void TextureCache::AssociateStencil(ImageId depth_id, const ImageInfo& depth_info) {
    if (depth_info.stencil_addr == 0 || depth_info.stencil_addr == depth_info.guest_address) {
        return;
    }
    ImageId stencil_id{};
    ForEachImageInRegion(
        depth_info.stencil_addr, depth_info.stencil_size, [&](ImageId image_id, Image& image) {
            if (image.info.guest_address != depth_info.stencil_addr) {
                return;
            }
            if (image.info.pixel_format == vk::Format::eUndefined ||
                Vulkan::LiverpoolToVK::IsFormatStencilCompatible(image.info.pixel_format)) {
                stencil_id = image_id;
            }
        });
    if (!stencil_id) {
        ImageInfo info{};
        info.guest_address = depth_info.stencil_addr;
        info.guest_size = depth_info.stencil_size;
        info.size = depth_info.size;
        stencil_id = slot_images.Insert(instance, runtime, slot_image_views, info);
        RegisterImage(stencil_id);
    }
    Image& stencil_image = slot_images[stencil_id];
    TouchImage(stencil_image);
    stencil_image.AssociateDepth(depth_id, slot_images[depth_id].image_uid);
}

void TextureCache::GatherOwners(ImageId image_id, OwnerRuns& runs) {
    Image& image = slot_images[image_id];
    const VAddr addr = image.info.guest_address;
    const u64 size = image.info.guest_size;
    if (size == 0 || !gpu_memory.IsRegionOwned(addr, size)) {
        return;
    }
    // The owner lock is held while walking, so nothing is touched until it is gone.
    boost::container::small_vector<std::pair<VAddr, VAddr>, 4> buffer_runs;
    gpu_memory.ForEachOwner(addr, size, [&](VAddr run_addr, u64 run_size, Owner owner) {
        if (owner.kind == OwnerKind::Image && ImageId{owner.index} != image_id) {
            runs.emplace_back(ImageId{owner.index}, run_addr, run_addr + run_size);
        } else if (owner.kind == OwnerKind::Buffer) {
            buffer_runs.emplace_back(run_addr, run_addr + run_size);
        }
    });
    for (const auto& [begin, end] : buffer_runs) {
        image.MarkDirty(ImageFlagBits::GpuDirty, begin, end);
    }
}

void TextureCache::ValidateFromOwners(ImageId image_id, const OwnerRuns& runs) {
    Image& image = slot_images[image_id];
    for (const auto& [src_id, begin, end] : runs) {
        Image& src_image = slot_images[src_id];
        if (False(src_image.flags & ImageFlagBits::Registered) || !src_image.HasGpuValidData()) {
            continue;
        }
        SubresourceCopies copies;
        CollectOverlapCopies(src_image.info, image.info, copies);
        ClipCopiesToRange(image.info, copies, begin, end);
        if (!copies.empty()) {
            runtime.CopySubresources(&src_image, &image, copies);
            image.info.meta_info.htile_clear_mask = 0;
        }
    }
}

void TextureCache::RefreshImage(Image& image) {
    if (False(image.flags & ImageFlagBits::Dirty) || image.info.num_samples > 1) {
        return;
    }

    RENDERER_TRACE;
    TRACE_HINT(fmt::format("{:x}:{:x}", image.info.guest_address, image.info.guest_size));

    if (True(image.flags & ImageFlagBits::MaybeCpuDirty) &&
        False(image.flags & ImageFlagBits::CpuDirty)) {
        // The image size should be less than page size to be considered MaybeCpuDirty
        // So this calculation should be very uncommon and reasonably fast
        // For now we'll just check up to 64 first pixels
        const auto addr = std::bit_cast<u8*>(image.info.guest_address);
        const u32 w = std::min(image.info.size.width, u32(8));
        const u32 h = std::min(image.info.size.height, u32(8));

        const u32 s_w = image.info.props.is_block ? Common::DivCeil(w, 4u) : w;
        const u32 s_h = image.info.props.is_block ? Common::DivCeil(h, 4u) : h;
        const u32 size = s_w * s_h * (image.info.num_bits / 8);
        const u64 hash = XXH3_64bits(addr, size);
        if (image.cpu_hash == hash) {
            image.flags &= ~ImageFlagBits::MaybeCpuDirty;
            return;
        }
        image.cpu_hash = hash;
    }

    image.gpu_hash = 0;

    const u32 num_layers = image.info.resources.layers;
    const u32 num_mips = image.info.resources.levels;

    SmallVector<vk::BufferImageCopy, 14> image_copies;
    for (u32 m = 0; m < num_mips; m++) {
        const u32 width = std::max(image.info.size.width >> m, 1u);
        const u32 height = std::max(image.info.size.height >> m, 1u);
        const u32 depth =
            image.info.props.is_volume ? std::max(image.info.size.depth >> m, 1u) : 1u;
        const auto [mip_size, mip_pitch, mip_height, mip_offset, mip_slice_size] =
            image.info.mips_layout[m];
        // Only the mips something was actually written into have to come back from guest memory;
        // the rest keep whatever the gpu last rendered into them, as a mip chain built on the gpu
        // one level at a time does.
        const VAddr mip_addr = image.info.guest_address + mip_offset;
        if (!image.IsDirtyAt(mip_addr, mip_addr + mip_size)) {
            continue;
        }
        const u32 extent_width = mip_pitch ? std::min<u32>(mip_pitch, width) : width;
        const u32 extent_height = mip_height ? std::min<u32>(mip_height, height) : height;
        image_copies.push_back({
            .bufferOffset = mip_offset,
            .bufferRowLength = mip_pitch,
            .bufferImageHeight = mip_height,
            .imageSubresource{
                .aspectMask = image.aspect_mask & ~vk::ImageAspectFlagBits::eStencil,
                .mipLevel = m,
                .baseArrayLayer = 0,
                .layerCount = num_layers,
            },
            .imageOffset = {0, 0, 0},
            .imageExtent = {extent_width, extent_height, depth},
        });
    }

    if (image_copies.empty()) {
        image.ClearDirty();
        return;
    }

    scheduler.EndRendering();

    const auto [in_buffer, in_offset] =
        buffer_cache.ObtainBufferForImage(image.info.guest_address, image.info.guest_size);
    const auto [buffer, offset] = tile_manager.DetileImage(in_buffer, in_offset, image.info);
    for (auto& copy : image_copies) {
        copy.bufferOffset += offset;
    }

    runtime.UploadImage(&image, buffer, image_copies);
}

vk::Sampler TextureCache::GetSampler(const AmdGpu::Sampler& sharp,
                                     AmdGpu::BorderColorBuffer border_color_base,
                                     const bool is_depth) {
    // Compare and plain uses of one S# need separate samplers.
    const u64 hash = HashCombine(XXH3_64bits(&sharp, sizeof(sharp)), is_depth);
    const auto [it, new_sampler] = samplers.try_emplace(hash);
    if (new_sampler) {
        it->second = slot_samplers.Insert(instance, sharp, border_color_base, is_depth);
        Sampler& sampler = slot_samplers[it->second];
        sampler.hash = hash;
        sampler_lru_cache.Insert(sampler, gc_tick);
    }

    Sampler& sampler = slot_samplers[it->second];
    sampler_lru_cache.Touch(sampler, gc_tick);
    return sampler.Handle();
}

void TextureCache::RegisterImage(ImageId image_id) {
    Image& image = slot_images[image_id];
    ASSERT_MSG(False(image.flags & ImageFlagBits::Registered),
               "Trying to register an already registered image");
    image.flags |= ImageFlagBits::Registered;
    image.memory_usage_counter = &total_used_memory;
    total_used_memory += Common::AlignUp(image.SizeBytes(), 1024);
    image_lru_cache.Insert(image, gc_tick);
    const auto& info = image.info;
    ASSERT_MSG((info.guest_address & ((1 << IMAGE_ADDRESS_SHIFT) - 1)) == 0,
               "Trying to register an unaligned image");
    const BucketEntry entry{
        .key = info.Key(),
        .size = info.guest_size,
        .id = image_id,
    };
    ForEachPage(info.guest_address, info.guest_size,
                [this, entry](u64 page) { page_table[page].entries.emplace_back(entry); });
}

void TextureCache::UnregisterImage(ImageId image_id) {
    Image& image = slot_images[image_id];
    ASSERT_MSG(True(image.flags & ImageFlagBits::Registered),
               "Trying to unregister an already unregistered image");
    ReleaseOwnership(image_id);
    image.flags &= ~ImageFlagBits::Registered;
    image.memory_usage_counter = nullptr;
    image_lru_cache.Free(image);
    total_used_memory -= Common::AlignUp(image.SizeBytes(), 1024);
    ForEachPage(image.info.guest_address, image.info.guest_size, [this, image_id](u64 page) {
        const auto page_it = page_table.find(page);
        ASSERT_MSG(page_it, "Unregistering unregistered page={:#x}", page << Traits::PAGE_BITS);
        auto& entries = page_it->entries;
        const auto vector_it = std::ranges::find(entries, image_id, &BucketEntry::id);
        ASSERT_MSG(vector_it != entries.end(), "Unregistering unregistered image in page={:#x}",
                   page << Traits::PAGE_BITS);
        entries.erase(vector_it);
    });
}

void TextureCache::TrackImage(ImageId image_id, bool owner, bool assume_locked) {
    auto& image = slot_images[image_id];
    if (False(image.flags & ImageFlagBits::Registered)) {
        return;
    }
    constexpr auto dropped = ImageFlagBits::HeadUntracked | ImageFlagBits::TailUntracked;
    const bool was_owner = True(image.flags & ImageFlagBits::ReadTracked);
    const bool tracked = True(image.flags & ImageFlagBits::Tracked);
    if (tracked && was_owner == owner && False(image.flags & dropped)) {
        return;
    }

    std::unique_lock lk{image.mutex, std::defer_lock};
    if (!assume_locked) {
        lk.lock();
    }
    // Pages the image does not watch yet take both watchers at once, while the ones it already
    // watches only need their read watcher brought in line with the ownership claim.
    const auto read_op = owner ? PageOp::Track : PageOp::None;
    const auto [tracked_begin, tracked_end] = image.TrackedRange();
    const auto image_begin = image.info.guest_address;
    const auto image_end = image_begin + image.info.guest_size;
    if (!tracked) {
        gpu_memory.UpdatePageWatchers(image_begin, image.info.guest_size, PageOp::Track, read_op);
    } else {
        if (was_owner != owner && tracked_begin < tracked_end) {
            gpu_memory.UpdatePageWatchers(tracked_begin, tracked_end - tracked_begin, PageOp::None,
                                          owner ? PageOp::Track : PageOp::Untrack);
        }
        if (image_begin < tracked_begin) {
            gpu_memory.UpdatePageWatchers(image_begin, tracked_begin - image_begin, PageOp::Track,
                                          read_op);
        }
        if (tracked_end < image_end) {
            gpu_memory.UpdatePageWatchers(tracked_end, image_end - tracked_end, PageOp::Track,
                                          read_op);
        }
    }
    image.flags |= ImageFlagBits::Tracked;
    image.flags &= ~dropped;
    if (owner) {
        image.flags |= ImageFlagBits::ReadTracked;
    } else {
        image.flags &= ~ImageFlagBits::ReadTracked;
    }
    MarkDirtyIfEdgesModified(image);
    image.head_hash = 0;
    image.tail_hash = 0;
}

void TextureCache::UntrackImage(ImageId image_id) {
    auto& image = slot_images[image_id];
    const auto [tracked_begin, tracked_end] = image.TrackedRange();
    if (False(image.flags & ImageFlagBits::Tracked)) {
        return;
    }
    const auto read_op =
        True(image.flags & ImageFlagBits::ReadTracked) ? PageOp::Untrack : PageOp::None;
    if (tracked_begin < tracked_end) {
        gpu_memory.UpdatePageWatchers(tracked_begin, tracked_end - tracked_begin, PageOp::Untrack,
                                      read_op);
    }
    image.flags &= ~(ImageFlagBits::Tracked | ImageFlagBits::HeadUntracked |
                     ImageFlagBits::TailUntracked | ImageFlagBits::ReadTracked);
}

void TextureCache::UntrackImageHead(ImageId image_id) {
    auto& image = slot_images[image_id];
    if (False(image.flags & ImageFlagBits::Tracked) ||
        True(image.flags & ImageFlagBits::HeadUntracked)) {
        return;
    }
    const auto image_begin = image.info.guest_address;
    const auto head_end = GetNextPageAddr(image_begin);
    image.head_hash = HashBackingMemory(image_begin, head_end - image_begin);
    gpu_memory.UpdatePageWatchers(image_begin, head_end - image_begin, PageOp::Untrack,
                                  True(image.flags & ImageFlagBits::ReadTracked) ? PageOp::Untrack
                                                                                 : PageOp::None);

    image.flags |= ImageFlagBits::HeadUntracked;
    const auto [tracked_begin, tracked_end] = image.TrackedRange();
    if (tracked_begin == tracked_end) {
        // This image spans only 2 pages and both are modified, but the image itself was not
        // directly affected. Check its hash later.
        MarkAsMaybeDirty(image_id, image);
    }
}

void TextureCache::UntrackImageTail(ImageId image_id) {
    auto& image = slot_images[image_id];
    if (False(image.flags & ImageFlagBits::Tracked) ||
        True(image.flags & ImageFlagBits::TailUntracked)) {
        return;
    }
    const auto image_end = image.info.guest_address + image.info.guest_size;
    const auto tail_begin = GetPageAddr(image_end);
    image.tail_hash = HashBackingMemory(tail_begin, image_end - tail_begin);
    gpu_memory.UpdatePageWatchers(tail_begin, image_end - tail_begin, PageOp::Untrack,
                                  True(image.flags & ImageFlagBits::ReadTracked) ? PageOp::Untrack
                                                                                 : PageOp::None);

    image.flags |= ImageFlagBits::TailUntracked;
    const auto [tracked_begin, tracked_end] = image.TrackedRange();
    if (tracked_begin == tracked_end) {
        MarkAsMaybeDirty(image_id, image);
    }
}

void TextureCache::MarkDirtyIfEdgesModified(Image& image) {
    const auto image_begin = image.info.guest_address;
    const auto image_end = image.info.guest_address + image.info.guest_size;
    if (image.head_hash != 0) {
        const auto head_end = GetNextPageAddr(image_begin);
        if (HashBackingMemory(image_begin, head_end - image_begin) != image.head_hash) {
            image.MarkDirty(ImageFlagBits::CpuDirty, image_begin, head_end);
        }
    }
    if (image.tail_hash != 0) {
        const auto tail_begin = GetPageAddr(image_end);
        if (HashBackingMemory(tail_begin, image_end - tail_begin) != image.tail_hash) {
            image.MarkDirty(ImageFlagBits::CpuDirty, tail_begin, image_end);
        }
    }
}

void TextureCache::GarbageCollectImages() {
    if (instance.CanReportMemoryUsage()) {
        total_used_memory = instance.GetDeviceMemoryUsage();
    }
    if (total_used_memory < trigger_gc_memory) {
        return;
    }
    bool pressured = false;
    bool aggresive = false;
    u64 ticks_to_destroy = 0;
    size_t num_deletions = 0;
    boost::container::small_vector<ImageId, 8> download_queue;
    boost::container::small_vector<ImageId, 8> maybe_download;

    const auto configure = [&](bool allow_aggressive) {
        pressured = total_used_memory >= pressure_gc_memory;
        aggresive = allow_aggressive && total_used_memory >= critical_gc_memory;
        ticks_to_destroy = aggresive ? 160 : pressured ? 80 : 16;
        ticks_to_destroy = std::min(ticks_to_destroy, gc_tick);
        num_deletions = aggresive ? 40 : pressured ? 20 : 10;
    };
    const auto clean_up = [&](Image& image) {
        if (num_deletions == 0) {
            return true;
        }
        // An edge write means the guest reused the memory, so the image is freed without download.
        MarkDirtyIfEdgesModified(image);
        const bool download = image.HasGpuValidData();
        if (download && (!pressured || !image.CanBeDownloaded())) {
            return false;
        }
        --num_deletions;
        const auto image_id = slot_images.GetSlotId(image);
        if (download) {
            UntrackImage(image_id);
            UnregisterImage(image_id);
            if (image.gpu_hash != 0) {
                buffer_cache.ReadMemory(image.info.guest_address, image.info.guest_size, false,
                                        true, true);
                maybe_download.push_back(image_id);
            } else {
                download_queue.push_back(image_id);
            }
        } else {
            FreeImage(image_id);
        }
        if (total_used_memory < critical_gc_memory) {
            if (aggresive) {
                num_deletions >>= 2;
                aggresive = false;
                return false;
            }
            if (pressured && total_used_memory < pressure_gc_memory) {
                num_deletions >>= 1;
                pressured = false;
            }
        }
        return false;
    };

    // Try to remove anything old enough and not high priority.
    configure(false);
    image_lru_cache.ForEachItemBelow(gc_tick - ticks_to_destroy, clean_up);

    if (total_used_memory >= critical_gc_memory) {
        // If we are still over the critical limit, run an aggressive GC
        configure(true);
        image_lru_cache.ForEachItemBelow(gc_tick - ticks_to_destroy, clean_up);
    }

    if (!maybe_download.empty()) {
        // wait for memory reads to finish
        scheduler.Finish();
        scheduler.PopPendingOperations();

        for (const auto& image_id : maybe_download) {
            const auto& image = slot_images[image_id];
            const u64 hash = HashBackingMemory(image.info.guest_address, image.info.guest_size);
            if (image.gpu_hash == hash) {
                download_queue.push_back(image_id);
            } else {
                DeleteImage(image_id);
            }
        }
    }

    for (const auto& image_id : download_queue) {
        buffer_cache.ReadEdgeImagePages(slot_images[image_id]);
    }
    for (const auto& image_id : download_queue) {
        DownloadImageMemory(image_id, DownloadMemoryFlags::DiscardBufferCache);
    }

    if (!download_queue.empty()) {
        // We need to make downloads synchronous. It is possible that the contents
        // of the image are requested before they are downloaded in which case
        // outdated buffer cache contents are used instead.
        scheduler.Finish();
        scheduler.PopPendingOperations();
    }

    for (const auto& image_id : download_queue) {
        DeleteImage(image_id);
    }
}

void TextureCache::GarbageCollectSamplers() {
    total_used_samplers = samplers.size();
    if (total_used_samplers < trigger_gc_samplers) {
        return;
    }
    bool pressured = false;
    bool aggresive = false;
    u64 ticks_to_destroy = 0;
    size_t num_deletions = 0;

    const auto configure = [&](bool allow_aggressive) {
        pressured = total_used_samplers >= pressure_gc_samplers;
        aggresive = allow_aggressive && total_used_samplers >= critical_gc_samplers;
        ticks_to_destroy = aggresive ? 160 : pressured ? 80 : 16;
        ticks_to_destroy = std::min(ticks_to_destroy, gc_tick);
        num_deletions = aggresive ? 40 : pressured ? 20 : 10;
    };
    const auto clean_up = [&](Sampler& sampler) {
        if (num_deletions == 0) {
            return true;
        }
        --num_deletions;
        sampler_lru_cache.Free(sampler);
        samplers.erase(sampler.hash);
        const auto sampler_id = slot_samplers.GetSlotId(sampler);
        slot_samplers.Erase(sampler_id);
        return false;
    };

    // Try to remove anything old enough and not high priority.
    configure(false);
    sampler_lru_cache.ForEachItemBelow(gc_tick - ticks_to_destroy, clean_up);

    if (total_used_samplers >= critical_gc_samplers) {
        // If we are still over the critical limit, run an aggressive GC
        configure(true);
        sampler_lru_cache.ForEachItemBelow(gc_tick - ticks_to_destroy, clean_up);
    }
}

void TextureCache::RunGarbageCollector() {
    GarbageCollectImages();
    GarbageCollectSamplers();
    ++gc_tick;
}

void TextureCache::TouchImage(Image& image) {
    image_lru_cache.Touch(image, gc_tick);
}

void TextureCache::DeleteImage(ImageId image_id) {
    Image& image = slot_images[image_id];
    ASSERT_MSG(False(image.flags & ImageFlagBits::Tracked), "Image was not untracked");
    ASSERT_MSG(False(image.flags & ImageFlagBits::Registered), "Image was not unregistered");

    // Remove any registered meta areas.
    const auto& meta_info = image.info.meta_info;
    if (meta_info.cmask_addr) {
        UnregisterMeta(meta_info.cmask_addr, image.image_uid);
    }
    if (meta_info.fmask_addr) {
        UnregisterMeta(meta_info.fmask_addr, image.image_uid);
    }
    if (meta_info.htile_addr) {
        UnregisterMeta(meta_info.htile_addr, image.image_uid);
    }

    {
        std::unique_lock lk{download_images_mutex};
        if (download_images.contains(image_id)) {
            download_images.erase(image_id);
        }
    }

    // Reclaim image and any image views it references.
    scheduler.DeferOperation([this, image_id] {
        Image& image = slot_images[image_id];
        for (auto& backing : image.backing_images) {
            for (const ImageViewId image_view_id : backing.image_view_ids) {
                slot_image_views.Erase(image_view_id);
            }
        }
        slot_images.Erase(image_id);
    });
}

} // namespace VideoCore
