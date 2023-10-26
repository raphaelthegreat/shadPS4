#pragma once

#include <vector>
#include "graphics/vk_common.h"

namespace Core::Frontend {
class EmuWindow;
enum class WindowSystemType : unsigned char;
}

namespace Vulkan {

vk::SurfaceKHR createSurface(vk::Instance instance, const Core::Frontend::EmuWindow& emu_window);

std::vector<const char*> getInstanceExtensions(Core::Frontend::WindowSystemType window_type, bool enable_debug_utils);

vk::UniqueDebugUtilsMessengerEXT createDebugCallback(vk::Instance instance);

} // namespace Vulkan
