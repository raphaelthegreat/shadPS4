// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/types.h"
#include "video_core/renderer_vulkan/vk_common.h"
#include "video_core/renderer_vulkan/pipeline_key.h"

namespace Vulkan {

static constexpr u32 MaxShaderStages = 5;

class Instance;

class GraphicsPipeline {
public:
    explicit GraphicsPipeline(const Instance& instance,
                              const PipelineKey& key, vk::PipelineCache pipeline_cache,
                              vk::PipelineLayout layout);
    ~GraphicsPipeline();

    bool Build(bool fail_on_compile_required = false);

    [[nodiscard]] vk::Pipeline Handle() const noexcept {
        return *pipeline;
    }

private:
    const Instance& instance;
    vk::UniquePipeline pipeline;
    vk::PipelineLayout pipeline_layout;
    vk::PipelineCache pipeline_cache;
    PipelineKey key;
};

} // namespace Vulkan
