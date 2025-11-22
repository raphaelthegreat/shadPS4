// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include "common/alignment.h"
#include "common/debug.h"
#include "common/scope_exit.h"
#include "core/memory.h"
#include "video_core/amdgpu/liverpool.h"
#include "video_core/buffer_cache/buffer_cache.h"
#include "video_core/buffer_cache/memory_tracker.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_runtime.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/texture_cache/texture_cache.h"

namespace VideoCore {

static constexpr size_t DataShareBufferSize = 64_KB;
static constexpr size_t StagingBufferSize = 512_MB;
static constexpr size_t DownloadBufferSize = 32_MB;
static constexpr size_t UboStreamBufferSize = 64_MB;

BufferCache::BufferCache(const Vulkan::Instance& instance_, Vulkan::Scheduler& scheduler_,
                         AmdGpu::Liverpool* liverpool_, TextureCache& texture_cache_,
                         Vulkan::Runtime& res_context_, PageManager& tracker)
    : instance{instance_}, scheduler{scheduler_}, res_context{res_context_}, liverpool{liverpool_},
      memory{Core::Memory::Instance()}, texture_cache{texture_cache_},
      fault_manager{instance, scheduler, *this, CACHING_PAGEBITS, CACHING_NUMPAGES},
      staging_buffer{instance, scheduler, MemoryUsage::Upload, StagingBufferSize},
      stream_buffer{instance, scheduler, MemoryUsage::Stream, UboStreamBufferSize},
      download_buffer{instance, scheduler, MemoryUsage::Download, DownloadBufferSize},
      gds_buffer{instance, scheduler, MemoryUsage::Stream, 0, AllFlags, DataShareBufferSize},
      bda_pagetable_buffer{instance, scheduler, MemoryUsage::DeviceLocal,
                           0,        AllFlags,  BDA_PAGETABLE_SIZE} {
    Vulkan::SetObjectName(instance.GetDevice(), gds_buffer.Handle(), "GDS Buffer");
    Vulkan::SetObjectName(instance.GetDevice(), bda_pagetable_buffer.Handle(),
                          "BDA Page Table Buffer");

    memory_tracker = std::make_unique<MemoryTracker>(tracker);

    std::memset(gds_buffer.mapped_data.data(), 0, DataShareBufferSize);

    // Ensure the first slot is used for the null buffer
    const auto null_id =
        slot_buffers.insert(instance, scheduler, MemoryUsage::DeviceLocal, 0, AllFlags, 16);
    ASSERT(null_id.index == 0);
    const vk::Buffer& null_buffer = slot_buffers[null_id].buffer;
    Vulkan::SetObjectName(instance.GetDevice(), null_buffer, "Null Buffer");

    // Set up garbage collection parameters
    if (!instance.CanReportMemoryUsage()) {
        trigger_gc_memory = DEFAULT_TRIGGER_GC_MEMORY;
        critical_gc_memory = DEFAULT_CRITICAL_GC_MEMORY;
        return;
    }

    const s64 device_local_memory = static_cast<s64>(instance.GetTotalMemoryBudget());
    const s64 min_spacing_expected = device_local_memory - 1_GB;
    const s64 min_spacing_critical = device_local_memory - 512_MB;
    const s64 mem_threshold = std::min<s64>(device_local_memory, TARGET_GC_THRESHOLD);
    const s64 min_vacancy_expected = (6 * mem_threshold) / 10;
    const s64 min_vacancy_critical = (2 * mem_threshold) / 10;
    trigger_gc_memory = static_cast<u64>(
        std::max<u64>(std::min(device_local_memory - min_vacancy_expected, min_spacing_expected),
                      DEFAULT_TRIGGER_GC_MEMORY));
    critical_gc_memory = static_cast<u64>(
        std::max<u64>(std::min(device_local_memory - min_vacancy_critical, min_spacing_critical),
                      DEFAULT_CRITICAL_GC_MEMORY));
}

BufferCache::~BufferCache() = default;

void BufferCache::InvalidateMemory(VAddr device_addr, u64 size) {
    if (!IsRegionRegistered(device_addr, size)) {
        return;
    }
    memory_tracker->InvalidateRegion(
        device_addr, size, [this, device_addr, size] { ReadMemory(device_addr, size, true); });
}

void BufferCache::ReadMemory(VAddr device_addr, u64 size, bool is_write) {
    liverpool->SendCommand<true>([this, device_addr, size, is_write] {
        Buffer& buffer = slot_buffers[FindBuffer(device_addr, size)];
        DownloadBufferMemory<false>(buffer, device_addr, size, is_write);
    });
}

template <bool async>
void BufferCache::DownloadBufferMemory(Buffer& buffer, VAddr device_addr, u64 size, bool is_write) {
    boost::container::small_vector<vk::BufferCopy, 1> copies;
    u64 total_size_bytes = 0;
    memory_tracker->ForEachDownloadRange<false>(
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
            };
            gpu_modified_ranges.ForEachInRange(device_addr_out, range_size, add_download);
            gpu_modified_ranges.Subtract(device_addr_out, range_size);
        });
    if (total_size_bytes == 0) {
        return;
    }
    const auto [download, offset] = download_buffer.Map(total_size_bytes);
    for (auto& copy : copies) {
        // Modify copies to have the staging offset in mind
        copy.dstOffset += offset;
    }
    download_buffer.Commit();
    scheduler.EndRendering();
    const auto cmdbuf = scheduler.CommandBuffer();
    cmdbuf.copyBuffer(buffer.buffer, download_buffer.Handle(), copies);
    const auto write_data = [&]() {
        auto* memory = Core::Memory::Instance();
        for (const auto& copy : copies) {
            const VAddr copy_device_addr = buffer.CpuAddr() + copy.srcOffset;
            const u64 dst_offset = copy.dstOffset - offset;
            memory->TryWriteBacking(std::bit_cast<u8*>(copy_device_addr), download + dst_offset,
                                    copy.size);
        }
        memory_tracker->UnmarkRegionAsGpuModified(device_addr, size);
        if (is_write) {
            memory_tracker->MarkRegionAsCpuModified(device_addr, size);
        }
    };
    if constexpr (async) {
        scheduler.DeferOperation(write_data);
    } else {
        scheduler.Finish();
        write_data();
    }
}

