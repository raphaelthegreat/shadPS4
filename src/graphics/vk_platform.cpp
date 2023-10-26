// Copyright 2023 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

// Include the vulkan platform specific header
#if defined(ANDROID)
#define VK_USE_PLATFORM_ANDROID_KHR
#elif defined(WIN32)
#define VK_USE_PLATFORM_WIN32_KHR
#elif defined(__APPLE__)
#define VK_USE_PLATFORM_METAL_EXT
#else
#define VK_USE_PLATFORM_WAYLAND_KHR
#define VK_USE_PLATFORM_XLIB_KHR
#endif

#include <memory>
#include <fmt/core.h>

#include "core/frontend/emu_window.h"
#include "graphics/vk_platform.h"

namespace Vulkan {

static VKAPI_ATTR VkBool32 VKAPI_CALL DebugUtilsCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT type,
    const VkDebugUtilsMessengerCallbackDataEXT* callback_data, void* user_data) {

    std::string level;
    switch (severity) {
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT:
        level = "[ERROR]";
        break;
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT:
        level = "[INFO]";
        break;
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT:
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT:
        level = "[DEBUG]";
        break;
    default:
        level = "[INFO]";
    }

    fmt::print("{} {}: {}\n", level, callback_data->pMessageIdName, callback_data->pMessage);
    return VK_FALSE;
}

vk::SurfaceKHR createSurface(vk::Instance instance, const Core::Frontend::EmuWindow& emu_window) {
    const auto& window_info = emu_window.getInfo();
    vk::SurfaceKHR surface{};

#if defined(VK_USE_PLATFORM_WIN32_KHR)
    if (window_info.type == Core::Frontend::WindowSystemType::Windows) {
        const vk::Win32SurfaceCreateInfoKHR win32_ci = {
            .hinstance = nullptr,
            .hwnd = static_cast<HWND>(window_info.render_surface),
        };
        surface = instance.createWin32SurfaceKHR(win32_ci).value;
    }
#elif defined(VK_USE_PLATFORM_XLIB_KHR) || defined(VK_USE_PLATFORM_WAYLAND_KHR)
    if (window_info.type == Core::Frontend::WindowSystemType::X11) {
        const vk::XlibSurfaceCreateInfoKHR xlib_ci = {
            .dpy = static_cast<Display*>(window_info.display_connection),
            .window = reinterpret_cast<Window>(window_info.render_surface),
        };
        surface = nstance.createXlibSurfaceKHR(xlib_ci).value;
    } else if (window_info.type == Core::Frontend::WindowSystemType::Wayland) {
        const vk::WaylandSurfaceCreateInfoKHR wayland_ci = {
            .display = static_cast<wl_display*>(window_info.display_connection),
            .surface = static_cast<wl_surface*>(window_info.render_surface),
        };
        surface = instance.createWaylandSurfaceKHR(wayland_ci).value;
    }
#endif

    if (!surface) [[unlikely]] {
        fmt::print("Presentation not supported on this platform\n");
        std::exit(0);
    }

    return surface;
}

std::vector<const char*> getInstanceExtensions(Core::Frontend::WindowSystemType window_type, bool enable_debug_utils) {
    const auto properties = vk::enumerateInstanceExtensionProperties().value;
    if (properties.empty()) {
        fmt::print("Failed to query extension properties\n");
        return {};
    }

    std::vector<const char*> extensions;
    extensions.reserve(6);

    switch (window_type) {
    case Core::Frontend::WindowSystemType::Headless:
        break;
#if defined(VK_USE_PLATFORM_WIN32_KHR)
    case Core::Frontend::WindowSystemType::Windows:
        extensions.push_back(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);
        break;
#elif defined(VK_USE_PLATFORM_XLIB_KHR) || defined(VK_USE_PLATFORM_WAYLAND_KHR)
    case Core::Frontend::WindowSystemType::X11:
        extensions.push_back(VK_KHR_XLIB_SURFACE_EXTENSION_NAME);
        break;
    case Core::Frontend::WindowSystemType::Wayland:
        extensions.push_back(VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME);
        break;
#endif
    default:
        fmt::print("Presentation not supported on this platform\n");
        break;
    }

    if (window_type != Core::Frontend::WindowSystemType::Headless) {
        extensions.push_back(VK_KHR_SURFACE_EXTENSION_NAME);
    }

    if (enable_debug_utils) {
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    // Sanitize extension list
    std::erase_if(extensions, [&](const char* extension) -> bool {
        const auto it =
            std::find_if(properties.begin(), properties.end(), [extension](const auto& prop) {
                return std::strcmp(extension, prop.extensionName) == 0;
            });

        if (it == properties.end()) {
            fmt::print("Candidate instance extension {} is not available\n", extension);
            return true;
        }
        return false;
    });

    return extensions;
}

vk::UniqueDebugUtilsMessengerEXT createDebugCallback(vk::Instance instance) {
    const vk::DebugUtilsMessengerCreateInfoEXT msg_ci = {
        .messageSeverity = vk::DebugUtilsMessageSeverityFlagBitsEXT::eInfo |
                           vk::DebugUtilsMessageSeverityFlagBitsEXT::eError |
                           vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning |
                           vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose,
        .messageType = vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral |
                       vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation |
                       vk::DebugUtilsMessageTypeFlagBitsEXT::eDeviceAddressBinding |
                       vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance,
        .pfnUserCallback = DebugUtilsCallback,
    };
    auto result = instance.createDebugUtilsMessengerEXTUnique(msg_ci);
    if (result.result != vk::Result::eSuccess) {
        fmt::print("Unable to create debug messenger\n");
        return {};
    }
    return std::move(result.value);
}

} // namespace Vulkan
