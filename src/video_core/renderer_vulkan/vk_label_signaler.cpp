// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <cstring>

#include "common/assert.h"
#include "common/thread.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_label_signaler.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"

namespace Vulkan {

constexpr std::chrono::microseconds MarkerPollInterval{50};
constexpr std::chrono::milliseconds TickPollInterval{10};

LabelSignaler::LabelSignaler(const Instance& instance, Scheduler& scheduler_)
    : scheduler{scheduler_} {
    if (instance.IsAmdBufferMarkerSupported()) {
        marker_buffer.emplace(instance, 0, NumMarkerSlots * sizeof(u32),
                              VideoCore::MemoryType::HostCached, "LabelMarkers");
        ASSERT_MSG(!marker_buffer->mapped_data.empty(), "Label marker buffer is not host visible");
        std::memset(marker_buffer->mapped_data.data(), 0, marker_buffer->SizeBytes());
        marker_buffer->Flush(0, marker_buffer->SizeBytes());
    }
    signal_thread = std::jthread{std::bind_front(&LabelSignaler::SignalThread, this)};
}

LabelSignaler::~LabelSignaler() {
    signal_thread.request_stop();
    signal_thread.join();
}

void LabelSignaler::Signal(AmdGpu::EventType event_type, AmdGpu::EventIndex event_index,
                           Common::UniqueFunction<void>&& func,
                           std::optional<AmdGpu::LabelWrite> label) {
    vk::PipelineStageFlags2 stage = vk::PipelineStageFlagBits2::eAllCommands;
    MarkerSlot slot = MarkerSlot::AllCommands;
    if (event_index == AmdGpu::EventIndex::EosReserved) {
        if (event_type == AmdGpu::EventType::CsDone) {
            stage = vk::PipelineStageFlagBits2::eComputeShader;
            slot = MarkerSlot::ComputeShader;
        } else if (event_type == AmdGpu::EventType::PsDone) {
            stage = vk::PipelineStageFlagBits2::eFragmentShader;
            slot = MarkerSlot::FragmentShader;
        }
    }

    const u32 marker = ++current_marker;
    if (marker_buffer) {
        scheduler.CommandBuffer().writeBufferMarker2AMD(stage, marker_buffer->Handle(),
                                                        slot * sizeof(u32), marker);
    }
    last_signal_tick = scheduler.CurrentTick();

    {
        std::scoped_lock lk{mutex};
        pending_signals.push_back(PendingSignal{
            .func = std::move(func),
            .tick = last_signal_tick,
            .marker = marker,
            .slot = slot,
            .label = label,
        });
    }
    signal_cv.notify_one();
}

bool LabelSignaler::NeedsFlush() const noexcept {
    return last_signal_tick >= scheduler.CurrentTick();
}

void LabelSignaler::WaitForProgress(std::chrono::microseconds timeout) {
    std::unique_lock lk{mutex};
    const u32 retired = retired_marker.load(std::memory_order_relaxed);
    progress_cv.wait_for(lk, timeout,
                         [&] { return retired_marker.load(std::memory_order_relaxed) != retired; });
}

void LabelSignaler::SignalThread(std::stop_token stoken) {
    Common::SetCurrentThreadName("shadPS4:GpuLabelSignaler");

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

        {
            std::scoped_lock lk{mutex};
            retired_marker.store(signal->marker, std::memory_order_release);
            pending_signals.pop_front();
        }
        progress_cv.notify_all();
    }
}

bool LabelSignaler::WaitSignal(const PendingSignal& signal, std::stop_token stoken) {
    auto* semaphore = scheduler.GetWorkSemaphore();
    const auto poll_interval =
        marker_buffer ? std::chrono::nanoseconds{MarkerPollInterval} : TickPollInterval;
    while (!stoken.stop_requested()) {
        if (IsMarkerReached(signal.slot, signal.marker) ||
            semaphore->WaitFor(signal.tick, poll_interval)) {
            return true;
        }
    }
    return false;
}

bool LabelSignaler::IsMarkerReached(MarkerSlot slot, u32 marker) {
    if (!marker_buffer) {
        return false;
    }
    marker_buffer->Invalidate(0, marker_buffer->SizeBytes());
    auto* markers = reinterpret_cast<u32*>(marker_buffer->mapped_data.data());
    const auto is_reached = [&](MarkerSlot reached_slot) {
        const u32 value = std::atomic_ref{markers[reached_slot]}.load(std::memory_order_acquire);
        return static_cast<s32>(value - marker) >= 0;
    };
    return is_reached(slot) || is_reached(MarkerSlot::AllCommands);
}

} // namespace Vulkan
