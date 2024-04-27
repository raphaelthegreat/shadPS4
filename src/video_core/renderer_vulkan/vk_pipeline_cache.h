// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <tsl/robin_map.h>
#include "shader_recompiler/module.h"
#include "shader_recompiler/shader_key.h"
#include "shader_recompiler/shader_meta.h"
#include "shader_recompiler/gcn_mod_info.h"
#include "video_core/renderer_vulkan/pipeline_key.h"
#include "video_core/renderer_vulkan/vk_common.h"

namespace AmdGpu {
struct Liverpool;
}

namespace VideoCore {
class MemoryManager;
}

namespace Vulkan {

using namespace Shader::Gcn;

class Instance;
class Scheduler;
class GraphicsPipeline;

/**
 * @brief PipelineCache class
 *
 * Stores a collection of vulkan pipelines for reuse between draw calls, manages shaders
 * produced by the shader recompiler and associated descriptor layouts.
 */
class PipelineCache {
    using ModuleMap = tsl::robin_map<Shader::Gcn::ShaderKey, Shader::Gcn::GcnModule>;

public:
    explicit PipelineCache(const Instance& instance, Scheduler& scheduler,
                           VideoCore::MemoryManager& memory_manager, AmdGpu::Liverpool* liverpool);
    ~PipelineCache();

    /// Updates necessary vertex stage parts such as buffers and attribute setup.
    void UpdateVertexStage();

    /// Updates necessary pixel stage parts such as textures and uniforms.
    void UpdatePixelStage();

    /// Binds the vulkan pipeline to emulate the guest graphics state.
    void BindPipeline();

private:
    void UpdateVertexBinding(const GcnModule& module);
    void UpdateInputLayout(const u32* vertex_table, u32 num_buffers);
    void BindResources(vk::PipelineStageFlags stage, ShaderResourceTable& table,
                       const Liverpool::UserData& user_data);
    void BindHostBuffers(bool is_indexed, const u32* vertex_table, u32 num_buffers);

private:
    const Instance& instance;
    Scheduler& scheduler;
    VideoCore::MemoryManager& memory_manager;
    AmdGpu::Liverpool* liverpool;
    vk::UniquePipelineCache pipeline_cache;
    VertexInputSemanticTable sema_table;
    std::array<GcnShaderMeta, MaxShaderStages> metas{};
    vk::UniquePipelineLayout pipeline_layout;
    std::unique_ptr<GraphicsPipeline> pipeline;
    std::vector<u32> vs_code, ps_code;
    GcnModuleInfo module_info;
    PipelineKey key{};
    ModuleMap modules;
};

} // namespace Vulkan