void BufferCache::FillBuffer(VAddr address, u32 num_bytes, u32 value, bool is_gds) {
    ASSERT_MSG(address % 4 == 0, "GDS offset must be dword aligned");
    if (!is_gds) {
        texture_cache.ClearMeta(address);
        if (!IsRegionGpuModified(address, num_bytes)) {
            u32* buffer = std::bit_cast<u32*>(address);
            std::fill(buffer, buffer + num_bytes / sizeof(u32), value);
        }
    }
    Buffer& buffer = [&] -> Buffer& {
        if (is_gds) {
            return gds_buffer;
        }
        const auto [buffer, offset] = ObtainBuffer(address, num_bytes, true);
        return *buffer;
    }();
    res_context.FillBuffer(buffer, buffer.Offset(address), num_bytes, value);
}

void BufferCache::CopyBuffer(VAddr dst, VAddr src, u32 num_bytes, bool dst_gds, bool src_gds) {
    if (!dst_gds && !IsRegionGpuModified(dst, num_bytes)) {
        if (!src_gds && !IsRegionGpuModified(src, num_bytes) &&
            !texture_cache.FindImageFromRange(src, num_bytes)) {
            // Both buffers were not transferred to GPU yet. Can safely copy in host memory.
            std::memcpy(std::bit_cast<void*>(dst), std::bit_cast<void*>(src), num_bytes);
            return;
        }
        // Fallback to creating dst buffer on GPU to at least have this data there
    }
    texture_cache.InvalidateMemoryFromGPU(dst, num_bytes);
    auto& src_buffer = [&] -> const Buffer& {
        if (src_gds) {
            return gds_buffer;
        }
        const auto buffer_id = FindBuffer(src, num_bytes);
        auto& buffer = slot_buffers[buffer_id];
        SynchronizeBuffer(buffer, src, num_bytes, false, true);
        return buffer;
    }();
    auto& dst_buffer = [&] -> const Buffer& {
        if (dst_gds) {
            return gds_buffer;
        }
        const auto buffer_id = FindBuffer(dst, num_bytes);
        auto& buffer = slot_buffers[buffer_id];
        SynchronizeBuffer(buffer, dst, num_bytes, true, true);
        gpu_modified_ranges.Add(dst, num_bytes);
        return buffer;
    }();
    const vk::BufferCopy region = {
        .srcOffset = src_buffer.Offset(src),
        .dstOffset = dst_buffer.Offset(dst),
        .size = num_bytes,
    };
    res_context.CopyBuffer(src_buffer, dst_buffer, region);
}

