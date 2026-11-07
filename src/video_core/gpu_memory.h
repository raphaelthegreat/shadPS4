// SPDX-FileCopyrightText: Copyright 2024-2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <atomic>
#include <deque>
#include <memory>
#include <mutex>
#include <vector>

#include "common/adaptive_mutex.h"
#include "common/assert.h"
#include "common/types.h"
#include "video_core/owner_map.h"
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
 *
 * All of this is kept per 16MB region, so that locating the state of a page is a pair of shifts
 * into a flat table of region pointers rather than a walk of a multi level one.
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
    static constexpr u64 PAGES_PER_REGION_SHIFT = HIGHER_PAGE_BITS - PAGE_BITS;
    static constexpr size_t NUM_REGIONS = 1ULL << (ADDRESS_BITS - HIGHER_PAGE_BITS);

#ifdef PTHREAD_ADAPTIVE_MUTEX_INITIALIZER_NP
    using RegionMutex = Common::AdaptiveMutex;
#else
    using RegionMutex = std::mutex;
#endif

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

    /// Everything known about one 16MB region, tagged with the address its first page maps to.
    /// The owned bits summarize at page granularity which parts of the region some resource holds
    /// a newer copy of, and are published after host protection so they can be read without any
    /// lock. The owner map records the same thing exactly, at the cost of needing the lock to be
    /// read. The present bits mark which pages were ever mapped, as the rest have no state worth
    /// touching even though the region they fall in exists.
    struct RegionState {
        VAddr base_addr;
        mutable RegionMutex mutex;
        RegionBits owned;
        RegionBits present;
        OwnerMap owners;
        std::array<PageState, NUM_REGION_PAGES> pages;
    };
    static_assert(OwnerMap::MAX_REGION_SIZE >= HIGHER_PAGE_SIZE);

    struct OwnedUpdates;

    /// Returns the region a page falls in, or null if nothing ever mapped it.
    RegionState* RegionOfPage(u64 page) const {
        return owned_regions[page >> PAGES_PER_REGION_SHIFT].load(std::memory_order_acquire);
    }

public:
    /// Returns true if any page in the range holds data newer than guest memory.
    [[nodiscard]] bool IsRegionOwned(VAddr addr, u64 size) const {
        bool owned = false;
        ForEachRegion(addr, size, [&](const RegionState* state, const Bounds& bounds) {
            owned = state != nullptr && state->owned.AnySet(bounds);
            return !owned;
        });
        return owned;
    }

    /// Calls 'func' with the address and size of each owned run of pages in the range.
    void ForEachOwnedRange(VAddr addr, u64 size, auto&& func) const {
        ForEachRegion(addr, size, [&](const RegionState* state, const Bounds& bounds) {
            if (state != nullptr) {
                const VAddr base = state->base_addr;
                state->owned.ForEachSetRange(
                    bounds, [&](u64 offset, u64 range_size) { func(base + offset, range_size); });
            }
            return true;
        });
    }

    /// Gives 'owner' the newest copy of a range of guest memory, taking it away from whoever held
    /// it before. Ranges never claimed by anyone belong to the guest.
    void AssignOwner(VAddr addr, u64 size, Owner owner) {
        ForEachOwnerMap(addr, size, [&](RegionState& state, u32 begin, u32 end) {
            state.owners.Assign(begin, end, owner);
        });
    }

    /// Takes a range away from 'owner', leaving alone the parts of it other resources have
    /// claimed in the meantime.
    void ReleaseOwner(VAddr addr, u64 size, Owner owner) {
        ForEachOwnerMap(addr, size, [&](RegionState& state, u32 begin, u32 end) {
            state.owners.Release(begin, end, owner);
        });
    }

    /// Calls 'func' with the address, size and owner of each owned run in the range. The owner
    /// lock is held throughout, so the callback must not touch gpu memory itself.
    void ForEachOwner(VAddr addr, u64 size, auto&& func) const {
        ForEachOwnerMap(addr, size, [&](RegionState& state, u32 begin, u32 end) {
            state.owners.ForEachRun(begin, end, [&](u32 from, u32 to, Owner owner) {
                func(state.base_addr + from, u64{to - from}, owner);
            });
        });
    }

private:
    /// Calls 'func' with the state and local byte bounds of each existing region the range spans,
    /// holding its owner lock. Regions never claimed are skipped, as nothing can own them yet.
    void ForEachOwnerMap(VAddr addr, u64 size, auto&& func) const {
        const VAddr end_addr = addr + size;
        while (addr < end_addr) {
            const VAddr region_base = addr & ~HIGHER_PAGE_MASK;
            const VAddr region_end = std::min(region_base + HIGHER_PAGE_SIZE, end_addr);
            if (auto* state = RegionOfPage(addr >> PAGE_BITS)) {
                std::scoped_lock lk{state->mutex};
                func(*state, static_cast<u32>(addr - region_base),
                     static_cast<u32>(region_end - region_base));
            }
            addr = region_end;
        }
    }

    /// Calls 'func' with the state and local bounds of each region the range touches, stopping
    /// early if it returns false. The state is null for regions nothing ever mapped.
    void ForEachRegion(VAddr addr, u64 size, auto&& func) const {
        const VAddr end_addr = addr + size;
        while (addr < end_addr) {
            const VAddr region_end = std::min((addr | HIGHER_PAGE_MASK) + 1, end_addr);
            const auto bounds = GetBounds(addr & HIGHER_PAGE_MASK, region_end - addr);
            if (!func(RegionOfPage(addr >> PAGE_BITS), bounds)) {
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
    std::mutex ensure_mutex;
    std::deque<RegionState> owned_pool;
    std::vector<std::atomic<RegionState*>> owned_regions;
};

} // namespace VideoCore
