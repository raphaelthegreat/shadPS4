// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <deque>
#include <boost/container/small_vector.hpp>

#include "common/enum.h"
#include "common/interval_set.h"
#include "common/types.h"
#include "video_core/buffer_cache/buffer.h"
#include "video_core/buffer_cache/fault_manager.h"
#include "video_core/buffer_cache/region_manager.h"
#include "video_core/gpu_memory.h"
#include "video_core/renderer_vulkan/vk_semaphore.h"
#include "video_core/texture_cache/image.h"

namespace AmdGpu {
struct Liverpool;
}

namespace Core {
class MemoryManager;
}

namespace Vulkan {
class GraphicsPipeline;
struct SubmitInfo;
class Runtime;
class StagingBufferPool;
struct StagingBufferRef;
} // namespace Vulkan

namespace VideoCore {

class TextureCache;
class GpuMemory;

/// Buffers all share one owner identity. Nothing reads back a single buffer in isolation, and a
/// range handed from one arena to another is still the same bytes, so telling them apart would
/// only keep runs from merging.
static constexpr Owner BufferOwner{OwnerKind::Buffer, 0};

enum class ObtainBufferFlags {
    None = 0,
    IsWritten = 1 << 0,
    IgnoreStreamBuffer = 1 << 2,
    InvalidateTextureCache = 1 << 3,
};
DECLARE_ENUM_FLAG_OPERATORS(ObtainBufferFlags)

class BufferCache {
    static constexpr u64 ADDRESS_SPACE_BITS = 40;
    static constexpr u64 ARENA_PAGE_BITS = 32;
    static constexpr u64 ARENA_PAGE_SIZE = u64{1} << ARENA_PAGE_BITS;
    static constexpr u64 NUM_ARENA_PAGES = u64{1} << (ADDRESS_SPACE_BITS - ARENA_PAGE_BITS);
    static constexpr u64 MIN_BLOCK_SIZE = 16_KB;
    static constexpr u64 STREAM_THRESHOLD = 16_KB;
    static constexpr u64 NUM_REGIONS = u64{1} << (ADDRESS_SPACE_BITS - HIGHER_PAGE_BITS);
    static constexpr u64 REGION_POOL_SIZE = 32;

public:
    explicit BufferCache(const Vulkan::Instance& instance, Vulkan::Scheduler& scheduler,
                         Vulkan::Runtime& runtime, AmdGpu::Liverpool* liverpool,
                         TextureCache& texture_cache, GpuMemory& gpu_memory);
    ~BufferCache();

    /// Returns a pointer to GDS device local buffer.
    [[nodiscard]] const Buffer* GetGdsBuffer() const noexcept {
        return &gds_buffer;
    }

    [[nodiscard]] const Buffer* GetNullIndexBuffer() const noexcept {
        return null_index_buffer.get();
    }

    /// Retrieves the device local DBA page table buffer.
    [[nodiscard]] Buffer* GetBdaPageTableBuffer() noexcept {
        return bda_pagetable_buffer.get();
    }

    /// Retrieves the fault buffer.
    [[nodiscard]] Buffer* GetFaultBuffer() noexcept {
        return fault_manager->GetFaultBuffer();
    }

    /// Retrieves the stream buffer.
    [[nodiscard]] StreamBuffer& GetStreamBuffer() noexcept {
        return stream_buffer;
    }

    /// Returns minimum granularity of a sparse memory bind.
    u32 GetSparsePageShift() const noexcept {
        return block_shift;
    }

    void TickFrame();

    /// Invalidates any buffer in the logical page range.
    void InvalidateMemory(VAddr device_addr, u64 size, bool assume_locks = false);

