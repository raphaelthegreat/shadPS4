// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "video_core/renderer_vulkan/liverpool_to_vk.h"

namespace Vulkan {

using Liverpool = AmdGpu::Liverpool;

constexpr u32 MaxVertexBufferCount = 32;
constexpr u32 MaxShaderStages = 5;

struct PipelineKey {
    Liverpool::DepthControl depth;
    Liverpool::StencilControl stencil;
    Liverpool::StencilRefMask stencil_ref_front;
    Liverpool::StencilRefMask stencil_ref_back;
    Liverpool::PrimitiveType prim_type;
    Liverpool::PolygonMode polygon_mode;
    Liverpool::CullMode cull_mode;
    u32 num_attributes;
    u32 num_bindings;
    std::array<vk::VertexInputAttributeDescription, MaxVertexBufferCount> attributes;
    std::array<vk::VertexInputBindingDescription, MaxVertexBufferCount> bindings;
};
static_assert(std::has_unique_object_representations_v<PipelineKey>);

} // namespace Vulkan
