#pragma once

#include <mutex>
#include <thread>
#include <condition_variable>
#include <vector>

#include "common/types.h"
#include <Core/PS4/HLE/Graphics/graphics_ctx.h>

union SDL_Event;
struct SDL_Window;

namespace Emulator::Host::Controller {
class GameController;
}

namespace Emulator::HLE::Libraries::LibPad {
enum class ScePadButton : u32;
}

namespace Core {

struct VulkanExt {
    bool enable_validation_layers = false;

    std::vector<const char*> required_extensions;
    std::vector<VkExtensionProperties> available_extensions;
    std::vector<const char*> required_layers;
    std::vector<VkLayerProperties> available_layers;
};

struct VulkanSurfaceCapabilities {
    VkSurfaceCapabilitiesKHR capabilities{};
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> present_modes;
    bool is_format_srgb_bgra32 = false;
    bool is_format_unorm_bgra32 = false;
};

struct VulkanQueueInfo {
    u32 family = 0;
    u32 index = 0;
    bool is_graphics = false;
    bool is_compute = false;
    bool is_transfer = false;
    bool is_present = false;
};

struct VulkanQueues {
    u32 family_count = 0;
    std::vector<VulkanQueueInfo> available;
    std::vector<VulkanQueueInfo> graphics;
    std::vector<VulkanQueueInfo> compute;
    std::vector<VulkanQueueInfo> transfer;
    std::vector<VulkanQueueInfo> present;
    std::vector<u32> family_used;
};

struct VulkanSwapchain {
    VkSwapchainKHR swapchain = nullptr;
    VkFormat swapchain_format = VK_FORMAT_UNDEFINED;
    VkExtent2D swapchain_extent = {};
    VkImage* swapchain_images = nullptr;
    VkImageView* swapchain_image_views = nullptr;
    u32 swapchain_images_count = 0;
    VkSemaphore present_complete_semaphore = nullptr;
    VkFence present_complete_fence = nullptr;
    u32 current_index = 0;
};

struct WindowCtx {
    HLE::Libs::Graphics::GraphicCtx m_graphic_ctx;
    std::mutex m_mutex;
    bool m_is_graphic_initialized = false;
    std::condition_variable m_graphic_initialized_cond;
    SDL_Window* m_window = nullptr;
    bool is_window_hidden = true;
    VkSurfaceKHR m_surface = nullptr;
    VulkanSurfaceCapabilities* m_surface_capabilities = nullptr;
    VulkanSwapchain* swapchain = nullptr;
};

void emuInit(u32 width, u32 height);
void emuRun();
void checkAndWaitForGraphicsInit();
HLE::Libs::Graphics::GraphicCtx* getGraphicCtx();
void DrawBuffer(HLE::Libs::Graphics::VideoOutVulkanImage* image);
void keyboardEvent(SDL_Event* event);

class Window {
public:
    explicit Window(s32 width_, s32 height_) : width{width_}, height{height_} {}
    virtual ~Window() = default;

    s32 getWidth() const {
        return width;
    }

    s32 getHeight() const {
        return height;
    }

    bool isRunning() const {
        return is_running;
    }
    
    virtual void pollEvents();

protected:
    s32 width{};
    s32 height{};
    bool is_running{};
};

class WindowSDL : public Window {
public:
    explicit WindowSDL(Emulator::Host::Controller::GameController& controller, s32 width, s32 height);
    ~WindowSDL();

    void pollEvents() override;

private:
    void keyboardEvent(SDL_Event* event);
    void resizeEvent();

private:
    Emulator::Host::Controller::GameController& controller;
    SDL_Window* sdl_window{};
};

class Emulator {
public:
    explicit Emulator();
    ~Emulator();

    [[nodiscard]] HLE::Libs::Graphics::GraphicCtx* getGraphicCtx() const {
        return m_graphic_ctx;
    }

    void run();
    void drawBuffer(HLE::Libs::Graphics::VideoOutVulkanImage* image);

private:
    std::unique_ptr<Window> window;
    std::jthread main_thread;
    HLE::Libs::Graphics::GraphicCtx* m_graphic_ctx = nullptr;
    void* data1 = nullptr;
    void* data2 = nullptr;
    u32 m_screen_width{};
    u32 m_screen_height{};
};

} // namespace Core