    /// Unmark region as modified from the host GPU, handing its memory back to the guest
    void UnmarkPagesAsGpuModified(VAddr device_addr, u64 size, bool is_write = false) {
        IterateRegions(device_addr, size, [is_write](RegionManager* region, u64 offset, u64 size) {
            if (is_write) {
                region->ChangeRegionState<StateOp::Set, StateOp::Clear>(offset, size);
            } else {
                region->ChangeRegionState<StateOp::None, StateOp::Clear>(offset, size);
            }
        });
        gpu_memory.ReleaseOwner(device_addr, size, BufferOwner);
    }

    /// Flushes any GPU modified buffer in the logical page range back to CPU memory.
    void ReadMemory(VAddr device_addr, u64 size, bool is_write = false, bool async = false,
                    bool assume_locks = false);

    /// Flushes GPU modified ranges of the uncovered part of the edge pages of an image.
    void ReadEdgeImagePages(const Image& image);

    /// Finds a buffer for the specified region.
    [[nodiscard]] std::pair<const Buffer*, u64> ObtainBuffer(
        VAddr device_addr, u32 size, ObtainBufferFlags flags = ObtainBufferFlags::None);

    /// Attempts to obtain a buffer without modifying the cache contents.
    [[nodiscard]] std::pair<const Buffer*, u64> ObtainBufferForImage(VAddr device_addr, u32 size);

    /// Return true when a region is modified from the CPU
    [[nodiscard]] bool IsRegionCpuModified(VAddr addr, size_t size);

    /// Return true when a region is modified from the GPU
    [[nodiscard]] bool IsRegionGpuModified(VAddr addr, size_t size);

    /// Mark region as modified from the GPU
    void MarkRegionAsGpuModified(VAddr addr, size_t size);

    /// Synchronizes all buffers needed for DMA.
    void SynchronizeDmaBuffers();

    /// Commits pending sparse buffer memory binds. Must be called before every scheduler submit.
    void SubmitPendingArenaBinds(Vulkan::SubmitInfo& info);

private:
    struct ArenaBinds {
        const Buffer* arena;
        boost::container::small_vector<vk::SparseMemoryBind, 32> binds;
    };

    ArenaBinds* BindsForArena(const Buffer* arena) {
        auto it = std::ranges::find(pending_binds, arena, &ArenaBinds::arena);
        if (it != pending_binds.end()) {
            return std::addressof(*it);
        }
        return &pending_binds.emplace_back(arena);
    }

    const Buffer* GetArena(u64 first_block, u64 last_block);

    void EnsureResident(const Buffer* arena, u64 first_block, u64 last_block);

    void DownloadMemory(const Buffer* arena, VAddr device_addr, u64 size, bool async);

    bool SynchronizeMemory(const Buffer* arena, VAddr device_addr, u32 size, bool is_written);

    bool SynchronizeMemoryFromImage(const Buffer* arena, VAddr device_addr, u32 size);

    /// Calls 'func' with the region and local range of each region the range spans
    template <bool create_region_on_fail = false, typename Func>
    bool IterateRegions(VAddr device_addr, u64 size, Func&& func) {
        using FuncReturn = typename std::invoke_result<Func, RegionManager*, u64, size_t>::type;
        static constexpr bool BOOL_BREAK = std::is_same_v<FuncReturn, bool>;
        u64 remaining_size = size;
        u64 page_index = device_addr >> HIGHER_PAGE_BITS;
        u64 page_offset = device_addr & HIGHER_PAGE_MASK;
        while (remaining_size > 0) {
            const u64 copy_amount = std::min(HIGHER_PAGE_SIZE - page_offset, remaining_size);
            auto* region = regions[page_index];
            if (region == nullptr && create_region_on_fail) {
                region = CreateRegion(page_index);
            }
            if (region != nullptr) {
                if constexpr (BOOL_BREAK) {
                    if (func(region, page_offset, copy_amount)) {
                        return true;
                    }
                } else {
                    func(region, page_offset, copy_amount);
                }
            }
            page_index++;
            page_offset = 0;
            remaining_size -= copy_amount;
        }
        return false;
    }

