// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <limits>
#include <boost/container/static_vector.hpp>
#include "common/assert.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_master_semaphore.h"

namespace Vulkan {

constexpr u64 WAIT_TIMEOUT = std::numeric_limits<u64>::max();

MasterSemaphore::MasterSemaphore(const Instance& instance_) : instance{instance_} {
    const vk::StructureChain semaphore_chain = {
        vk::SemaphoreCreateInfo{},
        vk::SemaphoreTypeCreateInfo{
            .semaphoreType = vk::SemaphoreType::eTimeline,
            .initialValue = 0,
        },
    };
    semaphore = instance.GetDevice().createSemaphoreUnique(semaphore_chain.get());
}

MasterSemaphore::~MasterSemaphore() = default;

void MasterSemaphore::Refresh() {
    u64 this_tick{};
    u64 counter{};
    do {
        this_tick = gpu_tick.load(std::memory_order_acquire);
        counter = instance.GetDevice().getSemaphoreCounterValue(*semaphore);
        if (counter < this_tick) {
            return;
        }
    } while (!gpu_tick.compare_exchange_weak(this_tick, counter, std::memory_order_release,
                                             std::memory_order_relaxed));
}

void MasterSemaphore::Wait(u64 tick) {
    // No need to wait if the GPU is ahead of the tick
    if (IsFree(tick)) {
        return;
    }
    // Update the GPU tick and try again
    Refresh();
    if (IsFree(tick)) {
        return;
    }

    // If none of the above is hit, fallback to a regular wait
    const vk::SemaphoreWaitInfo wait_info = {
        .semaphoreCount = 1,
        .pSemaphores = &semaphore.get(),
        .pValues = &tick,
    };

    while (instance.GetDevice().waitSemaphores(&wait_info, WAIT_TIMEOUT) != vk::Result::eSuccess) {
    }
    Refresh();
}

void MasterSemaphore::SubmitWork(vk::CommandBuffer cmdbuf, std::span<const vk::Semaphore> wait,
                                 vk::Semaphore signal,
                                 vk::Fence fence, s64 signal_value) {
    cmdbuf.end();

    boost::container::static_vector<vk::Semaphore, 2> signal_semaphores;
    boost::container::static_vector<u64, 2> signal_values;

    if (signal_value) {
        signal_semaphores.emplace_back(Handle());
        signal_values.emplace_back(signal_value);
    }
    if (signal) {
        signal_semaphores.emplace_back(signal);
        signal_values.emplace_back(1);
    }

    static constexpr std::array<vk::PipelineStageFlags, 2> wait_stage_masks = {
        vk::PipelineStageFlagBits::eAllCommands,
        vk::PipelineStageFlagBits::eColorAttachmentOutput,
    };

    const vk::TimelineSemaphoreSubmitInfo timeline_si = {
        .waitSemaphoreValueCount = 0U,
        .pWaitSemaphoreValues = nullptr,
        .signalSemaphoreValueCount = static_cast<u32>(signal_values.size()),
        .pSignalSemaphoreValues = signal_values.data(),
    };

    const vk::SubmitInfo submit_info = {
        .pNext = signal_value ? &timeline_si : nullptr,
        .waitSemaphoreCount = static_cast<u32>(wait.size()),
        .pWaitSemaphores = wait.data(),
        .pWaitDstStageMask = wait_stage_masks.data(),
        .commandBufferCount = 1u,
        .pCommandBuffers = &cmdbuf,
        .signalSemaphoreCount = static_cast<u32>(signal_semaphores.size()),
        .pSignalSemaphores = signal_semaphores.data(),
    };

    try {
        instance.GetGraphicsQueue().submit(submit_info, fence);
    } catch (vk::DeviceLostError& err) {
        UNREACHABLE_MSG("Device lost during submit: {}", err.what());
    }
}

} // namespace Vulkan
