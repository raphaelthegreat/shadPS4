// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <xxhash.h>
#include "common/assert.h"
#include "video_core/buffer_cache/buffer_cache.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/texture_cache/texture_cache.h"
#include "video_core/texture_cache/tile_manager.h"
#include "video_core/page_manager.h"

namespace VideoCore {

static constexpr u64 PageShift = 12;
static constexpr u64 StreamBufferSize = 256_MB;

TextureCache::TextureCache(const Vulkan::Instance& instance_, Vulkan::Scheduler& scheduler_,
                           BufferCache& buffer_cache_, PageManager& tracker_)
    : instance{instance_}, scheduler{scheduler_}, buffer_cache{buffer_cache_}, tracker{tracker_},
      tile_manager{instance, scheduler}, staging{instance, scheduler, vk::BufferUsageFlagBits::eTransferSrc, StreamBufferSize,
                                                 Vulkan::BufferType::Upload} {
    const ImageInfo info{vk::Format::eR8G8B8A8Unorm};
    const ImageId null_id = slot_images.insert(instance, scheduler, info, 0);
    ASSERT(null_id.index == 0);

    ImageViewInfo view_info;
    void(slot_image_views.insert(instance, view_info, slot_images[null_id], null_id));
}

TextureCache::~TextureCache() = default;

void TextureCache::InvalidateMemory(VAddr address, size_t size) {
    std::scoped_lock lk{mutex};

    ForEachImageInRegion(address, size, [&](ImageId image_id, Image& image) {
        // Ensure image is reuploaded when accessed again.
        image.flags |= ImageFlagBits::CpuModified;
        // Untrack image, so the range is unprotected and the guest can write freely.
        UntrackImage(image, image_id);
    });
}

void TextureCache::UnmapMemory(VAddr cpu_addr, size_t size) {
    std::scoped_lock lk{mutex};

    boost::container::small_vector<ImageId, 16> deleted_images;
    ForEachImageInRegion(cpu_addr, size, [&](ImageId id, Image&) {
        deleted_images.push_back(id);
    });
    for (const ImageId id : deleted_images) {
        Image& image = slot_images[id];
        if (True(image.flags & ImageFlagBits::Tracked)) {
            UntrackImage(image, id);
        }
        UnregisterImage(id);
        DeleteImage(id);
    }
}

ImageId TextureCache::FindImage(const ImageInfo& info, VAddr cpu_address, bool refresh_on_create) {
    std::scoped_lock lk{mutex};

    boost::container::small_vector<ImageId, 2> image_ids;
    ForEachImageInRegion(cpu_address, info.guest_size_bytes, [&](ImageId image_id, Image& image) {
        // Address and width must match.
        if (image.cpu_addr != cpu_address || image.info.size.width != info.size.width) {
            return;
        }
        if (info.IsDepthStencil() != image.info.IsDepthStencil() &&
            info.pixel_format != vk::Format::eR32Sfloat) {
            return;
        }
        image_ids.push_back(image_id);
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

    return image_id;
}

ImageView& TextureCache::RegisterImageView(ImageId image_id, const ImageViewInfo& view_info) {
    Image& image = slot_images[image_id];
    if (const ImageViewId view_id = image.FindView(view_info); view_id) {
        return slot_image_views[view_id];
    }

    // All tiled images are created with storage usage flag. This makes set of formats (e.g. sRGB)
    // impossible to use. However, during view creation, if an image isn't used as storage we can
    // temporary remove its storage bit.
    std::optional<vk::ImageUsageFlags> usage_override;
    if (!image.info.usage.storage) {
        usage_override = image.usage & ~vk::ImageUsageFlagBits::eStorage;
    }

    const ImageViewId view_id =
        slot_image_views.insert(instance, view_info, image, image_id, usage_override);
    image.image_view_infos.emplace_back(view_info);
    image.image_view_ids.emplace_back(view_id);
    return slot_image_views[view_id];
}

ImageView& TextureCache::FindImageView(const AmdGpu::Image& desc, bool is_storage) {
    const ImageInfo info{desc};
    const ImageId image_id = FindImage(info, desc.Address());
    Image& image = slot_images[image_id];
    auto& usage = image.info.usage;

    if (is_storage) {
        image.Transit(vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderWrite);
        usage.storage = true;
    } else {
        const auto new_layout = image.info.IsDepthStencil()
                                    ? vk::ImageLayout::eDepthStencilReadOnlyOptimal
                                    : vk::ImageLayout::eShaderReadOnlyOptimal;
        image.Transit(new_layout, vk::AccessFlagBits::eShaderRead);
        usage.texture = true;
    }

    const ImageViewInfo view_info{desc, is_storage};
    return RegisterImageView(image_id, view_info);
}

ImageView& TextureCache::RenderTarget(const AmdGpu::Liverpool::ColorBuffer& buffer,
                                      const AmdGpu::Liverpool::CbDbExtent& hint) {
    const ImageInfo info{buffer, hint};
    const ImageId image_id = FindImage(info, buffer.Address());
    Image& image = slot_images[image_id];
    image.flags &= ~ImageFlagBits::CpuModified;

    image.Transit(vk::ImageLayout::eColorAttachmentOptimal,
                  vk::AccessFlagBits::eColorAttachmentWrite |
                      vk::AccessFlagBits::eColorAttachmentRead);

    image.info.usage.render_target = true;

    if (False(image.flags & ImageFlagBits::MetaRegistered)) {
        if (info.meta_info.cmask_addr) {
            const MetaDataInfo cmask_meta{.type = MetaDataInfo::Type::CMask, .is_cleared = true};
            surface_metas.emplace(info.meta_info.cmask_addr, cmask_meta);
            image.info.meta_info.cmask_addr = info.meta_info.cmask_addr;
        }
        if (info.meta_info.fmask_addr) {
            const MetaDataInfo fmask_meta{.type = MetaDataInfo::Type::FMask, .is_cleared = true};
            surface_metas.emplace(info.meta_info.fmask_addr, fmask_meta);
            image.info.meta_info.fmask_addr = info.meta_info.fmask_addr;
        }
        image.flags |= ImageFlagBits::MetaRegistered;
    }

    ImageViewInfo view_info{buffer, !!image.info.usage.vo_buffer};
    return RegisterImageView(image_id, view_info);
}

ImageView& TextureCache::DepthTarget(const AmdGpu::Liverpool::DepthBuffer& buffer,
                                     VAddr htile_address, const AmdGpu::Liverpool::CbDbExtent& hint,
                                     bool write_enabled) {
    const ImageInfo info{buffer, htile_address, hint};
    const ImageId image_id = FindImage(info, buffer.Address(), false);
    Image& image = slot_images[image_id];
    image.flags &= ~ImageFlagBits::CpuModified;

    const auto new_layout = write_enabled ? vk::ImageLayout::eDepthStencilAttachmentOptimal
                                          : vk::ImageLayout::eDepthStencilReadOnlyOptimal;
    image.Transit(new_layout, vk::AccessFlagBits::eDepthStencilAttachmentWrite |
                                  vk::AccessFlagBits::eDepthStencilAttachmentRead);

    image.info.usage.depth_target = true;

    if (False(image.flags & ImageFlagBits::MetaRegistered)) {
        const MetaDataInfo htile_meta{.type = MetaDataInfo::Type::HTile, .is_cleared = true};
        surface_metas.emplace(info.meta_info.htile_addr, htile_meta);
        image.info.meta_info.htile_addr = info.meta_info.htile_addr;
        image.flags |= ImageFlagBits::MetaRegistered;
    }

    ImageViewInfo view_info;
    view_info.format = info.pixel_format;
    return RegisterImageView(image_id, view_info);
}

void TextureCache::RefreshImage(Image& image) {
    // Mark image as validated.
    image.flags &= ~ImageFlagBits::CpuModified;

    if (!tile_manager.TryDetile(image)) {
        // Upload data to the staging buffer.
        const auto offset = staging.Copy(image.cpu_addr, image.info.guest_size_bytes, 4);
        // Copy to the image.
        image.Upload(staging.Handle(), offset);
    }

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
}

void TextureCache::TrackImage(Image& image, ImageId image_id) {
    if (True(image.flags & ImageFlagBits::Tracked)) {
        return;
    }
    image.flags |= ImageFlagBits::Tracked;
    tracker.UpdatePagesCachedCount(image.cpu_addr, image.info.guest_size_bytes, 1);
}

void TextureCache::UntrackImage(Image& image, ImageId image_id) {
    if (False(image.flags & ImageFlagBits::Tracked)) {
        return;
    }
    LOG_INFO(Render_Vulkan, "Untracking image addr = {:#x}, size = {:#x}",
             image.cpu_addr, image.info.guest_size_bytes);
    image.flags &= ~ImageFlagBits::Tracked;
    tracker.UpdatePagesCachedCount(image.cpu_addr, image.info.guest_size_bytes, -1);
}

void TextureCache::DeleteImage(ImageId image_id) {
    Image& image = slot_images[image_id];
    ASSERT_MSG(False(image.flags & ImageFlagBits::Tracked), "Image was not untracked");
    ASSERT_MSG(False(image.flags & ImageFlagBits::Registered), "Image was not unregistered");

    // Remove any registered meta areas.
    const auto& meta_info = image.info.meta_info;
    if (meta_info.cmask_addr) {
        surface_metas.erase(meta_info.cmask_addr);
    }
    if (meta_info.fmask_addr) {
        surface_metas.erase(meta_info.fmask_addr);
    }
    if (meta_info.htile_addr) {
        surface_metas.erase(meta_info.htile_addr);
    }

    // Reclaim image and any image views it references.
    scheduler.DeferOperation([this, image_id] {
        Image& image = slot_images[image_id];
        for (const ImageViewId image_view_id : image.image_view_ids) {
            slot_image_views.erase(image_view_id);
        }
        slot_images.erase(image_id);
    });
}

} // namespace VideoCore
