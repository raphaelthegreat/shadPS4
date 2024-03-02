// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/renderer_vulkan/vk_swapchain.h"
#include "video_core/texture_cache/texture_cache.h"

namespace Frontend {
class WindowSDL;
}

namespace Vulkan {

class RendererVulkan {
public:
    explicit RendererVulkan(Frontend::WindowSDL& window);
    ~RendererVulkan();

    void Present(const Core::Libraries::VideoOut::BufferAttributeGroup& attribute,
                 VAddr cpu_address);

private:
    Instance instance;
    Scheduler scheduler;
    Swapchain swapchain;
    VideoCore::TextureCache texture_cache;
    vk::UniqueCommandPool command_pool;
    std::vector<vk::CommandBuffer> cmdbuffers;
    std::vector<vk::Fence> fences;
};

} // namespace Vulkan
