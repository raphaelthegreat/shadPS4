// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <utility>

#include "common/assert.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_label_signaler.h"
#include "video_core/renderer_vulkan/vk_occlusion_queries.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"

namespace Vulkan {

constexpr u32 NumQueries = 4096;
constexpr u32 SubmitThreshold = 256;
constexpr std::chrono::milliseconds SlotWaitTimeout{1};

OcclusionQueries::OcclusionQueries(const Instance& instance, Scheduler& scheduler_,
                                   LabelSignaler& label_signaler_)
    : device{instance.GetDevice()}, scheduler{scheduler_}, label_signaler{label_signaler_},
      precise_supported{instance.IsOcclusionQueryPreciseSupported()} {
    const vk::QueryPoolCreateInfo pool_ci = {
        .queryType = vk::QueryType::eOcclusion,
        .queryCount = NumQueries,
    };
    auto [pool_result, pool] = device.createQueryPoolUnique(pool_ci);
    ASSERT_MSG(pool_result == vk::Result::eSuccess, "Failed to create occlusion query pool: {}",
               vk::to_string(pool_result));
    query_pool = std::move(pool);
    device.resetQueryPool(*query_pool, 0, NumQueries);
    results.resize(NumQueries);
}

OcclusionQueries::~OcclusionQueries() = default;

void OcclusionQueries::Update(const AmdGpu::DepthCountControl& control) {
    enabled = control.IsZPassCountEnabled();
    query_flags = enabled && control.perfect_zpass_counts && precise_supported
                      ? vk::QueryControlFlagBits::ePrecise
                      : vk::QueryControlFlags{};
    if (active && (!enabled || query_flags != active_flags)) {
        End();
    }
    const u64 ended_query = active ? next_query - 1 : next_query;
    if (ended_query - submitted_query >= SubmitThreshold) {
        SubmitEnded({}, std::nullopt);
    }
    if (!enabled) {
        return;
    }
    while (next_query - retired_query.load(std::memory_order_acquire) >= NumQueries) {
        if (label_signaler.NeedsFlush()) {
            scheduler.Flush();
        }
        label_signaler.WaitForProgress(SlotWaitTimeout);
    }
}

void OcclusionQueries::Begin() {
    if (!enabled || active) {
        return;
    }
    ASSERT(next_query - retired_query.load(std::memory_order_acquire) < NumQueries);
    const u32 slot = next_query++ % NumQueries;
    scheduler.CommandBuffer().beginQuery(*query_pool, slot, query_flags);
    active_flags = query_flags;
    active = true;
}

void OcclusionQueries::End() {
    if (!active) {
        return;
    }
    const u32 slot = (next_query - 1) % NumQueries;
    scheduler.CommandBuffer().endQuery(*query_pool, slot);
    active = false;
}

void OcclusionQueries::Dump(const AmdGpu::LabelWrite& label,
                            Common::UniqueFunction<void, u64>&& func) {
    End();
    SubmitEnded(std::move(func), label);
}

void OcclusionQueries::SubmitEnded(Common::UniqueFunction<void, u64>&& func,
                                   std::optional<AmdGpu::LabelWrite> label) {
    const u64 last = active ? next_query - 1 : next_query;
    const u64 first = std::exchange(submitted_query, last);
    label_signaler.Signal(
        AmdGpu::EventType::PixelPipeStatDump, AmdGpu::EventIndex::ZpassDone,
        [this, first, last, func = std::move(func)] {
            Accumulate(first, last);
            if (func) {
                func(u64{total});
            }
        },
        label);
}

void OcclusionQueries::Accumulate(u64 first, u64 last) {
    while (first < last) {
        const u32 slot = first % NumQueries;
        const u32 count = static_cast<u32>(std::min<u64>(last - first, NumQueries - slot));
        const auto result = device.getQueryPoolResults(
            *query_pool, slot, count, count * sizeof(u64), results.data(), sizeof(u64),
            vk::QueryResultFlagBits::e64 | vk::QueryResultFlagBits::eWait);
        ASSERT_MSG(result == vk::Result::eSuccess, "Failed to get occlusion query results: {}",
                   vk::to_string(result));
        for (u32 i = 0; i < count; ++i) {
            total += results[i];
        }
        device.resetQueryPool(*query_pool, slot, count);
        first += count;
    }
    retired_query.store(last, std::memory_order_release);
}

} // namespace Vulkan
