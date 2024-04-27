// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/types.h"
#include "video_core/renderer_vulkan/pipeline_key.h"
#include "video_core/renderer_vulkan/vk_common.h"

namespace Vulkan {

class Instance;

class GraphicsPipeline {
public:
    explicit GraphicsPipeline(const Instance& instance, const PipelineKey& key,
                              vk::PipelineCache pipeline_cache, vk::PipelineLayout layout,
                              std::span<const u32> vs_code, std::span<const u32> fs_code);
    ~GraphicsPipeline();

    [[nodiscard]] vk::Pipeline Handle() const noexcept {
        return *pipeline;
    }

private:
    const Instance& instance;
    vk::UniquePipeline pipeline;
    vk::PipelineLayout pipeline_layout;
    PipelineKey key;
};

} // namespace Vulkan
