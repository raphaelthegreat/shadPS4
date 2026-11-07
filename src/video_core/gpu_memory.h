// SPDX-FileCopyrightText: Copyright 2024-2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <atomic>
#include <deque>
#include <memory>
#include <mutex>
#include <vector>

#include "common/adaptive_mutex.h"
#include "common/assert.h"
#include "common/multi_level_page_table.h"
#include "common/types.h"
#include "video_core/page_bits.h"
#include "video_core/page_protect.h"

namespace VideoCore {

enum class PageOp : s8 {
    None = 0,
    Track = 1,
    Untrack = -1,
};

/**
 * Coordinates coherency between guest memory and the GPU caches that shadow it.
 *
 * Every 4KB page carries a refcount of read and write watchers. A watcher means one GPU
 * resource is interested in accesses to the page, so host protection may only be lifted once
 * every interested resource has dropped its watcher. Write watchers are held by any resource
 * holding a copy of the page, while read watchers are held only by resources whose copy is
 * newer than guest memory, which makes the read watcher count double as an ownership mark.
 *
 * Ownership is summarized in a page granular bitset so that it can be queried over large
 * ranges without walking the watcher table.
 */
class GpuMemory {
public:
    explicit GpuMemory(Vulkan::Rasterizer* rasterizer);
    ~GpuMemory();

    /// Register a range of mapped gpu memory.
    void OnGpuMap(VAddr address, size_t size);

    /// Unregister a range of gpu memory that was unmapped.
    void OnGpuUnmap(VAddr address, size_t size);

    /// Updates watchers in the pages touching the specified region.
    void UpdatePageWatchers(VAddr addr, u64 size, PageOp write_op, PageOp read_op = PageOp::None);

    /// Updates watchers in the pages touching the inclusive bounds using a mask.
    void UpdatePageWatchersForRegion(VAddr base_addr, const Bounds& bounds,
                                     const RegionBits& write_mask, const RegionBits& read_mask,
                                     PageOp write_op, PageOp read_op);

private:
    static constexpr size_t ADDRESS_BITS = 40;
    static constexpr size_t NUM_REGIONS = 1ULL << (ADDRESS_BITS - HIGHER_PAGE_BITS);

    /// Ownership bits of one region, tagged with the address its first page maps to.
    struct OwnedBits : RegionBits {
        VAddr base_addr;
    };

    struct OwnedUpdates;

public:
    /// Returns true if any page in the range holds data newer than guest memory.
    [[nodiscard]] bool IsRegionOwned(VAddr addr, u64 size) const {
        bool owned = false;
        ForEachRegion(addr, size, [&](const OwnedBits* bits, const Bounds& bounds) {
            owned = bits != nullptr && bits->AnySet(bounds);
            return !owned;
        });
        return owned;
    }

    /// Calls 'func' with the address and size of each owned run of pages in the range.
    void ForEachOwnedRange(VAddr addr, u64 size, auto&& func) const {
        ForEachRegion(addr, size, [&](const OwnedBits* bits, const Bounds& bounds) {
            if (bits != nullptr) {
                const VAddr base = bits->base_addr;
                bits->ForEachSetRange(
                    bounds, [&](u64 offset, u64 range_size) { func(base + offset, range_size); });
            }
            return true;
        });
    }

private:
    struct PageState {
        u8 num_write_watchers;
        u8 num_read_watchers;

        Core::MemoryPermission WritePerm() const noexcept {
            return num_write_watchers == 0 ? Core::MemoryPermission::Write
                                           : Core::MemoryPermission::None;
        }

        /// Read watchers are always refcounted so ownership of a page can be queried, but
        /// faulting on reads is only worth the cost when readbacks must be exact.
        Core::MemoryPermission ReadPerm() const noexcept {
            return num_read_watchers == 0 || !precise_readbacks ? Core::MemoryPermission::Read
                                                                : Core::MemoryPermission::None;
        }

        Core::MemoryPermission Perms() const noexcept {
            return ReadPerm() | WritePerm();
        }

        constexpr Core::MemoryPermission Update(PageOp write_op, PageOp read_op) {
            if (read_op == PageOp::Track) {
                ASSERT_MSG(num_read_watchers < 255, "Too many watchers");
            } else if (read_op == PageOp::Untrack) {
                ASSERT_MSG(num_read_watchers > 0, "Not enough watchers");
            }
            num_read_watchers += std::to_underlying(read_op);
            if (write_op == PageOp::Track) {
                ASSERT_MSG(num_write_watchers < 255, "Too many watchers");
            } else if (write_op == PageOp::Untrack) {
                ASSERT_MSG(num_write_watchers > 0, "Not enough watchers");
            }
            num_write_watchers += std::to_underlying(write_op);
            return Perms();
        }
    };

    struct PageTraits {
        using Entry = PageState;
        static constexpr size_t ADDRESS_SPACE_BITS = ADDRESS_BITS;
        static constexpr size_t L1_BITS = 16;
        static constexpr size_t PAGE_BITS = VideoCore::PAGE_BITS;
        static constexpr bool NULL_CHECK = false;
    };

    struct MutexTraits {
#ifdef PTHREAD_ADAPTIVE_MUTEX_INITIALIZER_NP
        using Entry = Common::AdaptiveMutex;
#else
        using Entry = std::mutex;
#endif
        static constexpr size_t ADDRESS_SPACE_BITS = ADDRESS_BITS;
        static constexpr size_t L1_BITS = 16;
        static constexpr size_t PAGE_BITS = VideoCore::PAGE_BITS;
        static constexpr bool NULL_CHECK = false;
    };

    /// Calls 'func' with the ownership bits and local bounds of each region the range touches,
    /// stopping early if it returns false. The bits are null for regions never claimed.
    void ForEachRegion(VAddr addr, u64 size, auto&& func) const {
        const VAddr end_addr = addr + size;
        while (addr < end_addr) {
            const VAddr region_end = std::min((addr | HIGHER_PAGE_MASK) + 1, end_addr);
            const auto bounds = GetBounds(addr & HIGHER_PAGE_MASK, region_end - addr);
            const auto* bits =
                owned_regions[addr >> HIGHER_PAGE_BITS].load(std::memory_order_acquire);
            if (!func(bits, bounds)) {
                return;
            }
            addr = region_end;
        }
    }

    /// Walks the pages of a range applying the watcher ops the callback picks for each.
    template <typename PageOps>
    void UpdateWatchers(u64 page_start, u64 page_end, PageOps&& page_ops);

    /// Publishes buffered ownership changes into the summary bits.
    void ApplyOwnedUpdates(const OwnedUpdates& updates);

    /// Allocates page state for a range so it can be watched.
    void EnsurePages(VAddr begin, VAddr end);

    inline static bool precise_readbacks;

    Vulkan::Rasterizer* rasterizer;
    std::unique_ptr<PageProtector> protector;
    Common::MultiLevelPageTable<PageTraits> cached_pages;
    Common::MultiLevelPageTable<MutexTraits> locks;
    std::mutex ensure_mutex;
    std::deque<OwnedBits> owned_pool;
    std::vector<std::atomic<OwnedBits*>> owned_regions;
};

} // namespace VideoCore
