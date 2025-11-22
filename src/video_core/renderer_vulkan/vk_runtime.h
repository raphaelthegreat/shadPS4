// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <span>
#include "video_core/buffer_cache/buffer.h"
#include "video_core/renderer_vulkan/vk_barrier_tracker.h"
#include "video_core/renderer_vulkan/vk_common.h"
#include "video_core/texture_cache/blit_helper.h"
#include "video_core/types.h"

namespace AmdGpu {
enum class IndexType : u32;
}

namespace VideoCore {
struct Image;
struct Buffer;
struct BackingImage;
} // namespace VideoCore

namespace Vulkan {

class Instance;
class Scheduler;

class Runtime {
public:
    explicit Runtime(const Instance& instance, Scheduler& scheduler);
    ~Runtime() = default;

    void CopyBuffer(const VideoCore::Buffer& src, const VideoCore::Buffer& dst,
                    std::span<const vk::BufferCopy> copies);
    void CopyBuffer(const VideoCore::Buffer& src, const VideoCore::Buffer& dst,
                    vk::BufferCopy copy);

    void FillBuffer(const VideoCore::Buffer& dst, u64 offset, u64 size, u32 value);

    void InlineData(VideoCore::Buffer& dst, u64 offset, u32 value);

    void UploadImage(VideoCore::Image& dst, VideoCore::Buffer& src, u64 copy_size,
                     std::span<const vk::BufferImageCopy> upload_copies);
    void DownloadImage(VideoCore::Image& src, VideoCore::Buffer& dst, u64 copy_size,
                       std::span<const vk::BufferImageCopy> download_copies);

    void CopyImage(VideoCore::Image& src, VideoCore::Image& dst);
    void CopyImageWithBuffer(VideoCore::Image& src, VideoCore::Image& dst,
                             const VideoCore::Buffer& buffer, u64 offset);

    void CopyColorAndDepth(VideoCore::Image& src, VideoCore::Image& dst);

    void CopyDepthStencil(VideoCore::Image& src, VideoCore::Image& dst,
                          const VideoCore::SubresourceRange& sub_range,
                          vk::ImageAspectFlags aspect);

    void ResolveImage(VideoCore::Image& src, VideoCore::Image& dst,
                      const VideoCore::SubresourceRange& src_range,
                      const VideoCore::SubresourceRange& dst_range);
    void ClearImage(VideoCore::Image& dst, const VideoCore::SubresourceRange& range,
                    const vk::ClearValue& clear_value);

    bool IsBufferAccessed(const VideoCore::Buffer& buffer, u64 offset, u64 size,
                          bool check_read_access = false);

    bool TransitionImageLayout(
        VideoCore::Image& image, vk::ImageLayout dst_layout, vk::PipelineStageFlags2 dst_stage,
        vk::AccessFlags2 dst_access,
        std::optional<VideoCore::SubresourceRange> subres_range = std::nullopt);

    bool TransitionBackingLayout(
        VideoCore::BackingImage* backing, const VideoCore::SubresourceExtent& resources,
        vk::ImageAspectFlags aspect_mask, vk::ImageLayout dst_layout,
        vk::PipelineStageFlags2 dst_stage, vk::AccessFlags2 dst_access,
        std::optional<VideoCore::SubresourceRange> subres_range = std::nullopt);

    void SetBackingSamples(VideoCore::Image& image, u32 num_samples, bool copy_backing = true);

    void FlushBarriers();

    void AccessBuffer(const VideoCore::Buffer& buffer, u64 offset, u64 size,
                      vk::PipelineStageFlags2 src_stage, vk::AccessFlags2 src_access);

private:
    const Instance& instance;
    Scheduler& scheduler;
    VideoCore::BlitHelper blit_helper;
    VideoCore::StreamBuffer copy_buffer;
    vk::MemoryBarrier2 memory_barrier{};
    std::vector<vk::ImageMemoryBarrier2> image_barriers;
    BarrierTracker barrier_tracker;
};

} // namespace Vulkan