std::pair<Buffer*, u32> BufferCache::ObtainBuffer(VAddr device_addr, u32 size, bool is_written,
                                                  bool is_texel_buffer, BufferId buffer_id) {
    // For read-only buffers use device local stream buffer to reduce renderpass breaks.
    if (!is_written && size <= CACHING_PAGESIZE && !IsRegionGpuModified(device_addr, size)) {
        const u64 offset = stream_buffer.Copy(device_addr, size, instance.UniformMinAlignment());
        return {&stream_buffer, offset};
    }
    if (IsBufferInvalid(buffer_id)) {
        buffer_id = FindBuffer(device_addr, size);
    }
    Buffer& buffer = slot_buffers[buffer_id];
    SynchronizeBuffer(buffer, device_addr, size, is_written, is_texel_buffer);
    if (is_written) {
        gpu_modified_ranges.Add(device_addr, size);
    }
    return {&buffer, buffer.Offset(device_addr)};
}

std::pair<Buffer*, u32> BufferCache::ObtainBufferForImage(VAddr gpu_addr, u32 size) {
    // Check if any buffer contains the full requested range.
    const BufferId buffer_id = page_table[gpu_addr >> CACHING_PAGEBITS].buffer_id;
    if (buffer_id) {
        if (Buffer& buffer = slot_buffers[buffer_id]; buffer.IsInBounds(gpu_addr, size)) {
            SynchronizeBuffer(buffer, gpu_addr, size, false, false);
            return {&buffer, buffer.Offset(gpu_addr)};
        }
    }
    // If some buffer within was GPU modified create a full buffer to avoid losing GPU data.
    if (IsRegionGpuModified(gpu_addr, size)) {
        return ObtainBuffer(gpu_addr, size, false, false);
    }
    // In all other cases, just do a CPU copy to the staging buffer.
    const auto [data, offset] = staging_buffer.Map(size, 16);
    memory->CopySparseMemory(gpu_addr, data, size);
    staging_buffer.Commit();
    return {&staging_buffer, offset};
}

bool BufferCache::IsRegionRegistered(VAddr addr, size_t size) {
    // Check if we are missing some edge case here
    return buffer_ranges.Intersects(addr, size);
}

bool BufferCache::IsRegionCpuModified(VAddr addr, size_t size) {
    return memory_tracker->IsRegionCpuModified(addr, size);
}

bool BufferCache::IsRegionGpuModified(VAddr addr, size_t size) {
    return memory_tracker->IsRegionGpuModified(addr, size);
}

BufferId BufferCache::FindBuffer(VAddr device_addr, u32 size) {
    if (device_addr == 0) {
        return NULL_BUFFER_ID;
    }
    const u64 page = device_addr >> CACHING_PAGEBITS;
    const BufferId buffer_id = page_table[page].buffer_id;
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
        const BufferId overlap_id = page_table[device_addr >> CACHING_PAGEBITS].buffer_id;
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
    res_context.CopyBuffer(overlap, new_buffer, copy);
    DeleteBuffer(overlap_id);
}

BufferId BufferCache::CreateBuffer(VAddr device_addr, u32 wanted_size) {
    const VAddr device_addr_end = Common::AlignUp(device_addr + wanted_size, CACHING_PAGESIZE);
    device_addr = Common::AlignDown(device_addr, CACHING_PAGESIZE);
    wanted_size = static_cast<u32>(device_addr_end - device_addr);
    const OverlapResult overlap = ResolveOverlaps(device_addr, wanted_size);
    const u32 size = static_cast<u32>(overlap.end - overlap.begin);
    const BufferId new_buffer_id =
        slot_buffers.insert(instance, scheduler, MemoryUsage::DeviceLocal, overlap.begin,
                            AllFlags | vk::BufferUsageFlagBits::eShaderDeviceAddress, size);
    auto& new_buffer = slot_buffers[new_buffer_id];
    for (const BufferId overlap_id : overlap.ids) {
        JoinOverlap(new_buffer_id, overlap_id, !overlap.has_stream_leap);
    }
    Register(new_buffer_id);
    return new_buffer_id;
}

