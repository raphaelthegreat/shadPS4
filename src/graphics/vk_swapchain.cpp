#include <fmt/core.h>
#include "graphics/vk_swapchain.h"
#include "graphics/vk_platform.h"
#include "graphics/vk_instance.h"

namespace Vulkan {

Swapchain::Swapchain(const Core::Frontend::EmuWindow& window_, const Instance& instance_,
                     Scheduler& scheduler_, u32 width, u32 height)
    : window(window_), instance(instance_), scheduler(scheduler_) {
    surface = createSurface(instance.getInstance(), window);
    findPresentFormat();
    queryPresentMode();
    create(width, height);
}

Swapchain::~Swapchain() {
    destroy();
}

void Swapchain::create(u32 width_, u32 height_) {
    width = width_;
    height = height_;
    needsRecreation = false;

    destroy();

    queryPresentMode();
    setSurfaceProperties();

    const u32 familyIndex = instance.getQueueFamilyIndex();
    const vk::SwapchainCreateInfoKHR swapchainInfo = {
        .surface = surface,
        .minImageCount = imageCount,
        .imageFormat = surfaceFormat.format,
        .imageColorSpace = surfaceFormat.colorSpace,
        .imageExtent = extent,
        .imageArrayLayers = 1,
        .imageUsage = vk::ImageUsageFlagBits::eColorAttachment,
        .imageSharingMode = vk::SharingMode::eExclusive,
        .queueFamilyIndexCount = 1u,
        .pQueueFamilyIndices = &familyIndex,
        .preTransform = transform,
        .compositeAlpha = compositeAlpha,
        .presentMode = presentMode,
        .clipped = true,
        .oldSwapchain = nullptr,
    };

    auto createResult = instance.getDevice().createSwapchainKHRUnique(swapchainInfo);
    if (createResult.result == vk::Result::eSuccess) {
        swapchain = std::move(createResult.value);
    } else {
        fmt::print("Swapchain creation failed: {}\n", vk::to_string(createResult.result));
        return;
    }

    setupImages();
    refreshSemaphores();
}

bool Swapchain::acquireNextImage() {
    auto device = instance.getDevice();
    const auto semaphore = getImageAcquiredSemaphore();
    const vk::Result result =
        device.acquireNextImageKHR(*swapchain, std::numeric_limits<u64>::max(),
                                   semaphore, VK_NULL_HANDLE, &imageIndex);
    switch (result) {
    case vk::Result::eSuccess:
        break;
    case vk::Result::eSuboptimalKHR:
    case vk::Result::eErrorSurfaceLostKHR:
    case vk::Result::eErrorOutOfDateKHR:
        needsRecreation = true;
        break;
    default:
        fmt::print("Swapchain acquire returned unknown result {}\n", vk::to_string(result));
        break;
    }

    return !needsRecreation;
}

void Swapchain::present() {
    if (needsRecreation) [[unlikely]] {
        return;
    }

    const auto vk_swapchain = *swapchain;
    const auto waitSemaphore = getPresentReadySemaphore();
    const vk::PresentInfoKHR presentInfo = {
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &waitSemaphore,
        .swapchainCount = 1,
        .pSwapchains = &vk_swapchain,
        .pImageIndices = &imageIndex,
    };

    const vk::Queue queue = instance.getQueue();
    const vk::Result result = queue.presentKHR(presentInfo);

    switch (result) {
    case vk::Result::eSuccess:
        break;
    case vk::Result::eErrorOutOfDateKHR:
    case vk::Result::eErrorSurfaceLostKHR:
        needsRecreation = true;
        break;
    default:
        fmt::print("Swapchain presentation failed {}\n", vk::to_string(result));
        break;
    }

    // Advance frame index
    frameIndex = (frameIndex + 1) % imageCount;

    // Wait for next frame to be ready.
    const auto device = instance.getDevice();
    const auto fence = *fences[frameIndex];
    void(device.waitForFences(fence, false, std::numeric_limits<u64>::max()));
    device.resetFences(fence);
}

void Swapchain::findPresentFormat() {
    const auto [result, formats] = instance.getPhysicalDevice().getSurfaceFormatsKHR(surface);

    // If there is a single undefined surface format, the device doesn't care, so we'll just use RGBA.
    if (formats[0].format == vk::Format::eUndefined) {
        surfaceFormat.format = vk::Format::eR8G8B8A8Unorm;
        surfaceFormat.colorSpace = vk::ColorSpaceKHR::eSrgbNonlinear;
    } else {
        // Try to find a suitable format.
        for (const vk::SurfaceFormatKHR& sformat : formats) {
            const vk::Format format = sformat.format;
            if (format != vk::Format::eR8G8B8A8Unorm && format != vk::Format::eB8G8R8A8Unorm) {
                continue;
            }

            surfaceFormat.format = format;
            surfaceFormat.colorSpace = sformat.colorSpace;
            break;
        }
    }

    if (surfaceFormat.format == vk::Format::eUndefined) {
        fmt::print("Unable to find required swapchain format\n");
        return;
    }
}

void Swapchain::queryPresentMode() {
    const auto [result, presentModes] = instance.getPhysicalDevice().getSurfacePresentModesKHR(surface);
    if (result != vk::Result::eSuccess) [[unlikely]] {
        fmt::print("Error enumerating surface present modes: {}\n", vk::to_string(result));
        return;
    }

    // Fifo support is required by all vulkan implementations, waits for vsync
    presentMode = vk::PresentModeKHR::eFifo;

    // Use mailbox if available, lowest-latency vsync-enabled mode
    if (std::find(presentModes.begin(), presentModes.end(), vk::PresentModeKHR::eMailbox) != presentModes.end()) {
        presentMode = vk::PresentModeKHR::eMailbox;
    }
}

void Swapchain::setSurfaceProperties() {
    const auto [result, capabilities] = instance.getPhysicalDevice().getSurfaceCapabilitiesKHR(surface);
    if (result != vk::Result::eSuccess) [[unlikely]] {
        fmt::print("Unable to query surface capabilities\n");
        return;
    }

    extent = capabilities.currentExtent;
    if (capabilities.currentExtent.width == std::numeric_limits<u32>::max()) {
        extent.width = std::max(capabilities.minImageExtent.width,
                                std::min(capabilities.maxImageExtent.width, width));
        extent.height = std::max(capabilities.minImageExtent.height,
                                 std::min(capabilities.maxImageExtent.height, height));
    }

    // Select number of images in swap chain, we prefer one buffer in the background to work on
    imageCount = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount > 0) {
        imageCount = std::min(imageCount, capabilities.maxImageCount);
    }

