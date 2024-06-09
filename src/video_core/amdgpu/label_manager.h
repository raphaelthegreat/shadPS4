#pragma once

#include <future>
#include <tsl/robin_map.h>
#include "common/assert.h"
#include "common/types.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"

namespace Vulkan {
class Instance;
class Scheduler;
} // namespace Vulkan

namespace AmdGpu {

class LabelManager {
public:
    explicit LabelManager(const Vulkan::Instance* instance, Vulkan::Scheduler& scheduler)
        : instance{instance}, scheduler{scheduler} {}
    ~LabelManager() = default;

    void Signal(VAddr addr, u64 value, bool is_32bit, auto&& on_signal) {
        auto* label = GetLabel(addr);
        const auto cmdbuf = scheduler.CommandBuffer();
        LOG_INFO(Render_Vulkan, "Signalling label {:#x} with value {:#x}", addr, value);
        label->signal_value = value;
        label->has_signal = true;
        cmdbuf.setEvent(*label->event, vk::PipelineStageFlagBits::eAllCommands);
        std::future<void> future = std::async(
            [this, is_32bit, func = std::move(on_signal), addr, value, event = *label->event] {
                while (instance->GetDevice().getEventStatus(event) != vk::Result::eEventSet) {
                    std::this_thread::yield();
                }
                LOG_INFO(Render_Vulkan, "Label {:#x} got written value {:#x}", addr, value);
                if (is_32bit) {
                    *reinterpret_cast<u32*>(addr) = value;
                } else {
                    *reinterpret_cast<u64*>(addr) = value;
                }
                func();
            });
        label->future = std::move(future);
    }

    void Wait(VAddr addr, u64 value) {
        LOG_INFO(Render_Vulkan, "Inserting wait for label {:#x} for value {:#x}", addr, value);
        auto* label = GetLabel(addr);
        ASSERT(label->signal_value == value && label->has_signal);
        const auto cmdbuf = scheduler.CommandBuffer();
        cmdbuf.waitEvents(*label->event, vk::PipelineStageFlagBits::eAllCommands,
                          vk::PipelineStageFlagBits::eAllCommands, {}, {}, {});
        cmdbuf.resetEvent(*label->event, vk::PipelineStageFlagBits::eAllCommands);
        label->has_signal = false;
    }

    struct Label {
        std::future<void> future;
        vk::UniqueEvent event;
        u64 signal_value;
        bool has_signal{};
    };

    Label* GetLabel(VAddr addr) {
        const auto [it, new_label] = labels.try_emplace(addr);
        if (new_label) {
            it.value().event = instance->GetDevice().createEventUnique({});
        }
        return &it.value();
    }

private:
    const Vulkan::Instance* instance;
    Vulkan::Scheduler& scheduler;
    tsl::robin_map<VAddr, Label> labels;
};

} // namespace AmdGpu
