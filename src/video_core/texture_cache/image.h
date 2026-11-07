// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <limits>
#include <mutex>
#include <optional>

#include "common/enum.h"
#include "common/incremental_id.h"
#include "common/lru_cache.h"
#include "common/small_vector.h"
#include "common/types.h"
#include "video_core/renderer_vulkan/vk_common.h"
#include "video_core/texture_cache/host_compatibility.h"
#include "video_core/texture_cache/image_info.h"
#include "video_core/texture_cache/image_view.h"

namespace Vulkan {
class Instance;
class Runtime;
} // namespace Vulkan

VK_DEFINE_HANDLE(VmaAllocation)
VK_DEFINE_HANDLE(VmaAllocator)

namespace VideoCore {

enum ImageFlagBits : u32 {
    Empty = 0,
    MaybeCpuDirty = 1 << 0, ///< The page this image is in was touched before the image address
    CpuDirty = 1 << 1,      ///< Contents have been modified from the CPU
    GpuDirty = 1 << 2, ///< Image contents have been modified from the GPU (valid data in buffer
                       ///< cache)
    Dirty = MaybeCpuDirty | CpuDirty | GpuDirty,
    GpuModified = 1 << 3,   ///< Contents have been modified from the GPU
    Owned = 1 << 4,         ///< Image holds data newer than guest memory over its guest range
    ReadTracked = 1 << 5,   ///< Read watchers were taken alongside the write ones
    Registered = 1 << 6,    ///< True when the image is registered
    Tracked = 1 << 7,       ///< Write watchers are held over the image range
    HeadUntracked = 1 << 8, ///< The watcher on the partly occupied first page was dropped
    TailUntracked = 1 << 9, ///< The watcher on the partly occupied last page was dropped
    Displaced = 1 << 10,    ///< A buffer took over part of the range this image had claimed, so the
                            ///< image must not be written back over it
};
DECLARE_ENUM_FLAG_OPERATORS(ImageFlagBits)

vk::ImageUsageFlags ImageUsageFlags(const Vulkan::Instance& instance, const ImageInfo& info,
                                    bool needs_storage);

struct UniqueImage {
    explicit UniqueImage() = default;
    explicit UniqueImage(vk::Device device, VmaAllocator allocator)
        : device{device}, allocator{allocator} {}
    ~UniqueImage();

    UniqueImage(const UniqueImage&) = delete;
    UniqueImage& operator=(const UniqueImage&) = delete;

    UniqueImage(UniqueImage&& other)
        : allocator{std::exchange(other.allocator, VK_NULL_HANDLE)},
          allocation{std::exchange(other.allocation, VK_NULL_HANDLE)},
          image{std::exchange(other.image, VK_NULL_HANDLE)}, image_ci{std::move(other.image_ci)},
          size_bytes{std::exchange(other.size_bytes, 0)} {}
    UniqueImage& operator=(UniqueImage&& other) {
        image = std::exchange(other.image, VK_NULL_HANDLE);
        allocator = std::exchange(other.allocator, VK_NULL_HANDLE);
        allocation = std::exchange(other.allocation, VK_NULL_HANDLE);
        image_ci = std::move(other.image_ci);
        size_bytes = std::exchange(other.size_bytes, 0);
        return *this;
    }

    void Create(const vk::ImageCreateInfo& image_ci);

    void Destroy();

    operator vk::Image() const {
        return image;
    }

    operator bool() const {
        return image;
    }

public:
    vk::Device device{};
    VmaAllocator allocator{};
    VmaAllocation allocation{};
    vk::Image image{};
    vk::ImageCreateInfo image_ci{};
    vk::DeviceSize size_bytes{};
};

struct Image : public Common::LRUNode<> {
    explicit Image(const Vulkan::Instance& instance, Vulkan::Runtime& runtime,
                   Common::SlotVector<ImageView>& slot_image_views, const ImageInfo& info,
                   bool needs_storage = false);
    ~Image();

    Image(const Image&) = delete;
    Image& operator=(const Image&) = delete;

    Image(Image&&) = delete;
    Image& operator=(Image&&) = delete;

    bool Overlaps(VAddr addr, size_t size) const noexcept {
        return info.guest_address < (addr + size) && addr < (info.guest_address + info.guest_size);
    }

    vk::Image GetImage() const {
        return backing->image.image;
    }

    /// Range of guest memory the image currently holds write watchers over. The edges are given
    /// up once a neighbouring resource faults on a page the image only partly occupies, so that
    /// its writes stop faulting; the hash of the image bytes in there is what recovers from it.
    std::pair<VAddr, VAddr> TrackedRange() const noexcept;

