// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <memory>

#include "shader_recompiler/resource.h"
#include "shader_recompiler/runtime_info.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"

namespace Vulkan {

struct Descriptor {
    union {
        VkDescriptorBufferInfo buffer;
        VkDescriptorImageInfo image;
    };
    u32 binding;
    vk::DescriptorType type;
};
static_assert(sizeof(Descriptor) == 32);

/// Ring of descriptors referenced by recorded commands.
class DescriptorRing {
    static constexpr size_t MaxDescriptorsPerDraw =
        Shader::MaxStageTypes * (Shader::NUM_BUFFERS + Shader::NUM_IMAGES + Shader::NUM_SAMPLERS);
    static constexpr size_t NumDescriptors = 64_KB;
    static constexpr size_t NumRegions = 16;
    static constexpr size_t RegionSize = NumDescriptors / NumRegions;
    static_assert(MaxDescriptorsPerDraw <= RegionSize);

public:
    explicit DescriptorRing(Scheduler& scheduler_)
        : scheduler{scheduler_},
          descriptors{std::make_unique_for_overwrite<Descriptor[]>(NumDescriptors)} {}

    /// Returns space for the descriptors of a single draw.
    Descriptor* Acquire() {
        if (offset + MaxDescriptorsPerDraw > NumDescriptors) {
            offset = 0;
            next_region = 0;
        }
        const size_t last_region = (offset + MaxDescriptorsPerDraw - 1) / RegionSize;
        for (; next_region <= last_region; ++next_region) {
            scheduler.WaitChunk(region_chunks[next_region]);
        }
        return descriptors.get() + offset;
    }

    /// Releases count acquired descriptors once the command that references them is recorded.
    void Commit(size_t count) {
        if (count == 0) {
            return;
        }
        const u64 chunk_id = scheduler.CurrentChunkId();
        const size_t last_region = (offset + count - 1) / RegionSize;
        for (size_t region = offset / RegionSize; region <= last_region; ++region) {
            region_chunks[region] = chunk_id;
        }
        offset += count;
    }

private:
    Scheduler& scheduler;
    std::unique_ptr<Descriptor[]> descriptors;
    std::array<u64, NumRegions> region_chunks{};
    size_t next_region{};
    size_t offset{};
};

} // namespace Vulkan
