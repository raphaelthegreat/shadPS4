#include "renderer_vulkan.h"

namespace Vulkan {

bool CanBlitToSwapchain(const vk::PhysicalDevice physical_device, vk::Format format) {
    const vk::FormatProperties props{physical_device.getFormatProperties(format)};
    return static_cast<bool>(props.optimalTilingFeatures & vk::FormatFeatureFlagBits::eBlitDst);
}

[[nodiscard]] vk::ImageSubresourceLayers MakeImageSubresourceLayers() {
    return vk::ImageSubresourceLayers{
        .aspectMask = vk::ImageAspectFlagBits::eColor,
        .mipLevel = 0,
        .baseArrayLayer = 0,
        .layerCount = 1,
    };
}

[[nodiscard]] vk::ImageBlit MakeImageBlit(s32 frame_width, s32 frame_height, s32 swapchain_width,
                                          s32 swapchain_height) {
    return vk::ImageBlit{
        .srcSubresource = MakeImageSubresourceLayers(),
        .srcOffsets =
            std::array{
                vk::Offset3D{
                    .x = 0,
                    .y = 0,
                    .z = 0,
                },
                vk::Offset3D{
                    .x = frame_width,
                    .y = frame_height,
                    .z = 1,
                },
            },
        .dstSubresource = MakeImageSubresourceLayers(),
        .dstOffsets =
            std::array{
                vk::Offset3D{
                    .x = 0,
                    .y = 0,
                    .z = 0,
                },
                vk::Offset3D{
                    .x = swapchain_width,
                    .y = swapchain_height,
                    .z = 1,
                },
            },
    };
}

RendererVulkan::RendererVulkan(Frontend::WindowSDL& window)
    : instance{window, 1}, scheduler{instance}, swapchain{instance, window}, texture_cache{
                                                                                 instance,
                                                                                 scheduler} {
    const u32 num_images = swapchain.GetImageCount();
    const vk::Device device = instance.GetDevice();

    const vk::CommandPoolCreateInfo pool_info = {
        .flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer |
                 vk::CommandPoolCreateFlagBits::eTransient,
        .queueFamilyIndex = instance.GetGraphicsQueueFamilyIndex(),
    };
    command_pool = device.createCommandPoolUnique(pool_info);

    const vk::CommandBufferAllocateInfo alloc_info = {
        .commandPool = *command_pool,
        .level = vk::CommandBufferLevel::ePrimary,
        .commandBufferCount = num_images,
    };
    cmdbuffers = device.allocateCommandBuffers(alloc_info);

    fences.resize(num_images);
    for (auto& fence : fences) {
        fence = device.createFence({
            .flags = vk::FenceCreateFlagBits::eSignaled,
        });
    }
}

RendererVulkan::~RendererVulkan() = default;

void RendererVulkan::Present(const Core::Libraries::VideoOut::BufferAttributeGroup& attribute,
                             VAddr cpu_address) {
    auto& image = texture_cache.FindDisplayBuffer(attribute, cpu_address);

    scheduler.WaitWorker();
    scheduler.Flush();

    const vk::Device device = instance.GetDevice();
    const u32 frame_index = swapchain.GetFrameIndex();
    void(device.waitForFences(fences[frame_index], false, std::numeric_limits<u64>::max()));
    device.resetFences(fences[frame_index]);

    swapchain.AcquireNextImage();

    const vk::Image swapchain_image = swapchain.Image();

    const vk::CommandBufferBeginInfo begin_info = {
        .flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit,
    };
    const vk::CommandBuffer cmdbuf = cmdbuffers[frame_index];
    cmdbuf.begin(begin_info);

    const vk::Extent2D extent = swapchain.GetExtent();
    const std::array pre_barriers{
        vk::ImageMemoryBarrier{
            .srcAccessMask = vk::AccessFlagBits::eNone,
            .dstAccessMask = vk::AccessFlagBits::eTransferWrite,
            .oldLayout = vk::ImageLayout::eUndefined,
            .newLayout = vk::ImageLayout::eTransferDstOptimal,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = swapchain_image,
            .subresourceRange{
                .aspectMask = vk::ImageAspectFlagBits::eColor,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = VK_REMAINING_ARRAY_LAYERS,
            },
        },
        vk::ImageMemoryBarrier{
            .srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite,
            .dstAccessMask = vk::AccessFlagBits::eTransferRead,
            .oldLayout = vk::ImageLayout::eGeneral,
            .newLayout = vk::ImageLayout::eGeneral,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = image.image,
            .subresourceRange{
                .aspectMask = vk::ImageAspectFlagBits::eColor,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = VK_REMAINING_ARRAY_LAYERS,
            },
        },
    };
    const vk::ImageMemoryBarrier post_barrier{
        .srcAccessMask = vk::AccessFlagBits::eTransferWrite,
        .dstAccessMask = vk::AccessFlagBits::eMemoryRead,
        .oldLayout = vk::ImageLayout::eTransferDstOptimal,
        .newLayout = vk::ImageLayout::ePresentSrcKHR,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = swapchain_image,
        .subresourceRange{
            .aspectMask = vk::ImageAspectFlagBits::eColor,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = VK_REMAINING_ARRAY_LAYERS,
        },
    };

    cmdbuf.pipelineBarrier(vk::PipelineStageFlagBits::eColorAttachmentOutput,
                           vk::PipelineStageFlagBits::eTransfer, vk::DependencyFlagBits::eByRegion,
                           {}, {}, pre_barriers);

    printf("Blitting (0, 0) -> (%d, %d)\n", image.info.size.width, image.info.size.height);
    cmdbuf.blitImage(
        image.image, vk::ImageLayout::eGeneral, swapchain_image,
        vk::ImageLayout::eTransferDstOptimal,
        MakeImageBlit(image.info.size.width, image.info.size.height, extent.width, extent.height),
        vk::Filter::eLinear);

    cmdbuf.pipelineBarrier(vk::PipelineStageFlagBits::eAllCommands,
                           vk::PipelineStageFlagBits::eAllCommands,
                           vk::DependencyFlagBits::eByRegion, {}, {}, post_barrier);

    cmdbuf.end();

    static constexpr std::array<vk::PipelineStageFlags, 2> wait_stage_masks = {
        vk::PipelineStageFlagBits::eColorAttachmentOutput,
        vk::PipelineStageFlagBits::eAllGraphics,
    };

    const vk::Semaphore present_ready = swapchain.GetPresentReadySemaphore();
    const vk::Semaphore image_acquired = swapchain.GetImageAcquiredSemaphore();
    const std::array wait_semaphores = {image_acquired};

    vk::SubmitInfo submit_info = {
        .waitSemaphoreCount = static_cast<u32>(wait_semaphores.size()),
        .pWaitSemaphores = wait_semaphores.data(),
        .pWaitDstStageMask = wait_stage_masks.data(),
        .commandBufferCount = 1u,
        .pCommandBuffers = &cmdbuf,
        .signalSemaphoreCount = 1,
        .pSignalSemaphores = &present_ready,
    };

    std::scoped_lock submit_lock{scheduler.submit_mutex};

    try {
        instance.GetGraphicsQueue().submit(submit_info, fences[frame_index]);
    } catch (vk::DeviceLostError& err) {
        LOG_CRITICAL(Render_Vulkan, "Device lost during present submit: {}", err.what());
        UNREACHABLE();
    }

    swapchain.Present();
}

} // namespace Vulkan
