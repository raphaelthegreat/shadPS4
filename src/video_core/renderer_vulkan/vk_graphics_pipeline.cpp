// Copyright 2023 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <boost/container/static_vector.hpp>

#include "common/assert.h"
#include "video_core/renderer_vulkan/vk_graphics_pipeline.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_shader_util.h"

namespace Vulkan {

static constexpr std::string_view VsShader = R"(
#version 450 core

const vec2 positions[3] = vec2[3](
    vec2(-0.5, -0.4),
    vec2(0, 0.6),
    vec2(0.5, -0.4)
);

void main() {
    const vec2 pos = positions[gl_VertexIndex];
    gl_Position = vec4(pos.x, pos.y, 0.f, 1.f);
}
)";

static constexpr std::string_view PsShader = R"(
#version 450 core

layout (location = 0) out vec4 color;

void main() {
    color = vec4(0.3, 0.9, 0.9, 1.0);
}
)";

GraphicsPipeline::GraphicsPipeline(const Instance& instance_, const PipelineKey& key_,
                                   vk::PipelineCache pipeline_cache_, vk::PipelineLayout layout_)
    : instance{instance_}, pipeline_layout{layout_}, pipeline_cache{pipeline_cache_}, key{key_} {
    Build();
}

GraphicsPipeline::~GraphicsPipeline() = default;

bool GraphicsPipeline::Build(bool fail_on_compile_required) {
    const vk::Device device = instance.GetDevice();

    const vk::PipelineVertexInputStateCreateInfo vertex_input_info = {
        .vertexBindingDescriptionCount = 0U,
        .pVertexBindingDescriptions = nullptr,
        .vertexAttributeDescriptionCount = 0U,
        .pVertexAttributeDescriptions = nullptr,
    };

    const vk::PipelineInputAssemblyStateCreateInfo input_assembly = {
        .topology = LiverpoolToVK::PrimitiveType(key.prim_type),
        .primitiveRestartEnable = false,
    };

    const vk::PipelineRasterizationStateCreateInfo raster_state = {
        .depthClampEnable = false,
        .rasterizerDiscardEnable = false,
        .polygonMode = LiverpoolToVK::PolygonMode(key.polygon_mode),
        .cullMode = LiverpoolToVK::CullMode(key.cull_mode),
        .frontFace = vk::FrontFace::eClockwise,
        .depthBiasEnable = false,
        .lineWidth = 1.0f,
    };

    const vk::PipelineMultisampleStateCreateInfo multisampling = {
        .rasterizationSamples = vk::SampleCountFlagBits::e1,
        .sampleShadingEnable = false,
    };

    const vk::PipelineColorBlendAttachmentState colorblend_attachment = {
        .blendEnable = false,
        .colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                          vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA,
    };

    const vk::PipelineColorBlendStateCreateInfo color_blending = {
        .logicOpEnable = false,
        .logicOp = vk::LogicOp::eCopy,
        .attachmentCount = 1,
        .pAttachments = &colorblend_attachment,
        .blendConstants = std::array{1.0f, 1.0f, 1.0f, 1.0f},
    };

    const vk::Viewport viewport = {
        .x = 0.0f,
        .y = 0.0f,
        .width = 1.0f,
        .height = 1.0f,
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };

    const vk::Rect2D scissor = {
        .offset = {0, 0},
        .extent = {1, 1},
    };

    const vk::PipelineViewportStateCreateInfo viewport_info = {
        .viewportCount = 1,
        .pViewports = &viewport,
        .scissorCount = 1,
        .pScissors = &scissor,
    };

    boost::container::static_vector<vk::DynamicState, 14> dynamic_states = {
        vk::DynamicState::eViewport,
        vk::DynamicState::eScissor,
    };

    const vk::PipelineDynamicStateCreateInfo dynamic_info = {
        .dynamicStateCount = static_cast<u32>(dynamic_states.size()),
        .pDynamicStates = dynamic_states.data(),
    };

    const vk::PipelineDepthStencilStateCreateInfo depth_info = {
        .depthTestEnable = key.depth.depth_enable,
        .depthWriteEnable = key.depth.depth_write_enable,
        .depthCompareOp = LiverpoolToVK::CompareOp(key.depth.depth_func),
        .depthBoundsTestEnable = key.depth.depth_bounds_enable,
        .stencilTestEnable = key.depth.stencil_enable,
        .front{
            .failOp = LiverpoolToVK::StencilOp(key.stencil.stencil_fail_front),
            .passOp = LiverpoolToVK::StencilOp(key.stencil.stencil_zpass_front),
            .depthFailOp = LiverpoolToVK::StencilOp(key.stencil.stencil_zfail_front),
            .compareOp = LiverpoolToVK::CompareOp(key.depth.stencil_ref_func),
            .compareMask = key.stencil_ref_front.stencil_mask,
            .writeMask = key.stencil_ref_front.stencil_write_mask,
            .reference = key.stencil_ref_front.stencil_test_val,
        },
        .back{
            .failOp = LiverpoolToVK::StencilOp(key.stencil.stencil_fail_back),
            .passOp = LiverpoolToVK::StencilOp(key.stencil.stencil_zpass_back),
            .depthFailOp = LiverpoolToVK::StencilOp(key.stencil.stencil_zfail_back),
            .compareOp = LiverpoolToVK::CompareOp(key.depth.stencil_bf_func),
            .compareMask = key.stencil_ref_back.stencil_mask,
            .writeMask = key.stencil_ref_back.stencil_write_mask,
            .reference = key.stencil_ref_back.stencil_test_val,
        },
    };

    u32 shader_count = 2;
    std::array<vk::PipelineShaderStageCreateInfo, MaxShaderStages> shader_stages;
    shader_stages[0] = vk::PipelineShaderStageCreateInfo{
        .stage = vk::ShaderStageFlagBits::eVertex,
        .module = Compile(VsShader, vk::ShaderStageFlagBits::eVertex, device),
        .pName = "main",
    };
    shader_stages[1] = vk::PipelineShaderStageCreateInfo{
        .stage = vk::ShaderStageFlagBits::eFragment,
        .module = Compile(PsShader, vk::ShaderStageFlagBits::eFragment, device),
        .pName = "main",
    };

    const vk::Format color_format = vk::Format::eB8G8R8A8Srgb;
    const vk::PipelineRenderingCreateInfoKHR pipeline_rendering_ci = {
        .colorAttachmentCount = 1,
        .pColorAttachmentFormats = &color_format,
        .depthAttachmentFormat = vk::Format::eUndefined,
        .stencilAttachmentFormat = vk::Format::eUndefined,
    };

    const vk::GraphicsPipelineCreateInfo pipeline_info = {
        .pNext = &pipeline_rendering_ci,
        .stageCount = shader_count,
        .pStages = shader_stages.data(),
        .pVertexInputState = &vertex_input_info,
        .pInputAssemblyState = &input_assembly,
        .pViewportState = &viewport_info,
        .pRasterizationState = &raster_state,
        .pMultisampleState = &multisampling,
        .pDepthStencilState = &depth_info,
        .pColorBlendState = &color_blending,
        .pDynamicState = &dynamic_info,
        .layout = pipeline_layout,
    };

    auto result = device.createGraphicsPipelineUnique(pipeline_cache, pipeline_info);
    if (result.result == vk::Result::eSuccess) {
        pipeline = std::move(result.value);
    } else if (result.result == vk::Result::eErrorPipelineCompileRequiredEXT) {
        return false;
    } else {
        UNREACHABLE_MSG("Graphics pipeline creation failed!");
    }

    return true;
}

} // namespace Vulkan
