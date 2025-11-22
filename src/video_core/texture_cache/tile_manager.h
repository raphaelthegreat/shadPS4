// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/types.h"
#include "video_core/amdgpu/tiling.h"
#include "video_core/buffer_cache/buffer.h"

namespace Vulkan {
class Runtime;
}

namespace VideoCore {

struct ImageInfo;
struct Image;
class StreamBuffer;

class TileManager {
    static constexpr size_t NUM_BPPS = 5;

public:
    explicit TileManager(const Vulkan::Instance& instance, Vulkan::Scheduler& scheduler,
                         Vulkan::Runtime& runtime, StreamBuffer& stream_buffer);
    ~TileManager();

    void TileImage(Image& in_image, std::span<vk::BufferImageCopy> buffer_copies,
                   Buffer& out_buffer, u32 out_offset, u32 copy_size);

    std::pair<Buffer*, u32> DetileImage(Buffer* in_buffer, u32 in_offset, const ImageInfo& info);

private:
    vk::Pipeline GetTilingPipeline(const ImageInfo& info, bool is_tiler);

private:
    const Vulkan::Instance& instance;
    Vulkan::Scheduler& scheduler;
    Vulkan::Runtime& runtime;
    StreamBuffer& stream_buffer;
    vk::UniqueDescriptorSetLayout desc_layout;
    vk::UniquePipelineLayout pl_layout;
    std::array<vk::UniquePipeline, AmdGpu::NUM_TILE_MODES * NUM_BPPS> detilers{};
    std::array<vk::UniquePipeline, AmdGpu::NUM_TILE_MODES * NUM_BPPS> tilers{};
};

} // namespace VideoCore
