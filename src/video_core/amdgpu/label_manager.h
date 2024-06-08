#pragma once

#include <tsl/robin_map.h>
#include "common/types.h"
#include "video_core/renderer_vulkan/vk_common.h"

namespace Vulkan {
class Instance;
class Scheduler;
}

namespace AmdGpu {

class LabelManager {
public:
    explicit LabelManager(const Vulkan::Instance* instance,
                          Vulkan::Scheduler& scheduler);
    ~LabelManager();

    void Signal();
    void Wait();

private:
    const Vulkan::Instance* instance;
    Vulkan::Scheduler& scheduler;
    struct Label {
        vk::Event event;
        u64 signal_value;
    };
    tsl::robin_map<VAddr, Label> labels;
    struct WaitOperation {
        std::function<void()> callback;
    };
};

} // namespace AmdGpu

