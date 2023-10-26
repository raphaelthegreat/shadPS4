#pragma once

#include "common/types.h"

namespace Core::Frontend {

enum class WindowSystemType : u8 {
    Headless,
    Android,
    Windows,
    MacOS,
    X11,
    Wayland,
};

struct WindowSystemInfo {
    // Window system type. Determines which GL context or Vulkan WSI is used.
    WindowSystemType type = WindowSystemType::Headless;
    // Connection to a display server. This is used on X11 and Wayland platforms.
    void* display_connection = nullptr;
    // Render surface. This is a pointer to the native window handle, which depends
    // on the platform. e.g. HWND for Windows, Window for X11. If the surface is
    // set to nullptr, the video backend will run in headless mode.
    void* render_surface = nullptr;
    // Scale of the render surface. For hidpi systems, this will be >1.
    float render_surface_scale = 1.0f;
};

class EmuWindow {
public:
    explicit EmuWindow(s32 width_, s32 height_)
        : width{width_}, height{height_} {}
    virtual ~EmuWindow() = default;

    s32 getWidth() const {
        return width;
    }

    s32 getHeight() const {
        return height;
    }

    bool isRunning() const {
        return is_running;
    }
    
    const WindowSystemInfo& getInfo() const {
        return window_info;
    }

    virtual void pollEvents();

protected:
    s32 width{};
    s32 height{};
    bool is_running{};
    WindowSystemInfo window_info{};
};

} // namespace Core::Frontend
