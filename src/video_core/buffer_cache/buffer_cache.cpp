// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include "common/alignment.h"
#include "video_core/buffer_cache/buffer_cache.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"

namespace VideoCore {

static constexpr size_t StagingBufferSize = 64_MB;

BufferCache::BufferCache(const Vulkan::Instance& instance_,
                         Vulkan::Scheduler& scheduler_, PageManager* tracker)
    : instance{instance_}, scheduler{scheduler_},
      staging_buffer{instance, scheduler, vk::BufferUsageFlagBits::eTransferSrc,
                     StagingBufferSize, Vulkan::BufferType::Upload},
      memory_tracker{tracker} {
    // Ensure the first slot is used for the null buffer
    void(slot_buffers.insert(instance, 1));
}

BufferCache::~BufferCache() = default;

void BufferCache::WriteMemory(VAddr device_addr, u64 size) {
    if (memory_tracker.IsRegionGpuModified(device_addr, size)) {
        LOG_WARNING(Render_Vulkan, "Writing to GPU modified memory from CPU");
    }
    memory_tracker.MarkRegionAsCpuModified(device_addr, size);
}

bool BufferCache::OnCpuWrite(VAddr device_addr, u64 size) {
    const bool is_tracked = IsRegionRegistered(device_addr, size);
    if (!is_tracked) {
        return false;
    }
    if (memory_tracker.IsRegionGpuModified(device_addr, size)) {
        return true;
    }
    WriteMemory(device_addr, size);
    return false;
}

void BufferCache::DownloadMemory(VAddr device_addr, u64 size) {
    ForEachBufferInRange(device_addr, size, [&](BufferId, Buffer& buffer) {
        DownloadBufferMemory(buffer, device_addr, size);
    });
}

void BufferCache::DownloadBufferMemory(Buffer& buffer, VAddr device_addr, u64 size) {
    boost::container::small_vector<vk::BufferCopy, 1> copies;
    u64 total_size_bytes = 0;
    u64 largest_copy = 0;
    memory_tracker.ForEachDownloadRange<true>(
        device_addr, size, [&](u64 device_addr_out, u64 range_size) {
            const VAddr buffer_addr = buffer.CpuAddr();
            const auto add_download = [&](VAddr start, VAddr end) {
                const u64 new_offset = start - buffer_addr;
                const u64 new_size = end - start;
                copies.push_back(vk::BufferCopy{
                    .srcOffset = new_offset,
                    .dstOffset = total_size_bytes,
                    .size = new_size,
                });
                // Align up to avoid cache conflicts
                constexpr u64 align = 64ULL;
                constexpr u64 mask = ~(align - 1ULL);
                total_size_bytes += (new_size + align - 1) & mask;
                largest_copy = std::max(largest_copy, new_size);
            };
        });
    if (total_size_bytes == 0) {
        return;
    }

    const auto [staging, offset, _] = staging_buffer.Map(total_size_bytes);
    for (auto& copy : copies) {
        // Modify copies to have the staging offset in mind
        copy.dstOffset += offset;
    }
    scheduler.EndRendering();
    const auto cmdbuf = scheduler.CommandBuffer();
    cmdbuf.copyBuffer(buffer.buffer, staging_buffer.Handle(), copies);
    scheduler.Finish();
    for (const auto& copy : copies) {
        const VAddr copy_device_addr = buffer.CpuAddr() + copy.srcOffset;
        const u64 dst_offset = copy.dstOffset - offset;
        std::memcpy(std::bit_cast<u8*>(copy_device_addr), staging + dst_offset, copy.size);
    }
}

std::pair<Buffer*, u32> BufferCache::ObtainBuffer(
    VAddr device_addr, u32 size, bool sync_buffer, bool is_written) {
    const BufferId buffer_id = FindBuffer(device_addr, size);
    Buffer& buffer = slot_buffers[buffer_id];
    if (sync_buffer) {
        SynchronizeBuffer(buffer, device_addr, size);
    }
    if (is_written) {
        memory_tracker.MarkRegionAsGpuModified(device_addr, size);
    }
    return {&buffer, buffer.Offset(device_addr)};
}

bool BufferCache::IsRegionRegistered(VAddr addr, size_t size) {
    const VAddr end_addr = addr + size;
    const u64 page_end = Common::DivCeil(end_addr, CACHING_PAGESIZE);
    for (u64 page = addr >> CACHING_PAGEBITS; page < page_end;) {
        const BufferId buffer_id = page_table[page];
        if (!buffer_id) {
            ++page;
            continue;
        }
        Buffer& buffer = slot_buffers[buffer_id];
        const VAddr buf_start_addr = buffer.CpuAddr();
        const VAddr buf_end_addr = buf_start_addr + buffer.SizeBytes();
        if (buf_start_addr < end_addr && addr < buf_end_addr) {
            return true;
        }
        page = Common::DivCeil(end_addr, CACHING_PAGESIZE);
    }
    return false;
}

bool BufferCache::IsRegionCpuModified(VAddr addr, size_t size) {
    return memory_tracker.IsRegionCpuModified(addr, size);
}

BufferId BufferCache::FindBuffer(VAddr device_addr, u32 size) {
    if (device_addr == 0) {
        return NULL_BUFFER_ID;
    }
    const u64 page = device_addr >> CACHING_PAGEBITS;
    const BufferId buffer_id = page_table[page];
    if (!buffer_id) {
        return CreateBuffer(device_addr, size);
    }
    const Buffer& buffer = slot_buffers[buffer_id];
    if (buffer.IsInBounds(device_addr, size)) {
        return buffer_id;
    }
    return CreateBuffer(device_addr, size);
}

BufferCache::OverlapResult BufferCache::ResolveOverlaps(VAddr device_addr, u32 wanted_size) {
    static constexpr int STREAM_LEAP_THRESHOLD = 16;
    boost::container::small_vector<BufferId, 16> overlap_ids;
    VAddr begin = device_addr;
    VAddr end = device_addr + wanted_size;
    int stream_score = 0;
    bool has_stream_leap = false;
    const auto expand_begin = [&](VAddr add_value) {
        static constexpr VAddr min_page = CACHING_PAGESIZE + DEVICE_PAGESIZE;
        if (add_value > begin - min_page) {
            begin = min_page;
            device_addr = DEVICE_PAGESIZE;
            return;
        }
        begin -= add_value;
        device_addr = begin - CACHING_PAGESIZE;
    };
    const auto expand_end = [&](VAddr add_value) {
        static constexpr VAddr max_page = 1ULL << MemoryTracker::MAX_CPU_PAGE_BITS;
        if (add_value > max_page - end) {
            end = max_page;
            return;
        }
        end += add_value;
    };
    if (begin == 0) {
        return OverlapResult{
            .ids = std::move(overlap_ids),
            .begin = begin,
            .end = end,
            .has_stream_leap = has_stream_leap,
        };
    }
    for (; device_addr >> CACHING_PAGEBITS < Common::DivCeil(end, CACHING_PAGESIZE);
         device_addr += CACHING_PAGESIZE) {
        const BufferId overlap_id = page_table[device_addr >> CACHING_PAGEBITS];
        if (!overlap_id) {
            continue;
        }
        Buffer& overlap = slot_buffers[overlap_id];
        if (overlap.is_picked) {
            continue;
        }
        overlap_ids.push_back(overlap_id);
        overlap.is_picked = true;
        const VAddr overlap_device_addr = overlap.CpuAddr();
        const bool expands_left = overlap_device_addr < begin;
        if (expands_left) {
            begin = overlap_device_addr;
        }
        const VAddr overlap_end = overlap_device_addr + overlap.SizeBytes();
        const bool expands_right = overlap_end > end;
        if (overlap_end > end) {
            end = overlap_end;
        }
        stream_score += overlap.StreamScore();
        if (stream_score > STREAM_LEAP_THRESHOLD && !has_stream_leap) {
            // When this memory region has been joined a bunch of times, we assume it's being used
            // as a stream buffer. Increase the size to skip constantly recreating buffers.
            has_stream_leap = true;
            if (expands_right) {
                expand_begin(CACHING_PAGESIZE * 128);
            }
            if (expands_left) {
                expand_end(CACHING_PAGESIZE * 128);
            }
        }
    }
    return OverlapResult{
        .ids = std::move(overlap_ids),
        .begin = begin,
        .end = end,
        .has_stream_leap = has_stream_leap,
    };
}

void BufferCache::JoinOverlap(BufferId new_buffer_id, BufferId overlap_id,
                              bool accumulate_stream_score) {
    Buffer& new_buffer = slot_buffers[new_buffer_id];
    Buffer& overlap = slot_buffers[overlap_id];
    if (accumulate_stream_score) {
        new_buffer.IncreaseStreamScore(overlap.StreamScore() + 1);
    }
    const size_t dst_base_offset = overlap.CpuAddr() - new_buffer.CpuAddr();
    const vk::BufferCopy copy = {
        .srcOffset = 0,
        .dstOffset = dst_base_offset,
        .size = overlap.SizeBytes(),
    };
    scheduler.EndRendering();
    const auto cmdbuf = scheduler.CommandBuffer();
    cmdbuf.copyBuffer(overlap.buffer, new_buffer.buffer, copy);
    DeleteBuffer(overlap_id, true);
}

BufferId BufferCache::CreateBuffer(VAddr device_addr, u32 wanted_size) {
    VAddr device_addr_end = Common::AlignUp(device_addr + wanted_size, CACHING_PAGESIZE);
    device_addr = Common::AlignDown(device_addr, CACHING_PAGESIZE);
    wanted_size = static_cast<u32>(device_addr_end - device_addr);
    const OverlapResult overlap = ResolveOverlaps(device_addr, wanted_size);
    const u32 size = static_cast<u32>(overlap.end - overlap.begin);
    const BufferId new_buffer_id = slot_buffers.insert(instance, overlap.begin, size);
    auto& new_buffer = slot_buffers[new_buffer_id];
    const size_t size_bytes = new_buffer.SizeBytes();
    for (const BufferId overlap_id : overlap.ids) {
        JoinOverlap(new_buffer_id, overlap_id, !overlap.has_stream_leap);
    }
    Register(new_buffer_id);
    return new_buffer_id;
}

void BufferCache::Register(BufferId buffer_id) {
    ChangeRegister<true>(buffer_id);
}

void BufferCache::Unregister(BufferId buffer_id) {
    ChangeRegister<false>(buffer_id);
}

template <bool insert>
void BufferCache::ChangeRegister(BufferId buffer_id) {
    Buffer& buffer = slot_buffers[buffer_id];
    const auto size = buffer.SizeBytes();
    const VAddr device_addr_begin = buffer.CpuAddr();
    const VAddr device_addr_end = device_addr_begin + size;
    const u64 page_begin = device_addr_begin / CACHING_PAGESIZE;
    const u64 page_end = Common::DivCeil(device_addr_end, CACHING_PAGESIZE);
    for (u64 page = page_begin; page != page_end; ++page) {
        if constexpr (insert) {
            page_table[page] = buffer_id;
        } else {
            page_table[page] = BufferId{};
        }
    }
}

bool BufferCache::SynchronizeBuffer(Buffer& buffer, VAddr device_addr, u32 size) {
    boost::container::small_vector<vk::BufferCopy, 4> copies;
    u64 total_size_bytes = 0;
    u64 largest_copy = 0;
    VAddr buffer_start = buffer.CpuAddr();
    memory_tracker.ForEachUploadRange(device_addr, size, [&](u64 device_addr_out, u64 range_size) {
        copies.push_back(vk::BufferCopy{
            .srcOffset = total_size_bytes,
            .dstOffset = device_addr_out - buffer_start,
            .size = range_size,
        });
        total_size_bytes += range_size;
        largest_copy = std::max(largest_copy, range_size);
    });
    if (total_size_bytes == 0) {
        return true;
    }
    const auto [staging, offset, _] = staging_buffer.Map(total_size_bytes);
    for (auto& copy : copies) {
        u8* const src_pointer = staging + copy.srcOffset;
        const VAddr device_addr = buffer.CpuAddr() + copy.dstOffset;
        std::memcpy(src_pointer, std::bit_cast<const u8*>(device_addr), copy.size);
        // Apply the staging offset
        copy.srcOffset += offset;
    }
    staging_buffer.Commit(total_size_bytes);
    scheduler.EndRendering();
    const auto cmdbuf = scheduler.CommandBuffer();
    cmdbuf.copyBuffer(staging_buffer.Handle(), buffer.buffer, copies);
    return false;
}

void BufferCache::DeleteBuffer(BufferId buffer_id, bool do_not_mark) {
    // Mark the whole buffer as CPU written to stop tracking CPU writes
    if (!do_not_mark) {
        Buffer& buffer = slot_buffers[buffer_id];
        memory_tracker.MarkRegionAsCpuModified(buffer.CpuAddr(), buffer.SizeBytes());
    }
    Unregister(buffer_id);
    slot_buffers.erase(buffer_id);
}

} // namespace VideoCore