void BufferCache::ProcessFaultBuffer() {
    fault_manager.ProcessFaultBuffer();
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
    const u64 size_pages = page_end - page_begin;
    for (u64 page = page_begin; page != page_end; ++page) {
        if constexpr (insert) {
            page_table[page].buffer_id = buffer_id;
        } else {
            page_table[page].buffer_id = BufferId{};
        }
    }
    if constexpr (insert) {
        total_used_memory += Common::AlignUp(size, CACHING_PAGESIZE);
        buffer.SetLRUId(lru_cache.Insert(buffer_id, gc_tick));
        const auto copy_size = size_pages * sizeof(vk::DeviceAddress);
        ASSERT(copy_size < StagingBufferSize);
        const auto [staging, offset] = staging_buffer.Map(copy_size);
        auto* bda_addrs = reinterpret_cast<vk::DeviceAddress*>(staging);
        for (u64 i = 0; i < size_pages; ++i) {
            bda_addrs[i] = buffer.BufferDeviceAddress() + (i << CACHING_PAGEBITS);
        }
        staging_buffer.Commit();
        const vk::BufferCopy copy = {
            .srcOffset = offset,
            .dstOffset = page_begin * sizeof(vk::DeviceAddress),
            .size = copy_size,
        };
        res_context.CopyBuffer(staging_buffer, bda_pagetable_buffer, copy);
        buffer_ranges.Add(buffer.CpuAddr(), buffer.SizeBytes(), buffer_id);
    } else {
        total_used_memory -= Common::AlignUp(size, CACHING_PAGESIZE);
        lru_cache.Free(buffer.LRUId());
        const u64 offset = bda_pagetable_buffer.Offset(page_begin * sizeof(vk::DeviceAddress));
        res_context.FillBuffer(bda_pagetable_buffer, offset, size_pages * sizeof(vk::DeviceAddress),
                               0);
        buffer_ranges.Subtract(buffer.CpuAddr(), buffer.SizeBytes());
    }
}

bool BufferCache::SynchronizeBuffer(Buffer& buffer, VAddr device_addr, u32 size, bool is_written,
                                    bool is_texel_buffer) {
    boost::container::small_vector<vk::BufferCopy, 4> copies;
    size_t total_size_bytes = 0;
    VAddr buffer_start = buffer.CpuAddr();
    Buffer* src_buffer = nullptr;
    memory_tracker->ForEachUploadRange(
        device_addr, size, is_written,
        [&](u64 device_addr_out, u64 range_size) {
            copies.emplace_back(total_size_bytes, device_addr_out - buffer_start, range_size);
            total_size_bytes += range_size;
        },
        [&] { src_buffer = UploadStaging(buffer, copies, total_size_bytes); });

    if (src_buffer) {
        res_context.CopyBuffer(*src_buffer, buffer, copies);
        TouchBuffer(buffer);
    }
    if (is_texel_buffer && !is_written) {
        return SynchronizeBufferFromImage(buffer, device_addr, size);
    }
    return false;
}

Buffer* BufferCache::UploadStaging(Buffer& buffer, std::span<vk::BufferCopy> copies,
                                   size_t total_size_bytes) {
    if (copies.empty()) {
        return nullptr;
    }
    const auto [staging, offset] = staging_buffer.Map(total_size_bytes);
    if (staging) {
        for (auto& copy : copies) {
            u8* const src_pointer = staging + copy.srcOffset;
            const VAddr device_addr = buffer.CpuAddr() + copy.dstOffset;
            memory->CopySparseMemory(device_addr, src_pointer, copy.size);
            // Apply the staging offset
            copy.srcOffset += offset;
        }
        staging_buffer.Commit();
        return &staging_buffer;
    } else {
        // For large one time transfers use a temporary host buffer.
        auto* temp_buffer = new Buffer(instance, scheduler, MemoryUsage::Upload, 0,
                                       vk::BufferUsageFlagBits::eTransferSrc, total_size_bytes);
        u8* const staging = temp_buffer->mapped_data.data();
        for (const auto& copy : copies) {
            u8* const src_pointer = staging + copy.srcOffset;
            const VAddr device_addr = buffer.CpuAddr() + copy.dstOffset;
            memory->CopySparseMemory(device_addr, src_pointer, copy.size);
        }
        scheduler.DeferOperation([temp_buffer]() { delete temp_buffer; });
        return temp_buffer;
    }
}

