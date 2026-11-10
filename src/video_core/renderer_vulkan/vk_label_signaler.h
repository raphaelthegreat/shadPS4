// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <optional>
#include <thread>

#include "common/types.h"
#include "common/unique_function.h"
#include "video_core/amdgpu/pm4_cmds.h"
#include "video_core/buffer_cache/buffer.h"

namespace Vulkan {

class Instance;
class Scheduler;

/// Defers guest visible label writes and interrupts until the host GPU is finished
class LabelSignaler {
public:
    explicit LabelSignaler(const Instance& instance, Scheduler& scheduler);
    ~LabelSignaler();

    /// Runs func on the signaler thread once the host GPU pipeline stage reaches current work.
    void Signal(AmdGpu::EventType event_type, AmdGpu::EventIndex event_index,
                Common::UniqueFunction<void>&& func,
                std::optional<AmdGpu::LabelWrite> label = std::nullopt);

    /// Tests the dword at address against the most recent pending label write covering it.
    std::optional<bool> TestPendingLabel(VAddr address, auto&& pred) {
        std::scoped_lock lk{mutex};
        for (auto it = pending_signals.rbegin(); it != pending_signals.rend(); ++it) {
            const auto& label = it->label;
            if (!label || address < label->address ||
                address >= label->address + label->num_bytes) {
                continue;
            }
            const u32 shift = (address - label->address) * 8;
            return label->value && pred(static_cast<u32>(*label->value >> shift));
        }
        return std::nullopt;
    }

    /// Returns true if signals were recorded in the command buffer that is not submitted yet.
    [[nodiscard]] bool NeedsFlush() const noexcept;

    /// Waits until a pending signal is applied or the timeout expires.
    void WaitForProgress(std::chrono::microseconds timeout);

    /// Returns the marker of the most recent signal.
    [[nodiscard]] u32 CurrentMarker() const noexcept {
        return current_marker;
    }

    /// Returns true if all signals up to and including marker have been applied.
    [[nodiscard]] bool IsRetired(u32 marker) const noexcept {
        return static_cast<s32>(retired_marker.load(std::memory_order_acquire) - marker) >= 0;
    }

private:
    enum MarkerSlot : u32 {
        AllCommands,
        ComputeShader,
        FragmentShader,
        NumMarkerSlots,
    };

    struct PendingSignal {
        Common::UniqueFunction<void> func;
        u64 tick;
        u32 marker;
        MarkerSlot slot;
        std::optional<AmdGpu::LabelWrite> label;
    };

    void SignalThread(std::stop_token stoken);

    bool WaitSignal(const PendingSignal& signal, std::stop_token stoken);

    bool IsMarkerReached(MarkerSlot slot, u32 marker);

private:
    Scheduler& scheduler;
    std::optional<VideoCore::Buffer> marker_buffer;
    u32 current_marker{};
    u64 last_signal_tick{};
    std::mutex mutex;
    std::condition_variable_any signal_cv;
    std::condition_variable progress_cv;
    std::deque<PendingSignal> pending_signals;
    std::atomic<u32> retired_marker{};
    std::jthread signal_thread;
};

} // namespace Vulkan