    /// Mark region as modified from the host GPU without uploading it again
    void MarkPagesAsGpuModified(VAddr device_addr, u64 size) {
        IterateRegions<true>(device_addr, size, [](RegionManager* region, u64 offset, u64 size) {
            region->ChangeRegionState<StateOp::None, StateOp::Set>(offset, size);
        });
    }

    /// Mark region as modified from the CPU
    void MarkRegionAsCpuModified(VAddr device_addr, u64 size) {
        IterateRegions(device_addr, size, [](RegionManager* region, u64 offset, u64 size) {
            region->ChangeRegionState<StateOp::Set, StateOp::None>(offset, size);
        });
    }

    /// Call 'func' for each CPU modified range and unmark those pages as CPU modified
    void ForEachUploadRange(VAddr device_addr, u64 size, bool is_written, auto&& func) {
        IterateRegions<true>(
            device_addr, size, [&func, is_written](RegionManager* region, u64 offset, u64 size) {
                if (is_written) {
                    region->ForEachModifiedRange<Type::CPU, StateOp::Clear, StateOp::Set>(
                        offset, size, func);
                } else {
                    region->ForEachModifiedRange<Type::CPU, StateOp::Clear, StateOp::None>(
                        offset, size, func);
                }
            });
    }

    /// Call 'func' with the bounds of each part of the range a buffer holds the newest copy of.
    /// The owner lock is held throughout, so 'func' must not touch gpu memory itself.
    void ForEachGpuModifiedRange(VAddr device_addr, u64 size, auto&& func) {
        gpu_memory.ForEachOwner(device_addr, size, [&](VAddr addr, u64 run_size, Owner owner) {
            if (owner.kind == OwnerKind::Buffer) {
                func(addr, addr + run_size);
            }
        });
    }

    /// Call 'func' for each GPU modified range and unmark those pages as GPU modified
    template <bool clear>
    void ForEachDownloadRange(VAddr device_addr, u64 size, auto&& func) {
        constexpr auto gpu_op = clear ? StateOp::Clear : StateOp::None;
        IterateRegions(device_addr, size, [&func](RegionManager* region, u64 offset, u64 size) {
            region->ForEachModifiedRange<Type::GPU, StateOp::None, gpu_op>(offset, size, func);
        });
    }

    RegionManager* CreateRegion(u64 page_index);

    const Vulkan::Instance& instance;
    Vulkan::Scheduler& scheduler;
    Vulkan::Runtime& runtime;
    Vulkan::StagingBufferPool& staging_pool;
    AmdGpu::Liverpool* liverpool;
    Core::MemoryManager* memory;
    TextureCache& texture_cache;
    GpuMemory& gpu_memory;
    const u32 readbacks_mode;
    std::deque<std::array<RegionManager, REGION_POOL_SIZE>> region_pool;
    std::vector<RegionManager*> free_regions;
    std::vector<RegionManager*> regions;

    StreamBuffer stream_buffer;
    Buffer gds_buffer;
    std::unique_ptr<Buffer> null_index_buffer;

    std::unique_ptr<FaultManager> fault_manager;
    std::unique_ptr<Buffer> bda_pagetable_buffer;
    bool fault_process_pending{};

    std::array<const Buffer*, NUM_ARENA_PAGES> address_space{};
    std::deque<Buffer> arenas;
    std::vector<ArenaBinds> pending_binds;
    Vulkan::Semaphore memory_semaphore;

    struct Backing : public Interval {
        vk::DeviceMemory memory;
        u64 offset;
        constexpr bool CanMergeWith(const Backing& other) const noexcept {
            return memory == other.memory && offset + (end - start) == other.offset;
        }
        constexpr Backing SubRange(u64 a, u64 b) const noexcept {
            return {{a, b}, memory, offset + (a - start)};
        }
    };
    IntervalList<Backing> resident_ranges;

    u32 arena_memory_type_index{};
    u32 block_size{};
    u32 block_shift{};
    u32 blocks_per_arena_page{};
    u32 blocks_per_arena_page_shift{};
};

} // namespace VideoCore
