#include <fmt/core.h>
#include "video_core/vk_instance.h"
#include "core/frontend/emu_window.h"
#include "graphics/vk_platform.h"

#include <vk_mem_alloc.h>

namespace Vulkan {

Instance::Instance(Core::Frontend::EmuWindow& window_, u32 physicalDevice, bool enableDebug_)
        : window(window_), enableDebug(enableDebug_) {
    createInstance();
    pickPhysicalDevice(physicalDevice);
    pickGraphicsQueue();
    createDevice();
    createAllocator();
}

Instance::~Instance() = default;

void Instance::createInstance() {
    const auto getInstaceProcAddr = dl.getProcAddress<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr");
    VULKAN_HPP_DEFAULT_DISPATCHER.init(getInstaceProcAddr);

    const vk::ApplicationInfo applicationInfo = {
        .pApplicationName = "Alber",
        .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
        .pEngineName = "Alber",
        .engineVersion = VK_MAKE_VERSION(1, 0, 0),
        .apiVersion = VK_API_VERSION_1_1,
    };

    const auto instanceExtensions = getInstanceExtensions(window.getInfo().type, enableDebug);
    vk::InstanceCreateInfo instanceInfo = {
        .pApplicationInfo = &applicationInfo,
        .enabledLayerCount = 0,
        .ppEnabledLayerNames = nullptr,
        .enabledExtensionCount = static_cast<u32>(instanceExtensions.size()),
        .ppEnabledExtensionNames = instanceExtensions.data(),
    };

    if (auto createResult = vk::createInstanceUnique(instanceInfo); createResult.result == vk::Result::eSuccess) {
        instance = std::move(createResult.value);
    } else {
        fmt::print("Error creating Vulkan instance: {}\n", vk::to_string(createResult.result).c_str());
    }

    // Initialize instance-specific function pointers
    VULKAN_HPP_DEFAULT_DISPATCHER.init(instance.get());

    // Enable debug messenger if the instance was able to be created with debug_utils
    if (!enableDebug) {
        return;
    }

    debugMessenger = createDebugCallback(*instance);
}

void Instance::pickPhysicalDevice(u32 index) {
    const auto enumerateResult = instance->enumeratePhysicalDevices();
    if (enumerateResult.result != vk::Result::eSuccess) [[unlikely]] {
        fmt::print("Error enumerating physical devices: {}\n", vk::to_string(enumerateResult.result).c_str());
        return;
    }

    const auto physicalDevices = std::move(enumerateResult.value);
    if (index >= physicalDevices.size()) [[unlikely]] {
        fmt::print("Physical device index %d exceeds number of queried devices {}\n",
                   index, physicalDevices.size());
        return;
    }
    physicalDevice = physicalDevices[index];
}

void Instance::pickGraphicsQueue() {
    const auto familyProperties = physicalDevice.getQueueFamilyProperties();
    if (familyProperties.empty()) [[unlikely]] {
        fmt::print("Physical device reported no queues\n");
        return;
    }

    // Find a queue family that supports both graphics and transfer for our use.
    // We can assume that this queue will also support present as this is the case
    // on all modern hardware.
    for (size_t i = 0; i < familyProperties.size(); i++) {
        const u32 index = static_cast<u32>(i);
        const auto flags = familyProperties[i].queueFlags;
        if (flags & vk::QueueFlagBits::eGraphics && flags & vk::QueueFlagBits::eTransfer) {
            queueFamilyIndex = index;
            break;
        }
    }

    if (queueFamilyIndex == -1) {
        fmt::print("Unable to find suitable graphics queue\n");
        std::exit(0);
    }
}

void Instance::createDevice() {
    // Query for a few useful extensions for 3DS emulation
    const vk::StructureChain featureChain = physicalDevice.getFeatures2<
        vk::PhysicalDeviceFeatures2,
#if defined(__APPLE__)
        vk::PhysicalDevicePortabilitySubsetFeaturesKHR,
#endif
        vk::PhysicalDeviceTimelineSemaphoreFeaturesKHR,
        vk::PhysicalDeviceCustomBorderColorFeaturesEXT,
        vk::PhysicalDeviceIndexTypeUint8FeaturesEXT>();

    features = featureChain.get().features;

    const auto [result, extensions] = physicalDevice.enumerateDeviceExtensionProperties();
    std::array<const char*, 10> enabledExtensions;

    static constexpr float queuePriority = 1.0f;

    const vk::DeviceQueueCreateInfo queueInfo = {
        .queueFamilyIndex = static_cast<u32>(queueFamilyIndex),
        .queueCount = 1u,
        .pQueuePriorities = &queuePriority,
    };

    vk::StructureChain deviceChain = {
        vk::DeviceCreateInfo{
            .queueCreateInfoCount = 1u,
            .pQueueCreateInfos = &queueInfo,
            .enabledExtensionCount = 0,
            .ppEnabledExtensionNames = enabledExtensions.data(),
        },
        vk::PhysicalDeviceFeatures2{
            .features{
                .robustBufferAccess = features.robustBufferAccess,
                .logicOp = features.logicOp,
                .samplerAnisotropy = features.samplerAnisotropy,
                .shaderClipDistance = features.shaderClipDistance,
            },
        },
#if defined(__APPLE__)
        vk::PhysicalDevicePortabilitySubsetFeaturesKHR{},
#endif
        vk::PhysicalDeviceTimelineSemaphoreFeaturesKHR{
            .timelineSemaphore = true,
        },
        vk::PhysicalDeviceCustomBorderColorFeaturesEXT{
            .customBorderColorWithoutFormat = true,
        },
        vk::PhysicalDeviceIndexTypeUint8FeaturesEXT{
            .indexTypeUint8 = true,
        },
    };

    const auto addExtension = [&](std::string_view extension) -> bool {
        const auto result =
            std::find_if(extensions.begin(), extensions.end(),
                         [&](const auto& prop) { return prop.extensionName == extension; });

        if (result != extensions.end()) {
            printf("Using vulkan extension %s\n", extension.data());
            u32& numExtensions = deviceChain.get().enabledExtensionCount;
            enabledExtensions[numExtensions++] = extension.data();
            return true;
        }

        printf("Requested extension %s is unavailable\n", extension.data());
        return false;
    };

    // Helper macro that checks for an extension and any required features
#define USE_EXTENSION(name, feature_type, feature, property) \
    property = addExtension(name) && featureChain.get<feature_type>().feature;  \
    if (!property) { \
        deviceChain.unlink<feature_type>(); \
    }

    addExtension(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
#if defined(__APPLE__)
    portabilitySubset = useExtension(VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME);
#endif
    USE_EXTENSION(VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME, vk::PhysicalDeviceTimelineSemaphoreFeaturesKHR,
                  timelineSemaphore, timelineSemaphores);
    USE_EXTENSION(VK_EXT_CUSTOM_BORDER_COLOR_EXTENSION_NAME, vk::PhysicalDeviceCustomBorderColorFeaturesEXT,
                  customBorderColorWithoutFormat, customBorderColor);
    USE_EXTENSION(VK_EXT_INDEX_TYPE_UINT8_EXTENSION_NAME, vk::PhysicalDeviceIndexTypeUint8FeaturesEXT,
                  indexTypeUint8, indexTypeUint8);
#undef USE_EXTENSION

    if (auto createResult = physicalDevice.createDeviceUnique(deviceChain.get()); createResult.result == vk::Result::eSuccess) {
        device = std::move(createResult.value);
    } else {
        fmt::print("Error creating logical device: {}\n", vk::to_string(createResult.result).c_str());
    }

    // Initialize device-specific function pointers
    VULKAN_HPP_DEFAULT_DISPATCHER.init(device.get());
    graphicsQueue = device->getQueue(queueFamilyIndex, 0);
}

void Instance::createAllocator() {
    const VmaVulkanFunctions functions = {
        .vkGetInstanceProcAddr = VULKAN_HPP_DEFAULT_DISPATCHER.vkGetInstanceProcAddr,
        .vkGetDeviceProcAddr = VULKAN_HPP_DEFAULT_DISPATCHER.vkGetDeviceProcAddr,
    };

    const VmaAllocatorCreateInfo allocatorInfo = {
        .physicalDevice = physicalDevice,
        .device = *device,
        .pVulkanFunctions = &functions,
        .instance = *instance,
        .vulkanApiVersion = vk::enumerateInstanceVersion().value,
    };

    const VkResult result = vmaCreateAllocator(&allocatorInfo, &allocator);
    if (result != VK_SUCCESS) {
        fmt::print("Failed to initialize VMA with error {}\n", vk::to_string(static_cast<vk::Result>(result)));
    }
}

} // namespace Vulkan
