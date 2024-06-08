// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <xxhash.h>
#include "common/assert.h"
#include "common/config.h"
#include "common/error.h"
#include "core/virtual_memory.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/texture_cache/texture_cache.h"
#include "video_core/texture_cache/tile_manager.h"

#ifndef _WIN64
#include <signal.h>
#include <sys/mman.h>

#define PAGE_NOACCESS PROT_NONE
#define PAGE_READWRITE (PROT_READ | PROT_WRITE)
#define PAGE_READONLY PROT_READ
#else
#include <windows.h>

void mprotect(void* addr, size_t len, int prot) {
    DWORD old_prot{};
    BOOL result = VirtualProtect(addr, len, prot, &old_prot);
    ASSERT_MSG(result != 0, "Region protection failed {}", Common::GetLastErrorMsg());
}

#endif

namespace VideoCore {

static TextureCache* g_texture_cache = nullptr;

#ifndef _WIN64
void GuestFaultSignalHandler(int sig, siginfo_t* info, void* raw_context) {
    ucontext_t* ctx = reinterpret_cast<ucontext_t*>(raw_context);
    const VAddr address = reinterpret_cast<VAddr>(info->si_addr);
    if (ctx->uc_mcontext.gregs[REG_ERR] & 0x2) {
        g_texture_cache->OnCpuWrite(address);
    } else {
        // Read not supported!
        UNREACHABLE();
    }
}
#else
LONG WINAPI GuestFaultSignalHandler(EXCEPTION_POINTERS* pExp) noexcept {
    const u32 ec = pExp->ExceptionRecord->ExceptionCode;
    if (ec == EXCEPTION_ACCESS_VIOLATION) {
        const auto info = pExp->ExceptionRecord->ExceptionInformation;
        if (info[0] == 1) { // Write violation
            g_texture_cache->OnCpuWrite(info[1]);
            return EXCEPTION_CONTINUE_EXECUTION;
        } /* else {
            UNREACHABLE();
        }*/
    }
    return EXCEPTION_CONTINUE_SEARCH; // pass further
}
#endif

static constexpr u64 StreamBufferSize = 512_MB;
static constexpr u64 PageShift = 12;

TextureCache::TextureCache(const Vulkan::Instance& instance_, Vulkan::Scheduler& scheduler_)
    : instance{instance_}, scheduler{scheduler_},
      staging{instance, scheduler, vk::BufferUsageFlagBits::eTransferSrc, StreamBufferSize,
              Vulkan::BufferType::Upload},
      tile_manager{instance, scheduler} {

#ifndef _WIN64
    sigset_t signal_mask;
    sigemptyset(&signal_mask);
    sigaddset(&signal_mask, SIGSEGV);

    using HandlerType = decltype(sigaction::sa_sigaction);

    struct sigaction guest_access_fault {};
    guest_access_fault.sa_flags = SA_SIGINFO | SA_ONSTACK;
    guest_access_fault.sa_sigaction = &GuestFaultSignalHandler;
    guest_access_fault.sa_mask = signal_mask;
    sigaction(SIGSEGV, &guest_access_fault, nullptr);
#else
    veh_handle = AddVectoredExceptionHandler(0, GuestFaultSignalHandler);
    ASSERT_MSG(veh_handle, "Failed to register an exception handler");
#endif
    g_texture_cache = this;

    ImageInfo info;
    info.pixel_format = vk::Format::eR8G8B8A8Unorm;
    info.type = vk::ImageType::e2D;
    const ImageId null_id = slot_images.insert(instance, scheduler, info, 0);
    ASSERT(null_id.index == 0);

    ImageViewInfo view_info;
    void(slot_image_views.insert(instance, view_info, slot_images[null_id]));
}

TextureCache::~TextureCache() {
#if _WIN64
    RemoveVectoredExceptionHandler(veh_handle);
#endif
}

void TextureCache::OnCpuWrite(VAddr address) {
    std::unique_lock lock{m_page_table};
    ForEachImageInRegion(address, 1 << PageShift, [&](ImageId image_id, Image& image) {
        // Ensure image is reuploaded when accessed again.
        image.flags |= ImageFlagBits::CpuModified;
        // Untrack image, so the range is unprotected and the guest can write freely.
        UntrackImage(image, image_id);
    });
}

Image& TextureCache::FindImage(const ImageInfo& info, VAddr cpu_address, bool refresh_on_create) {
    std::unique_lock lock{m_page_table};
    boost::container::small_vector<ImageId, 2> image_ids;
    ForEachImageInRegion(cpu_address, info.guest_size_bytes, [&](ImageId image_id, Image& image) {
        if (image.cpu_addr == cpu_address && image.info.size.width == info.size.width) {
            image_ids.push_back(image_id);
        }
    });

    ASSERT_MSG(image_ids.size() <= 1, "Overlapping images not allowed!");

    ImageId image_id{};
    if (image_ids.empty()) {
        image_id = slot_images.insert(instance, scheduler, info, cpu_address);
        RegisterImage(image_id);
    } else {
        image_id = image_ids[0];
    }

    Image& image = slot_images[image_id];
    if (True(image.flags & ImageFlagBits::CpuModified) && refresh_on_create) {
        RefreshImage(image);
        TrackImage(image, image_id);
    }

    return image;
}

ImageView& TextureCache::RegisterImageView(Image& image, const ImageViewInfo& view_info) {
    if (const ImageViewId view_id = image.FindView(view_info); view_id) {
        return slot_image_views[view_id];
    }

    // All tiled images are created with storage usage flag. This makes set of formats (e.g. sRGB)
    // impossible to use. However, during view creation, if an image isn't used as storage we can
    // temporary remove its storage bit.
    std::optional<vk::ImageUsageFlags> usage_override;
    if (!image.info.is_storage) {
        usage_override = image.info.usage & ~vk::ImageUsageFlagBits::eStorage;
    }

    const ImageViewId view_id = slot_image_views.insert(instance, view_info, image, usage_override);
    image.image_view_infos.emplace_back(view_info);
    image.image_view_ids.emplace_back(view_id);
    return slot_image_views[view_id];
}

ImageView& TextureCache::FindImageView(const AmdGpu::Image& desc, bool is_storage) {
    Image& image = FindImage(ImageInfo{desc}, desc.Address());

    if (is_storage) {
        image.Transit(vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderWrite |
                                                 vk::AccessFlagBits::eShaderRead);
    } else {
        image.Transit(vk::ImageLayout::eShaderReadOnlyOptimal, vk::AccessFlagBits::eShaderRead);
    }

    const ImageViewInfo view_info{desc, is_storage};
    return RegisterImageView(image, view_info);
}

ImageView& TextureCache::RenderTarget(const AmdGpu::Liverpool::ColorBuffer& buffer,
                                      const AmdGpu::Liverpool::CbDbExtent& hint) {
    const ImageInfo info{buffer, hint};
    auto& image = FindImage(info, buffer.Address(), false);
    image.flags &= ~ImageFlagBits::CpuModified;

    image.Transit(vk::ImageLayout::eColorAttachmentOptimal,
                  vk::AccessFlagBits::eColorAttachmentWrite |
                      vk::AccessFlagBits::eColorAttachmentRead);

    ImageViewInfo view_info;
    view_info.format = info.pixel_format;
    return RegisterImageView(image, view_info);
}

ImageView& TextureCache::DepthTarget(const AmdGpu::Liverpool::DepthBuffer& buffer,
                                     const AmdGpu::Liverpool::CbDbExtent& hint) {
    const ImageInfo info{buffer, hint};
    auto& image = FindImage(info, buffer.Address(), false);
    image.flags &= ~ImageFlagBits::CpuModified;

    image.Transit(vk::ImageLayout::eDepthStencilAttachmentOptimal,
                  vk::AccessFlagBits::eDepthStencilAttachmentWrite |
                      vk::AccessFlagBits::eDepthStencilAttachmentRead);

    ImageViewInfo view_info;
    view_info.format = info.pixel_format;
    return RegisterImageView(image, view_info);
}

void TextureCache::RefreshImage(Image& image) {
    // Mark image as validated.
    image.flags &= ~ImageFlagBits::CpuModified;

    const u8* image_data = reinterpret_cast<const u8*>(image.cpu_addr);
    const auto [staging_data, offset, _] = staging.Map(image.info.guest_size_bytes, 16);
    if (image.info.texinfo.tm == GnmTileMode::GNM_TM_DISPLAY_LINEAR_GENERAL) {
        std::memcpy(staging_data, image_data, image.info.guest_size_bytes);
    } else {
        const GpaError res = gpaDetileTextureAll(image_data, image.info.guest_size_bytes, staging_data,
                                                 image.info.guest_size_bytes, &image.info.texinfo);
        ASSERT_MSG(res == GPA_ERR_OK, "Texture detiling failed with error: {}", gpaStrError(res));
    }
    staging.Commit(image.info.guest_size_bytes);

    // The mipmaps of each slice are next to each other in memory. So we iterate each layer
    // and detile its mipmaps. Vulkan allows us to copy to the same mipmap of multiple layers at
    // once, so we try to upload in that order.
    boost::container::small_vector<vk::BufferImageCopy, 50> image_copies;
    for (u32 mip = 0; mip < image.info.resources.levels; mip++) {
        // Initialize tiling parameters.
        GpaTilingParams tp = {};
        GpaError res = gpaTpInit(&tp, &image.info.texinfo, mip, 0);
        ASSERT(res == GPA_ERR_OK);

        // Figure out the offset of the slice0 mip in the image data and its size.
        u64 surfoffset = 0;
        u64 surfsize = 0;
        res = gpaCalcSurfaceSizeOffset(&surfsize, &surfoffset, &image.info.texinfo, mip, 0);
        ASSERT(res == GPA_ERR_OK);

        // Add a new buffer copy for later.
        image_copies.push_back({
            .bufferOffset = offset + surfoffset,
            .bufferRowLength = 0,
            .bufferImageHeight = 0,
            .imageSubresource{
                .aspectMask = vk::ImageAspectFlagBits::eColor,
                .mipLevel = mip,
                .baseArrayLayer = 0,
                .layerCount = u32(image.info.resources.layers),
            },
            .imageOffset = {0, 0, 0},
            .imageExtent = {image.info.size.width >> mip, image.info.size.height >> mip, 1},
        });
    }

    // Perform copy.
    const auto cmdbuf = scheduler.CommandBuffer();
    image.Transit(vk::ImageLayout::eTransferDstOptimal, vk::AccessFlagBits::eTransferWrite);

    cmdbuf.copyBufferToImage(staging.Handle(), image.image,
                             vk::ImageLayout::eTransferDstOptimal, image_copies);

    image.Transit(vk::ImageLayout::eGeneral,
                  vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eTransferRead);
}

vk::Sampler TextureCache::GetSampler(const AmdGpu::Sampler& sampler) {
    const u64 hash = XXH3_64bits(&sampler, sizeof(sampler));
    const auto [it, new_sampler] = samplers.try_emplace(hash, instance, sampler);
    return it->second.Handle();
}

void TextureCache::RegisterImage(ImageId image_id) {
    Image& image = slot_images[image_id];
    ASSERT_MSG(False(image.flags & ImageFlagBits::Registered),
               "Trying to register an already registered image");
    image.flags |= ImageFlagBits::Registered;
    ForEachPage(image.cpu_addr, image.info.guest_size_bytes,
                [this, image_id](u64 page) { page_table[page].push_back(image_id); });
}

void TextureCache::UnregisterImage(ImageId image_id) {
    Image& image = slot_images[image_id];
    ASSERT_MSG(True(image.flags & ImageFlagBits::Registered),
               "Trying to unregister an already registered image");
    image.flags &= ~ImageFlagBits::Registered;
    ForEachPage(image.cpu_addr, image.info.guest_size_bytes, [this, image_id](u64 page) {
        const auto page_it = page_table.find(page);
        if (page_it == page_table.end()) {
            ASSERT_MSG(false, "Unregistering unregistered page=0x{:x}", page << PageShift);
            return;
        }
        auto& image_ids = page_it.value();
        const auto vector_it = std::ranges::find(image_ids, image_id);
        if (vector_it == image_ids.end()) {
            ASSERT_MSG(false, "Unregistering unregistered image in page=0x{:x}", page << PageShift);
            return;
        }
        image_ids.erase(vector_it);
    });
    slot_images.erase(image_id);
}

void TextureCache::TrackImage(Image& image, ImageId image_id) {
    if (True(image.flags & ImageFlagBits::Tracked)) {
        return;
    }
    image.flags |= ImageFlagBits::Tracked;
    UpdatePagesCachedCount(image.cpu_addr, image.info.guest_size_bytes, 1);
}

void TextureCache::UntrackImage(Image& image, ImageId image_id) {
    if (False(image.flags & ImageFlagBits::Tracked)) {
        return;
    }
    image.flags &= ~ImageFlagBits::Tracked;
    UpdatePagesCachedCount(image.cpu_addr, image.info.guest_size_bytes, -1);
}

void TextureCache::UpdatePagesCachedCount(VAddr addr, u64 size, s32 delta) {
    std::scoped_lock lk{mutex};
    const u64 num_pages = ((addr + size - 1) >> PageShift) - (addr >> PageShift) + 1;
    const u64 page_start = addr >> PageShift;
    const u64 page_end = page_start + num_pages;

    const auto pages_interval =
        decltype(cached_pages)::interval_type::right_open(page_start, page_end);
    if (delta > 0) {
        cached_pages.add({pages_interval, delta});
    }

    const auto& range = cached_pages.equal_range(pages_interval);
    for (const auto& [range, count] : boost::make_iterator_range(range)) {
        const auto interval = range & pages_interval;
        const VAddr interval_start_addr = boost::icl::first(interval) << PageShift;
        const VAddr interval_end_addr = boost::icl::last_next(interval) << PageShift;
        const u32 interval_size = interval_end_addr - interval_start_addr;
        void* addr = reinterpret_cast<void*>(interval_start_addr);
        if (delta > 0 && count == delta) {
            //mprotect(addr, interval_size, PAGE_READONLY);
        } else if (delta < 0 && count == -delta) {
            mprotect(addr, interval_size, PAGE_READWRITE);
        } else {
            ASSERT(count >= 0);
        }
    }

    if (delta < 0) {
        cached_pages.add({pages_interval, delta});
    }
}

} // namespace VideoCore
