// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <atomic>
#include <vector>

#include "common/types.h"
#include "common/unique_function.h"
#include "video_core/amdgpu/pm4_cmds.h"
#include "video_core/amdgpu/regs_depth.h"
#include "video_core/renderer_vulkan/vk_common.h"

namespace Vulkan {

class Instance;
class Scheduler;
class LabelSignaler;

/// Emulates the guest z-pass sample counter by summing host occlusion queries
class OcclusionQueries {
public:
    explicit OcclusionQueries(const Instance& instance, Scheduler& scheduler,
                              LabelSignaler& label_signaler);
    ~OcclusionQueries();

    /// Applies the guest counter state and reserves a query slot. Must be called outside of
    /// recorded draw state since it may flush the scheduler.
    void Update(const AmdGpu::DepthCountControl& control);

    /// Begins a host query if counting is enabled. Must be called inside a render pass.
    void Begin();

    /// Ends the active host query.
    void End();

    /// Calls func on the signaler thread with the counter value once all prior queries finished.
    void Dump(const AmdGpu::LabelWrite& label, Common::UniqueFunction<void, u64>&& func);

private:
    void SubmitEnded(Common::UniqueFunction<void, u64>&& func,
                     std::optional<AmdGpu::LabelWrite> label);

    void Accumulate(u64 first, u64 last);

private:
    vk::Device device;
    Scheduler& scheduler;
    LabelSignaler& label_signaler;
    const bool precise_supported;
    vk::UniqueQueryPool query_pool;
    vk::QueryControlFlags query_flags{};
    vk::QueryControlFlags active_flags{};
    bool enabled{};
    bool active{};
    u64 next_query{};
    u64 submitted_query{};
    std::atomic<u64> retired_query{};
    u64 total{};
    std::vector<u64> results;
};

} // namespace Vulkan
