// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <mutex>
#include <tsl/robin_map.h>
#include <boost/container/small_vector.hpp>
#include "common/div_ceil.h"
#include "common/slot_vector.h"
#include "common/types.h"
#include "video_core/buffer_cache/buffer.h"
#include "video_core/buffer_cache/memory_tracker_base.h"
#include "video_core/renderer_vulkan/vk_stream_buffer.h"

namespace Shader {
struct Info;
}

namespace VideoCore {

using BufferId = Common::SlotId;

static constexpr BufferId NULL_BUFFER_ID{0};

static constexpr u32 NUM_VERTEX_BUFFERS = 32;

class BufferCache {
public:
    static constexpr u32 CACHING_PAGEBITS = 14;
    static constexpr u64 CACHING_PAGESIZE = u64{1} << CACHING_PAGEBITS;
    static constexpr u64 DEVICE_PAGESIZE = 4_KB;

    struct OverlapResult {
        boost::container::small_vector<BufferId, 16> ids;
        VAddr begin;
        VAddr end;
        bool has_stream_leap = false;
    };

public:
    explicit BufferCache(const Vulkan::Instance& instance, Vulkan::Scheduler& scheduler,
                         PageManager& tracker);
    ~BufferCache();

    /// Invalidates any buffer in the logical page range.
    bool InvalidateMemory(VAddr device_addr, u64 size);

    /// Downloads any GPU modified memory to the host in the specified region.
    void DownloadMemory(VAddr device_addr, u64 size);

    /// Binds host vertex buffers for the current draw.
    bool BindVertexBuffers(const Shader::Info& vs_info);

    void BindIndexBuffer();

    /// Obtains a buffer for the specified region.
    [[nodiscard]] std::pair<Buffer*, u32> ObtainBuffer(VAddr gpu_addr, u32 size, bool sync_buffer,
                                                       bool is_written);

    /// Return true when a region is registered on the cache
    [[nodiscard]] bool IsRegionRegistered(VAddr addr, size_t size);

    /// Return true when a CPU region is modified from the CPU
    [[nodiscard]] bool IsRegionCpuModified(VAddr addr, size_t size);

private:
    template <typename Func>
    void ForEachBufferInRange(VAddr device_addr, u64 size, Func&& func) {
        const u64 page_end = Common::DivCeil(device_addr + size, CACHING_PAGESIZE);
        for (u64 page = device_addr >> CACHING_PAGEBITS; page < page_end;) {
            const BufferId buffer_id = page_table[page];
            if (!buffer_id) {
                ++page;
                continue;
            }
            Buffer& buffer = slot_buffers[buffer_id];
            func(buffer_id, buffer);

            const VAddr end_addr = buffer.CpuAddr() + buffer.SizeBytes();
            page = Common::DivCeil(end_addr, CACHING_PAGESIZE);
        }
    }

    void DownloadBufferMemory(Buffer& buffer, VAddr device_addr, u64 size);

    [[nodiscard]] BufferId FindBuffer(VAddr device_addr, u32 size);

    [[nodiscard]] OverlapResult ResolveOverlaps(VAddr device_addr, u32 wanted_size);

    void JoinOverlap(BufferId new_buffer_id, BufferId overlap_id, bool accumulate_stream_score);

    [[nodiscard]] BufferId CreateBuffer(VAddr device_addr, u32 wanted_size);

    void Register(BufferId buffer_id);

    void Unregister(BufferId buffer_id);

    template <bool insert>
    void ChangeRegister(BufferId buffer_id);

    bool SynchronizeBuffer(Buffer& buffer, VAddr device_addr, u32 size);

    void DeleteBuffer(BufferId buffer_id, bool do_not_mark = false);

    const Vulkan::Instance& instance;
    Vulkan::Scheduler& scheduler;
    Vulkan::StreamBuffer staging_buffer;
    std::recursive_mutex mutex;
    Common::SlotVector<Buffer> slot_buffers;
    MemoryTracker memory_tracker;
    tsl::robin_pg_map<u32, BufferId> page_table;
};

} // namespace VideoCore
