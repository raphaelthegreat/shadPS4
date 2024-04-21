// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "video_core/amdgpu/liverpool.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_rasterizer.h"
#include "video_core/renderer_vulkan/vk_graphics_pipeline.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/texture_cache/texture_cache.h"
#include "video_core/texture_cache/image_view.h"

namespace Vulkan {

static constexpr vk::BufferUsageFlags VertexIndexFlags =
    vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eIndexBuffer |
    vk::BufferUsageFlagBits::eTransferDst;

Rasterizer::Rasterizer(const Instance& instance_, Scheduler& scheduler_,
                       VideoCore::TextureCache& texture_cache_, AmdGpu::Liverpool* liverpool_)
    : instance{instance_}, scheduler{scheduler_}, texture_cache{texture_cache_}, liverpool{liverpool_},
      vertex_index_buffer{instance, scheduler, VertexIndexFlags, 64_MB} {
    liverpool->BindRasterizer(this);
    const vk::PipelineLayoutCreateInfo layout_info = {
        .setLayoutCount = 0U,
        .pSetLayouts = nullptr,
        .pushConstantRangeCount = 0,
        .pPushConstantRanges = nullptr,
    };
    auto layout = instance.GetDevice().createPipelineLayout(layout_info);

    PipelineKey key{};
    key.cull_mode = Liverpool::CullMode::None;
    key.polygon_mode = Liverpool::PolygonMode::Fill;
    key.prim_type = Liverpool::PrimitiveType::TriangleList;
    pipeline = std::make_unique<GraphicsPipeline>(instance, key, VK_NULL_HANDLE, layout);
}

Rasterizer::~Rasterizer() = default;

void Rasterizer::DrawIndex() {
    const auto cmdbuf = scheduler.CommandBuffer();
    auto& regs = liverpool->regs;

    static bool first_time = true;
    if (first_time) {
        first_time = false;
        return;
    }

    UpdateDynamicState();

    const u32 pitch = regs.color_buffers[0].Pitch();
    const u32 height = regs.color_buffers[0].Height();
    const u32 tile_max = regs.color_buffers[0].slice.tile_max;
    auto& image_view = texture_cache.RenderTarget(regs.color_buffers[0].Address(), pitch);

    const vk::RenderingAttachmentInfo color_info = {
        .imageView = *image_view.image_view,
        .imageLayout = vk::ImageLayout::eGeneral,
        .loadOp = vk::AttachmentLoadOp::eLoad,
        .storeOp = vk::AttachmentStoreOp::eStore,
    };

    // TODO: Don't restart renderpass every draw
    const vk::RenderingInfo rendering_info = {
        .renderArea = {.offset = {0, 0}, .extent = {1920, 1080}},
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &color_info,
    };

    cmdbuf.beginRendering(rendering_info);
    cmdbuf.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline->Handle());
    cmdbuf.bindIndexBuffer(vertex_index_buffer.Handle(), 0, vk::IndexType::eUint32);
    cmdbuf.bindVertexBuffers(0, vertex_index_buffer.Handle(), vk::DeviceSize(0));
    //cmdbuf.drawIndexed(regs.num_indices, regs.num_instances.NumInstances(), 0, 0, 0);
    cmdbuf.draw(regs.num_indices, regs.num_instances.NumInstances(), 0, 0);
    cmdbuf.endRendering();
}

void Rasterizer::UpdateDynamicState() {
    UpdateViewportScissorState();
}

void Rasterizer::UpdateViewportScissorState() {
    auto& regs = liverpool->regs;

    const auto cmdbuf = scheduler.CommandBuffer();
    const vk::Viewport viewport{
        .x = regs.viewports[0].xoffset - regs.viewports[0].xscale,
        .y = regs.viewports[0].yoffset - regs.viewports[0].yscale,
        .width = regs.viewports[0].xscale * 2.0f,
        .height = regs.viewports[0].yscale * 2.0f,
        .minDepth = regs.viewports[0].zoffset - regs.viewports[0].zscale,
        .maxDepth = regs.viewports[0].zscale + regs.viewports[0].zoffset,
    };
    const vk::Rect2D scissor{
        .offset = {regs.screen_scissor.top_left_x, regs.screen_scissor.top_left_y},
        .extent = {regs.screen_scissor.GetWidth(), regs.screen_scissor.GetHeight()},
    };
    cmdbuf.setViewport(0, viewport);
    cmdbuf.setScissor(0, scissor);
}

void Rasterizer::UpdateDepthStencilState() {
    auto& depth = liverpool->regs.depth_control;

    const auto cmdbuf = scheduler.CommandBuffer();
    cmdbuf.setDepthBoundsTestEnable(depth.depth_bounds_enable);
}

} // namespace Vulkan
