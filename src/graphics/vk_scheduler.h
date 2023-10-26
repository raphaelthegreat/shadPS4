#pragma once

#include <vector>

#include "common/types.h"
#include "graphics/vk_common.h"

namespace Vulkan {

class Instance;

class Scheduler {
public:
    explicit Scheduler(const Instance& instance);
    ~Scheduler();

    void submitWork(vk::Semaphore signal = {}, vk::Semaphore wait = {}, vk::Fence fence = {});

    void finish();

    void waitFor(u64 counter);

    void refresh();

    u64 getGpuCounter() const noexcept {
        return gpuCounter;
    }

    u64 getCpuCounter() const noexcept {
        return cpuCounter;
    }

    bool isFree(u64 counter) const noexcept {
        return gpuCounter >= counter;
    }

    vk::CommandBuffer getCmdBuf() const noexcept {
        return cmdbuffers[currentCmdbuf].cmdbuf;
    }

private:
    void switchCmdbuffer();
    void growNumBuffers();

private:
    const Instance& instance;
    vk::UniqueSemaphore timeline{};
    vk::UniqueCommandPool cmdpool{};
    u64 cpuCounter{1};
    u64 gpuCounter{0};

    struct CommandBuffer {
        vk::CommandBuffer cmdbuf;
        u64 lastUsedCounter;
    };
    std::vector<CommandBuffer> cmdbuffers;
    u32 currentCmdbuf{0};
};

} // namespace Vulkan
