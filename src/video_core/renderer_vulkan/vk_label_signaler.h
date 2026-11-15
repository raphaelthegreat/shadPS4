// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <optional>
#include <thread>

#include "common/types.h"
#include "common/unique_function.h"
#include "video_core/amdgpu/pm4_cmds.h"
#include "video_core/renderer_vulkan/vk_common.h"

namespace Vulkan {

class Instance;
class Scheduler;

/// Defers guest visible label writes and interrupts until the host GPU is finished.
class LabelSignaler {
public:
    explicit LabelSignaler(const Instance& instance, Scheduler& scheduler);

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

    /// Waits until the next pending signal is applied, returns immediately if none are pending.
    void WaitForProgress();

    /// Returns the marker of the most recent signal.
    [[nodiscard]] u32 CurrentMarker() const noexcept {
        return write_slot;
    }

    /// Returns true if all signals up to and including marker have been applied.
    [[nodiscard]] bool IsRetired(u32 marker) const noexcept {
        return static_cast<s32>(read_slot.load(std::memory_order_acquire) - marker) >= 0;
    }

private:
    struct PendingSignal {
        Common::UniqueFunction<void> func;
        u64 tick;
        u32 marker;
        std::optional<AmdGpu::LabelWrite> label;
    };

    void SignalThread(std::stop_token stoken);

    bool WaitSignal(const PendingSignal& signal, std::stop_token stoken);

    void WaitFreeQuery(u32 marker);

private:
    Scheduler& scheduler;
    const bool host_gpu_sync;
    vk::Device device;
    vk::UniqueQueryPool query_pool;
    u32 write_slot{};
    u64 last_signal_tick{};
    std::mutex mutex;
    std::condition_variable_any signal_cv;
    std::deque<PendingSignal> pending_signals;
    std::atomic<u32> read_slot{};
    std::jthread signal_thread;
};

} // namespace Vulkan