    // Prefer identity transform if possible
    transform = vk::SurfaceTransformFlagBitsKHR::eIdentity;
    if (!(capabilities.supportedTransforms & transform)) {
        transform = capabilities.currentTransform;
    }

    // Opaque is not supported everywhere.
    compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque;
    if (!(capabilities.supportedCompositeAlpha & vk::CompositeAlphaFlagBitsKHR::eOpaque)) {
        compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eInherit;
    }
}

void Swapchain::destroy() {
    swapchain.reset();
    imageAcquired.clear();
    presentReady.clear();
    fences.clear();
}

void Swapchain::refreshSemaphores() {
    const auto device = instance.getDevice();
    imageAcquired.resize(imageCount);
    presentReady.resize(imageCount);
    fences.resize(imageCount);
    for (u32 i = 0; i < imageCount; ++i) {
        const vk::SemaphoreCreateInfo semaphoreInfo{};
        imageAcquired[i] = device.createSemaphoreUnique(semaphoreInfo).value;
        presentReady[i] = device.createSemaphoreUnique(semaphoreInfo).value;
        fences[i] = device.createFenceUnique({.flags = vk::FenceCreateFlagBits::eSignaled}).value;
    }
}

void Swapchain::setupImages() {
    const auto device = instance.getDevice();

    vk::Result result;
    std::tie(result, images) = device.getSwapchainImagesKHR(*swapchain);
    if (result != vk::Result::eSuccess) {
        fmt::print("Unable to setup swapchain images\n");
    }

    imageCount = static_cast<u32>(images.size());
}

} // namespace Vulkan
