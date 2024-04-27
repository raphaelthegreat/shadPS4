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
#version 450
#ifdef GL_ARB_shader_draw_parameters
#extension GL_ARB_shader_draw_parameters : enable
#endif

#if defined(GL_KHR_shader_subgroup_ballot)
#extension GL_KHR_shader_subgroup_ballot : require
#elif defined(GL_NV_shader_thread_group)
#extension GL_NV_shader_thread_group : require
#elif defined(GL_ARB_shader_ballot) && defined(GL_ARB_shader_int64)
#extension GL_ARB_shader_int64 : enable
#extension GL_ARB_shader_ballot : require
#else
#error No extensions available to emulate requested subgroup feature.
#endif

layout(location = 0) in vec4 i0;
layout(location = 1) in vec4 i1;
layout(location = 0) out vec4 o0;
#ifdef GL_ARB_shader_draw_parameters
#define SPIRV_Cross_BaseVertex gl_BaseVertexARB
#else
uniform int SPIRV_Cross_BaseVertex;
#endif
float s[1];
float v[12];
uint m0;
bool scc;
uint vcc_lo;
uint vcc_hi;
bool vcc_z;
uint exec_lo;
uint exec_hi;
bool exec_z;

#if defined(GL_KHR_shader_subgroup_ballot)
#elif defined(GL_NV_shader_thread_group)
uvec4 subgroupBallot(bool v) { return uvec4(ballotThreadNV(v), 0u, 0u, 0u); }
#elif defined(GL_ARB_shader_ballot)
uvec4 subgroupBallot(bool v) { return uvec4(unpackUint2x32(ballotARB(v)), 0u, 0u); }
#endif

void vs_fetch()
{
    v[4u] = i0.x;
    v[5u] = i0.y;
    v[6u] = i0.z;
    v[7u] = i0.w;
    v[8u] = i1.x;
    v[9u] = i1.y;
    v[10u] = i1.z;
    v[11u] = i1.w;
}

void vs_main()
{
    vcc_hi = 8u;
    vcc_z = (vcc_lo == 0u) && (vcc_hi == 0u);
    gl_Position = vec4(v[4u], v[5u], v[6u], v[7u]);
    o0 = vec4(v[8u], v[9u], v[10u], v[11u]);
}

void main()
{
    uvec4 _95 = subgroupBallot(true);
    exec_lo = _95.x;
    exec_hi = _95.y;
    exec_z = (exec_lo == 0u) && (exec_hi == 0u);
    vcc_lo = 0u;
    vcc_hi = 0u;
    vcc_z = (vcc_lo == 0u) && (vcc_hi == 0u);
    v[0u] = uintBitsToFloat(uint(gl_VertexIndex) - uint(SPIRV_Cross_BaseVertex));
    vs_fetch();
    vs_main();
}
)";

static constexpr std::string_view PsShader = R"(
#version 450

#if defined(GL_KHR_shader_subgroup_ballot)
#extension GL_KHR_shader_subgroup_ballot : require
#elif defined(GL_NV_shader_thread_group)
#extension GL_NV_shader_thread_group : require
#elif defined(GL_ARB_shader_ballot) && defined(GL_ARB_shader_int64)
#extension GL_ARB_shader_int64 : enable
#extension GL_ARB_shader_ballot : require
#else
#error No extensions available to emulate requested subgroup feature.
#endif

#if defined(GL_KHR_shader_subgroup_ballot)
#extension GL_KHR_shader_subgroup_ballot : require
#elif defined(GL_NV_shader_thread_group)
#extension GL_NV_shader_thread_group : require
#elif defined(GL_ARB_shader_ballot) && defined(GL_ARB_shader_int64)
#extension GL_ARB_shader_int64 : enable
#extension GL_ARB_shader_ballot : require
#else
#error No extensions available to emulate requested subgroup feature.
#endif

layout(location = 0, index = 0) out vec4 o0;
layout(location = 0) in vec4 i0;
float s[1];
float v[4];
uint m0;
bool scc;
uint vcc_lo;
uint vcc_hi;
bool vcc_z;
uint exec_lo;
uint exec_hi;
bool exec_z;

