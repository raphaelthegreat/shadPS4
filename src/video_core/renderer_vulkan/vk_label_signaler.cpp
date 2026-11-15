// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <chrono>

#ifdef __linux__
#include <sys/prctl.h>
#endif

#include "common/assert.h"
#include "common/thread.h"
#include "core/emulator_settings.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_label_signaler.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"

namespace Vulkan {

constexpr std::chrono::microseconds QueryPollInterval{50};
constexpr std::chrono::milliseconds TickPollInterval{10};
constexpr u32 NumQueries = 4096;

LabelSignaler::LabelSignaler(const Instance& instance, Scheduler& scheduler_)
    : scheduler{scheduler_}, host_gpu_sync{EmulatorSettings.IsHostGpuSync()},
      device{instance.GetDevice()} {
    if (!host_gpu_sync) {
        return;
    }
    const auto queue_props = instance.GetPhysicalDevice().getQueueFamilyProperties();
    const bool timestamps_supported =
        queue_props[instance.GetGraphicsQueueFamilyIndex()].timestampValidBits != 0;
    if (timestamps_supported && instance.IsHostQueryResetSupported()) {
        const vk::QueryPoolCreateInfo pool_ci = {
            .queryType = vk::QueryType::eTimestamp,
            .queryCount = NumQueries,
        };
        auto [pool_result, pool] = device.createQueryPoolUnique(pool_ci);
        ASSERT_MSG(pool_result == vk::Result::eSuccess, "Failed to create label query pool: {}",
                   vk::to_string(pool_result));
        query_pool = std::move(pool);
        device.resetQueryPool(*query_pool, 0, NumQueries);
    }
    signal_thread = std::jthread{std::bind_front(&LabelSignaler::SignalThread, this)};
}

void LabelSignaler::Signal(AmdGpu::EventType event_type, AmdGpu::EventIndex event_index,
                           Common::UniqueFunction<void>&& func,
                           std::optional<AmdGpu::LabelWrite> label) {
    if (!host_gpu_sync) {
        func();
        return;
    }
    vk::PipelineStageFlags2 stage = vk::PipelineStageFlagBits2::eAllCommands;
    if (event_index == AmdGpu::EventIndex::EosReserved) {
        if (event_type == AmdGpu::EventType::CsDone) {
            stage = vk::PipelineStageFlagBits2::eComputeShader;
        } else if (event_type == AmdGpu::EventType::PsDone) {
            stage = vk::PipelineStageFlagBits2::eFragmentShader;
        }
    }

    const u32 marker = ++write_slot;
    if (query_pool) {
        WaitFreeQuery(marker);
        scheduler.CommandBuffer().writeTimestamp2(stage, *query_pool, marker % NumQueries);
    }
    last_signal_tick = scheduler.CurrentTick();

    {
        std::scoped_lock lk{mutex};
        pending_signals.push_back(PendingSignal{
            .func = std::move(func),
            .tick = last_signal_tick,
            .marker = marker,
            .label = label,
        });
    }
    signal_cv.notify_one();
}

bool LabelSignaler::NeedsFlush() const noexcept {
    return host_gpu_sync && last_signal_tick >= scheduler.CurrentTick();
}

void LabelSignaler::WaitForProgress() {
    const u32 read = read_slot.load(std::memory_order_acquire);
    if (read == write_slot) {
        return;
    }
    read_slot.wait(read, std::memory_order_acquire);
}

void LabelSignaler::SignalThread(std::stop_token stoken) {
    Common::SetCurrentThreadName("shadPS4:GpuLabelSignaler");

#ifdef __linux__
    prctl(PR_SET_TIMERSLACK, 1);
#endif

    while (!stoken.stop_requested()) {
        PendingSignal* signal;
        {
            std::unique_lock lk{mutex};
            signal_cv.wait(lk, stoken, [this] { return !pending_signals.empty(); });
            if (stoken.stop_requested()) {
                break;
            }
            signal = &pending_signals.front();
        }

        if (!WaitSignal(*signal, stoken)) {
            break;
        }
        signal->func();
        if (query_pool) {
            device.resetQueryPool(*query_pool, signal->marker % NumQueries, 1);
        }
        {
            std::scoped_lock lk{mutex};
            read_slot.store(signal->marker, std::memory_order_release);
            pending_signals.pop_front();
        }
        read_slot.notify_all();
    }
}

bool LabelSignaler::WaitSignal(const PendingSignal& signal, std::stop_token stoken) {
    auto* semaphore = scheduler.GetWorkSemaphore();
    while (!stoken.stop_requested()) {
        if (!query_pool) {
            if (semaphore->WaitFor(signal.tick, TickPollInterval)) {
                return true;
            }
            continue;
        }
        u64 timestamp;
        const auto result = device.getQueryPoolResults(
            *query_pool, signal.marker % NumQueries, 1, sizeof(timestamp), &timestamp,
            sizeof(timestamp), vk::QueryResultFlagBits::e64);
        if (result == vk::Result::eSuccess) {
            return true;
        }
        // Driver semaphore waits with short timeouts may busy spin, sleep between polls instead
        Common::AccurateSleep(QueryPollInterval, nullptr, false);
    }
    return false;
}

void LabelSignaler::WaitFreeQuery(u32 marker) {
    // The slot is free once the signal that previously used it has been retired and reset
    while (marker - read_slot.load(std::memory_order_acquire) > NumQueries) {
        if (NeedsFlush()) {
            scheduler.Flush();
        }
        WaitForProgress();
    }
}

} // namespace Vulkan
