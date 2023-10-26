#include <fmt/core.h>
#include <SDL.h>

#include "core/emulator.h"
#include "core/hle/libraries/libpad/pad.h"
#include "core/hle/libraries/libscevideoout/video_out.h"
#include "core/input/controller.h"
#include "shad_sdl/config.h"
#include "shad_sdl/emu_window_sdl.h"

namespace Core {

Emulator::Emulator(const Config& config_) : config{config_} {
    controller = std::make_unique<Input::GameController>();
    window = std::make_unique<EmuWindowSDL>(*controller, config.getScreenWidth(), config.getScreenHeight());
    //Graphics::Vulkan::vulkanCreate(window_ctx);
}

void Emulator::run() {
    while (window->isRunning()) {
        window->pollEvents();
        Core::Libraries::videoOutFlip(100000);  // flip every 0.1 sec
    }
}

void Emulator::drawBuffer(HLE::Libs::Graphics::VideoOutVulkanImage* image) {
    window_ctx->swapchain->current_index = static_cast<u32>(-1);

    auto result = vkAcquireNextImageKHR(window_ctx->m_graphic_ctx.m_device, window_ctx->swapchain->swapchain, UINT64_MAX, nullptr,
                                        window_ctx->swapchain->present_complete_fence, &window_ctx->swapchain->current_index);

    if (result != VK_SUCCESS) {
        printf("Can't aquireNextImage\n");
        std::exit(0);
    }
    if (window_ctx->swapchain->current_index == static_cast<u32>(-1)) {
        printf("Unsupported:swapchain current index is -1\n");
        std::exit(0);
    }

    do {
        result = vkWaitForFences(window_ctx->m_graphic_ctx.m_device, 1, &window_ctx->swapchain->present_complete_fence, VK_TRUE, 100000000);
    } while (result == VK_TIMEOUT);
    if (result != VK_SUCCESS) {
        printf("vkWaitForFences is not success\n");
        std::exit(0);
    }

    vkResetFences(window_ctx->m_graphic_ctx.m_device, 1, &window_ctx->swapchain->present_complete_fence);

    auto* blt_src_image = image;
    auto* blt_dst_image = window_ctx->swapchain;

    if (blt_src_image == nullptr) {
        printf("blt_src_image is null\n");
        std::exit(0);
    }
    if (blt_dst_image == nullptr) {
        printf("blt_dst_image is null\n");
        std::exit(0);
    }

    GPU::CommandBuffer buffer(10);

    auto* vk_buffer = buffer.getPool()->buffers[buffer.getIndex()];

    buffer.begin();

    Graphics::Vulkan::vulkanBlitImage(&buffer, blt_src_image, blt_dst_image);

    VkImageMemoryBarrier pre_present_barrier{};
    pre_present_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    pre_present_barrier.pNext = nullptr;
    pre_present_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    pre_present_barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
    pre_present_barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    pre_present_barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    pre_present_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    pre_present_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    pre_present_barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    pre_present_barrier.subresourceRange.baseMipLevel = 0;
    pre_present_barrier.subresourceRange.levelCount = 1;
    pre_present_barrier.subresourceRange.baseArrayLayer = 0;
    pre_present_barrier.subresourceRange.layerCount = 1;
    pre_present_barrier.image = window_ctx->swapchain->swapchain_images[window_ctx->swapchain->current_index];
    vkCmdPipelineBarrier(vk_buffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1,
                         &pre_present_barrier);

    buffer.end();
    buffer.executeWithSemaphore();

    VkPresentInfoKHR present{};
    present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present.pNext = nullptr;
    present.swapchainCount = 1;
    present.pSwapchains = &window_ctx->swapchain->swapchain;
    present.pImageIndices = &window_ctx->swapchain->current_index;
    present.pWaitSemaphores = &buffer.getPool()->semaphores[buffer.getIndex()];
    present.waitSemaphoreCount = 1;
    present.pResults = nullptr;

    const auto& queue = window_ctx->m_graphic_ctx.queues[10];

    if (queue.mutex != nullptr) {
        printf("queue.mutexe is null\n");
        std::exit(0);
    }

    result = vkQueuePresentKHR(queue.vk_queue, &present);
    if (result != VK_SUCCESS) {
        printf("vkQueuePresentKHR failed\n");
        std::exit(0);
    }
}

} // namespace Core
