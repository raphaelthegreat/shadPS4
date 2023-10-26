#pragma once

#include <vector>

#include "common/types.h"
#include "graphics/vk_common.h"

namespace Core::Frontend {
class EmuWindow;
}

namespace Vulkan {

class Instance;
class Scheduler;

class Swapchain {
public:
    explicit Swapchain(const Core::Frontend::EmuWindow& window, const Instance& instance,
                       Scheduler& scheduler, u32 width, u32 height);
    ~Swapchain();

    void create(u32 width, u32 height);
    bool acquireNextImage();
    void present();

    vk::SurfaceKHR getSurface() const {
        return surface;
    }

    vk::Image getImage() const {
        return images[imageIndex];
    }

    vk::SurfaceFormatKHR getSurfaceFormat() const {
        return surfaceFormat;
    }

    vk::SwapchainKHR getHandle() const {
        return *swapchain;
    }

    u32 getWidth() const {
        return width;
    }

    u32 getHeight() const {
        return height;
    }

    u32 getImageIndex() const {
        return imageIndex;
    }

    u32 getImageCount() const {
        return imageCount;
    }

    vk::Extent2D getExtent() const {
        return extent;
    }

    vk::Semaphore getImageAcquiredSemaphore() const {
        return *imageAcquired[frameIndex];
    }

    vk::Semaphore getPresentReadySemaphore() const {
        return *presentReady[imageIndex];
    }

    vk::Fence getFence() const {
        return *fences[imageIndex];
    }

private:
    void findPresentFormat();
    void queryPresentMode();
    void setSurfaceProperties();
    void destroy();
    void setupImages();
    void refreshSemaphores();

private:
    const Core::Frontend::EmuWindow& window;
    const Instance& instance;
    Scheduler& scheduler;
    vk::UniqueSwapchainKHR swapchain{};
    vk::SurfaceKHR surface{};
    vk::SurfaceFormatKHR surfaceFormat;
    vk::PresentModeKHR presentMode;
    vk::Extent2D extent;
    vk::SurfaceTransformFlagBitsKHR transform;
    vk::CompositeAlphaFlagBitsKHR compositeAlpha;
    std::vector<vk::UniqueFence> fences;
    std::vector<vk::Image> images;
    std::vector<vk::UniqueSemaphore> imageAcquired;
    std::vector<vk::UniqueSemaphore> presentReady;
    u32 width = 0;
    u32 height = 0;
    u32 imageCount = 0;
    u32 imageIndex = 0;
    u32 frameIndex = 0;
    bool needsRecreation = true;
};

} // namespace Vulkan