bool BufferCache::SynchronizeBufferFromImage(Buffer& buffer, VAddr device_addr, u32 size) {
    const ImageId image_id = texture_cache.FindImageFromRange(device_addr, size);
    if (!image_id) {
        return false;
    }
    Image& image = texture_cache.GetImage(image_id);
    ASSERT_MSG(device_addr == image.info.guest_address,
               "Texel buffer aliases image subresources {:x} : {:x}", device_addr,
               image.info.guest_address);
    const u32 buf_offset = buffer.Offset(image.info.guest_address);
    boost::container::small_vector<vk::BufferImageCopy, 8> buffer_copies;
    u32 copy_size = 0;
    for (u32 mip = 0; mip < image.info.resources.levels; mip++) {
        const auto& mip_info = image.info.mips_layout[mip];
        const u32 width = std::max(image.info.size.width >> mip, 1u);
        const u32 height = std::max(image.info.size.height >> mip, 1u);
        const u32 depth = std::max(image.info.size.depth >> mip, 1u);
        if (buf_offset + mip_info.offset + mip_info.size > buffer.SizeBytes()) {
            break;
        }
        buffer_copies.push_back(vk::BufferImageCopy{
            .bufferOffset = mip_info.offset,
            .bufferRowLength = mip_info.pitch,
            .bufferImageHeight = mip_info.height,
            .imageSubresource{
                .aspectMask = image.aspect_mask & ~vk::ImageAspectFlagBits::eStencil,
                .mipLevel = mip,
                .baseArrayLayer = 0,
                .layerCount = image.info.resources.layers,
            },
            .imageOffset = {0, 0, 0},
            .imageExtent = {width, height, depth},
        });
        copy_size += mip_info.size;
    }
    if (copy_size == 0) {
        return false;
    }
    auto& tile_manager = texture_cache.GetTileManager();
    tile_manager.TileImage(image, buffer_copies, buffer, buf_offset, copy_size);
    return true;
}

void BufferCache::SynchronizeBuffersInRange(VAddr device_addr, u64 size) {
    const VAddr device_addr_end = device_addr + size;
    ForEachBufferInRange(device_addr, size, [&](BufferId buffer_id, Buffer& buffer) {
        RENDERER_TRACE;
        VAddr start = std::max(buffer.CpuAddr(), device_addr);
        VAddr end = std::min(buffer.CpuAddr() + buffer.SizeBytes(), device_addr_end);
        u32 size = static_cast<u32>(end - start);
        SynchronizeBuffer(buffer, start, size, false, false);
    });
}

void BufferCache::RunGarbageCollector() {
    SCOPE_EXIT {
        ++gc_tick;
    };
    if (instance.CanReportMemoryUsage()) {
        total_used_memory = instance.GetDeviceMemoryUsage();
    }
    if (total_used_memory < trigger_gc_memory) {
        return;
    }
    const bool aggressive = total_used_memory >= critical_gc_memory;
    const u64 ticks_to_destroy = std::min<u64>(aggressive ? 80 : 160, gc_tick);
    int max_deletions = aggressive ? 64 : 32;
    const auto clean_up = [&](BufferId buffer_id) {
        if (max_deletions == 0) {
            return;
        }
        --max_deletions;
        Buffer& buffer = slot_buffers[buffer_id];
        // InvalidateMemory(buffer.CpuAddr(), buffer.SizeBytes());
        DownloadBufferMemory<true>(buffer, buffer.CpuAddr(), buffer.SizeBytes(), true);
        DeleteBuffer(buffer_id);
    };
}

void BufferCache::TouchBuffer(const Buffer& buffer) {
    lru_cache.Touch(buffer.LRUId(), gc_tick);
}

void BufferCache::DeleteBuffer(BufferId buffer_id) {
    Buffer& buffer = slot_buffers[buffer_id];
    Unregister(buffer_id);
    scheduler.DeferOperation([this, buffer_id] { slot_buffers.erase(buffer_id); });
    buffer.is_deleted = true;
}

} // namespace VideoCore
