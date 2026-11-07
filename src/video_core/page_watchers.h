// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <memory>

#include "common/adaptive_mutex.h"
#include "common/alignment.h"
#include "common/assert.h"
#include "common/multi_level_page_table.h"
#include "common/types.h"
#include "core/address_space.h"
#include "video_core/page_bits.h"

namespace Vulkan {
class Rasterizer;
}

namespace VideoCore {

constexpr size_t PM_PAGE_BITS = 12;
constexpr size_t PM_PAGE_SIZE = 1ULL << PM_PAGE_BITS;

enum class PageOp : s8 {
    None = 0,
    Track = 1,
    Untrack = -1,
};

/// Returns page aligned address.
constexpr VAddr GetPageAddr(VAddr addr) {
    return Common::AlignDown(addr, PM_PAGE_SIZE);
}

/// Returns address of the next page.
constexpr VAddr GetNextPageAddr(VAddr addr) {
    return Common::AlignUp(addr + 1, PM_PAGE_SIZE);
}

/**
 * Refcounted table of read and write watchers over guest pages. A watcher means one GPU
 * resource wants to observe accesses to a page, so host protection may only be lifted once
 * every interested resource has dropped its watcher.
 *
 * Applying the protection is delegated to a platform backend, see CreatePageWatchers.
 */
class PageWatchers {
public:
    virtual ~PageWatchers() = default;

    /// Register a range of mapped gpu memory.
    virtual void OnMap(VAddr address, size_t size) {
        EnsurePages(address, address + size);
    }

    /// Unregister a range of gpu memory that was unmapped.
    virtual void OnUnmap(VAddr address, size_t size) {}

    /// Updates watchers in the pages touching the specified region.
    void UpdatePageWatchers(VAddr addr, u64 size, PageOp write_op);

    /// Updates watchers in the pages touching the inclusive bounds using a mask.
    void UpdatePageWatchersForRegion(VAddr base_addr, const Bounds& bounds,
                                     const RegionBits& write_mask, const RegionBits& read_mask,
                                     PageOp write_op, PageOp read_op);

protected:
    /// Applies host protection to a range of guest memory.
    virtual void Protect(VAddr address, size_t size, Core::MemoryPermission perms) = 0;

    /// Allocates page state for a range so it can be watched.
    void EnsurePages(VAddr begin, VAddr end);

    static constexpr size_t ADDRESS_BITS = 40;

    inline static Vulkan::Rasterizer* rasterizer;

private:
    struct PageState {
        u8 num_write_watchers;
        u8 num_read_watchers;

        Core::MemoryPermission WritePerm() const noexcept {
            return num_write_watchers == 0 ? Core::MemoryPermission::Write
                                           : Core::MemoryPermission::None;
        }

        Core::MemoryPermission ReadPerm() const noexcept {
            return num_read_watchers == 0 ? Core::MemoryPermission::Read
                                          : Core::MemoryPermission::None;
        }

        Core::MemoryPermission Perms() const noexcept {
            return ReadPerm() | WritePerm();
        }

        constexpr Core::MemoryPermission Update(PageOp write_op, bool update_write = true,
                                                PageOp read_op = PageOp::None,
                                                bool update_read = false) {
            if (update_read) {
                if (read_op == PageOp::Track) {
                    ASSERT_MSG(num_read_watchers == 0, "Too many watchers");
                } else if (read_op == PageOp::Untrack) {
                    ASSERT_MSG(num_read_watchers > 0, "Not enough watchers");
                }
                num_read_watchers += std::to_underlying(read_op);
            }
            if (update_write) {
                if (write_op == PageOp::Track) {
                    ASSERT_MSG(num_write_watchers < 255, "Too many watchers");
                } else if (write_op == PageOp::Untrack) {
                    ASSERT_MSG(num_write_watchers > 0, "Not enough watchers");
                }
                num_write_watchers += std::to_underlying(write_op);
            }
            return Perms();
        }
    };

    struct PageTraits {
        using Entry = PageState;
        static constexpr size_t ADDRESS_SPACE_BITS = ADDRESS_BITS;
        static constexpr size_t L1_BITS = 16;
        static constexpr size_t PAGE_BITS = PM_PAGE_BITS;
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
        static constexpr size_t PAGE_BITS = PM_PAGE_BITS;
        static constexpr bool NULL_CHECK = false;
    };

    Common::MultiLevelPageTable<PageTraits> cached_pages;
    Common::MultiLevelPageTable<MutexTraits> locks;
};

/// Creates the watcher table backed by the best available host protection method.
std::unique_ptr<PageWatchers> CreatePageWatchers(Vulkan::Rasterizer* rasterizer);

#ifdef __linux__
/// Creates a watcher table backed by userfaultfd, throws if unsupported.
std::unique_ptr<PageWatchers> CreateUffdPageWatchers(Vulkan::Rasterizer* rasterizer);
#endif

/// Creates a watcher table backed by access violation signals.
std::unique_ptr<PageWatchers> CreateSignalPageWatchers(Vulkan::Rasterizer* rasterizer);

} // namespace VideoCore