#if defined(GL_KHR_shader_subgroup_ballot)
#elif defined(GL_NV_shader_thread_group)
#define gl_SubgroupEqMask uvec4(gl_ThreadEqMaskNV, 0u, 0u, 0u)
#define gl_SubgroupGeMask uvec4(gl_ThreadGeMaskNV, 0u, 0u, 0u)
#define gl_SubgroupGtMask uvec4(gl_ThreadGtMaskNV, 0u, 0u, 0u)
#define gl_SubgroupLeMask uvec4(gl_ThreadLeMaskNV, 0u, 0u, 0u)
#define gl_SubgroupLtMask uvec4(gl_ThreadLtMaskNV, 0u, 0u, 0u)
#elif defined(GL_ARB_shader_ballot)
#define gl_SubgroupEqMask uvec4(unpackUint2x32(gl_SubGroupEqMaskARB), 0u, 0u)
#define gl_SubgroupGeMask uvec4(unpackUint2x32(gl_SubGroupGeMaskARB), 0u, 0u)
#define gl_SubgroupGtMask uvec4(unpackUint2x32(gl_SubGroupGtMaskARB), 0u, 0u)
#define gl_SubgroupLeMask uvec4(unpackUint2x32(gl_SubGroupLeMaskARB), 0u, 0u)
#define gl_SubgroupLtMask uvec4(unpackUint2x32(gl_SubGroupLtMaskARB), 0u, 0u)
#endif

#if defined(GL_KHR_shader_subgroup_ballot)
#elif defined(GL_NV_shader_thread_group)
uvec4 subgroupBallot(bool v) { return uvec4(ballotThreadNV(v), 0u, 0u, 0u); }
#elif defined(GL_ARB_shader_ballot)
uvec4 subgroupBallot(bool v) { return uvec4(unpackUint2x32(ballotARB(v)), 0u, 0u); }
#endif

void ps_main()
{
    vcc_hi = 8u;
    vcc_z = (vcc_lo == 0u) && (vcc_hi == 0u);
    m0 = floatBitsToUint(s[0u]);
    if (((exec_lo & gl_SubgroupEqMask.xy.x) != 0u) || ((exec_hi & gl_SubgroupEqMask.xy.y) != 0u))
    {
        v[3u] = i0.x;
        v[2u] = i0.y;
        v[2u] = uintBitsToFloat(packHalf2x16(vec2(v[3u], v[2u])));
        v[3u] = i0.z;
        v[0u] = i0.w;
        v[0u] = uintBitsToFloat(packHalf2x16(vec2(v[3u], v[0u])));
    }
    o0 = vec4(unpackHalf2x16(floatBitsToUint(v[2u])), unpackHalf2x16(floatBitsToUint(v[0u])));
}

void main()
{
    uvec4 _111 = subgroupBallot(true);
    exec_lo = _111.x;
    exec_hi = _111.y;
    exec_z = (exec_lo == 0u) && (exec_hi == 0u);
    vcc_lo = 0u;
    vcc_hi = 0u;
    vcc_z = (vcc_lo == 0u) && (vcc_hi == 0u);
    ps_main();
}
)";

GraphicsPipeline::GraphicsPipeline(const Instance& instance_, const PipelineKey& key_,
                                   vk::PipelineCache pipeline_cache, vk::PipelineLayout layout_,
                                   std::span<const u32> vs_code, std::span<const u32> fs_code)
    : instance{instance_}, pipeline_layout{layout_}, key{key_} {
    const vk::Device device = instance.GetDevice();

    const vk::PipelineVertexInputStateCreateInfo vertex_input_info = {
        .vertexBindingDescriptionCount = key.num_bindings,
        .pVertexBindingDescriptions = key.bindings.data(),
        .vertexAttributeDescriptionCount = key.num_attributes,
        .pVertexAttributeDescriptions = key.attributes.data(),
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

    const vk::ShaderModuleCreateInfo vs_info = {
        .codeSize = vs_code.size() * sizeof(u32),
        .pCode = vs_code.data(),
    };
    auto vs_module = device.createShaderModule(vs_info);
    ASSERT(vs_module != VK_NULL_HANDLE);
    const vk::ShaderModuleCreateInfo fs_info = {
        .codeSize = fs_code.size() * sizeof(u32),
        .pCode = fs_code.data(),
    };
    auto fs_module = device.createShaderModule(fs_info);
    ASSERT(fs_module != VK_NULL_HANDLE);

    u32 shader_count = 2;
    std::array<vk::PipelineShaderStageCreateInfo, MaxShaderStages> shader_stages;
    shader_stages[0] = vk::PipelineShaderStageCreateInfo{
        .stage = vk::ShaderStageFlagBits::eVertex,
        .module = vs_module,
        .pName = "main",
    };
    shader_stages[1] = vk::PipelineShaderStageCreateInfo{
        .stage = vk::ShaderStageFlagBits::eFragment,
        .module = fs_module,
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
    } else {
        UNREACHABLE_MSG("Graphics pipeline creation failed!");
    }
}

GraphicsPipeline::~GraphicsPipeline() = default;

} // namespace Vulkan