    /// Widens the range the image has to be refilled from to also cover [begin, end).
    void MarkDirty(ImageFlagBits kind, VAddr begin, VAddr end) noexcept {
        flags |= kind;
        if (dirty_begin >= dirty_end) {
            dirty_begin = begin;
            dirty_end = end;
        } else {
            dirty_begin = std::min(dirty_begin, begin);
            dirty_end = std::max(dirty_end, end);
        }
    }

    void MarkDirty(ImageFlagBits kind) noexcept {
        MarkDirty(kind, info.guest_address, info.guest_address + info.guest_size);
    }

    void ClearDirty() noexcept {
        flags &= ~ImageFlagBits::Dirty;
        dirty_begin = dirty_end = 0;
    }

    bool IsDirtyAt(VAddr begin, VAddr end) const noexcept {
        return dirty_begin < end && begin < dirty_end;
    }

    bool HasGpuValidData() const {
        return True(flags & ImageFlagBits::GpuModified) && False(flags & ImageFlagBits::Dirty);
    }

    bool CanBeDownloaded() {
        return HasGpuValidData() && info.num_samples == 1 && !info.props.has_stencil &&
               IsVulkanFormatCompatible(info.pixel_format, backing->image.image_ci.format);
    }

    bool HasUsage(vk::ImageUsageFlags required) const noexcept {
        return ((usage_flags | unsupported_usage) & required) == required;
    }

    void AssociateDepth(ImageId depth_image_id, u64 depth_image_uid) {
        depth_id = depth_image_id;
        depth_uid = depth_image_uid;
    }

    void DisassociateDepth() {
        depth_id = {};
        depth_uid = {};
    }

    vk::DeviceSize SizeBytes() const {
        return std::accumulate(
            backing_images.begin(), backing_images.end(), vk::DeviceSize(0),
            [](vk::DeviceSize sum, const BackingImage& bi) { return sum + bi.image.size_bytes; });
    }

    ImageView& FindView(const ImageViewInfo& view_info, bool ensure_guest_samples = true);

    using Barriers = SmallVector<vk::ImageMemoryBarrier2, 32>;
    void GetBarriers(Barriers& out_barriers, vk::ImageLayout dst_layout, vk::AccessFlags2 dst_mask,
                     vk::PipelineStageFlags2 dst_stage,
                     std::optional<SubresourceRange> subres_range = {});

public:
    Vulkan::Runtime* runtime;
    Common::SlotVector<ImageView>* slot_image_views;
    std::mutex mutex;
    ImageInfo info;
    vk::ImageAspectFlags aspect_mask = vk::ImageAspectFlagBits::eColor;
    vk::SampleCountFlags supported_samples = vk::SampleCountFlagBits::e1;
    ImageFlagBits flags = ImageFlagBits::Dirty;
    /// Guest range the image has to be refilled from, as the union of everything written to it.
    /// A fresh image has nothing at all, so it starts out covering every address.
    VAddr dirty_begin = 0;
    VAddr dirty_end = std::numeric_limits<VAddr>::max();
    ImageId depth_id{};
    u64 depth_uid{};

    vk::ImageUsageFlags usage_flags;
    vk::ImageUsageFlags unsupported_usage; ///< Usage the image was created without and cannot gain
    vk::FormatFeatureFlags2 format_features;
    struct State {
        vk::PipelineStageFlags2 pl_stage = vk::PipelineStageFlagBits2::eAllCommands;
        vk::AccessFlags2 access_mask = vk::AccessFlagBits2::eNone;
        vk::ImageLayout layout = vk::ImageLayout::eUndefined;
    };
    struct BackingImage {
        UniqueImage image;
        State state;
        std::vector<State> subresource_states;
        SmallVector<ImageViewInfo, 2> image_view_infos;
        SmallVector<ImageViewId, 2> image_view_ids;
        u32 num_samples;
    };
    SmallVector<BackingImage, 2> backing_images;
    BackingImage* backing{};
    u64* memory_usage_counter{};
    u64 image_uid{};
    u64 lru_id{};
    u64 tick_accessed_last{};
    u64 cpu_hash{};
    u64 head_hash{};
    u64 tail_hash{};

    struct {
        u32 texture : 1;
        u32 storage : 1;
        u32 render_target : 1;
        u32 depth_target : 1;
        u32 vo_surface : 1;
    } usage{};

    struct {
        u32 is_bound : 1;
        u32 is_target : 1;
        u32 needs_rebind : 1;
        u32 force_general : 1;
    } binding{};

private:
    static Common::IncrementalIdProvider<u64> global_image_uid;
};

} // namespace VideoCore
