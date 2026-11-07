// SPDX-FileCopyrightText: Copyright 2024-2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <boost/container/small_vector.hpp>

#include "common/div_ceil.h"
#include "core/emulator_settings.h"
#include "video_core/gpu_memory.h"
#include "video_core/renderer_vulkan/vk_rasterizer.h"

namespace VideoCore {

/// Pending ownership bit changes, buffered so they are published only once the host
/// protection matching them has been applied.
struct GpuMemory::OwnedUpdates {
    struct Run {
        u64 begin_page;
        u64 end_page;
        bool set;
    };

    void Add(u64 page, bool set) {
        if (!runs.empty()) {
            auto& last = runs.back();
            if (last.set == set && last.end_page == page) {
                last.end_page = page + 1;
                return;
            }
        }
        runs.emplace_back(page, page + 1, set);
    }

    boost::container::small_vector<Run, 8> runs;
};

GpuMemory::GpuMemory(Vulkan::Rasterizer* rasterizer_)
    : rasterizer{rasterizer_}, owned_regions(NUM_REGIONS) {
    precise_readbacks = EmulatorSettings.GetReadbacksMode() == GpuReadbacksMode::Precise;
    protector = CreatePageProtector(rasterizer, *this);
}

GpuMemory::~GpuMemory() = default;

void GpuMemory::OnGpuMap(VAddr address, size_t size) {
    EnsurePages(address, address + size);
    protector->OnMap(address, size);
}

void GpuMemory::OnGpuUnmap(VAddr address, size_t size) {
    protector->OnUnmap(address, size);
}

void GpuMemory::EnsurePages(VAddr begin, VAddr end) {
    end = std::min(end, VAddr{1} << ADDRESS_BITS) - 1;
    const size_t start_page = begin >> PAGE_BITS;
    const size_t end_page = end >> PAGE_BITS;

    std::scoped_lock lk{ensure_mutex};
    cached_pages.reserve(start_page, end_page);
    locks.reserve(start_page, end_page);
    for (size_t region = begin >> HIGHER_PAGE_BITS; region <= (end >> HIGHER_PAGE_BITS); ++region) {
        if (owned_regions[region].load(std::memory_order_relaxed) == nullptr) {
            auto& bits = owned_pool.emplace_back();
            bits.base_addr = region << HIGHER_PAGE_BITS;
            owned_regions[region].store(&bits, std::memory_order_release);
        }
    }
}

void GpuMemory::ApplyOwnedUpdates(const OwnedUpdates& updates) {
    for (const auto& run : updates.runs) {
        VAddr addr = run.begin_page << PAGE_BITS;
        const VAddr end = run.end_page << PAGE_BITS;
        while (addr < end) {
            const VAddr region_end = std::min((addr | HIGHER_PAGE_MASK) + 1, end);
            const auto bounds = GetBounds(addr & HIGHER_PAGE_MASK, region_end - addr);
            auto* bits = owned_regions[addr >> HIGHER_PAGE_BITS].load(std::memory_order_acquire);
            if (bits != nullptr) {
                if (run.set) {
                    bits->UpdateRange<StateOp::Set>(bounds);
                } else {
                    bits->UpdateRange<StateOp::Clear>(bounds);
                }
            }
            addr = region_end;
        }
    }
}

template <typename PageOps>
void GpuMemory::UpdateWatchers(u64 page_start, u64 page_end, PageOps&& page_ops) {
    Core::MemoryPermission perms{};
    u64 range_begin = page_start;
    u64 range_pages = 0;
    u64 potential_pages = 0;
    OwnedUpdates owned_updates;

    const auto release_pending = [&] {
        if (range_pages > 0) {
            protector->Protect(range_begin << PAGE_BITS, range_pages << PAGE_BITS, perms);
            range_pages = 0;
            potential_pages = 0;
        }
    };

    for (u64 page = page_start; page != page_end; ++page) {
        PageState* state = cached_pages.find(page);
        if (!state) {
            continue;
        }

        locks[page].lock();

        const auto old_perms = state->Perms();
        if (page == page_start) {
            perms = old_perms;
        }

        // Apply the change to the page state
        const auto [write_op, read_op] = page_ops(page);
        const bool was_owned = state->num_read_watchers != 0;
        const auto new_perms = state->Update(write_op, read_op);
        if (const bool is_owned = state->num_read_watchers != 0; is_owned != was_owned) {
            owned_updates.Add(page, is_owned);
        }

        if (new_perms != perms) [[unlikely]] {
            // If the protection changed add pending (un)protect action
            release_pending();
            perms = new_perms;
        } else if (range_pages != 0) {
            // If the protection did not change, extend the potential range
            ++potential_pages;
        }

        // If the page must be (un)protected
        if (new_perms != old_perms) {
            if (range_pages == 0) {
                // Start a new potential range
                range_begin = page;
                potential_pages = 1;
            }
            // Extend current range up to potential range
            range_pages = potential_pages;
        }
    }

    // Add pending (un)protect action
    release_pending();

    // Ownership is only published once the protection backing it is in place, as the bits are
    // consulted without holding the page locks.
    ApplyOwnedUpdates(owned_updates);

    for (u64 page = page_start; page != page_end; ++page) {
        if (auto* lock = locks.find(page)) {
            lock->unlock();
        }
    }
}

void GpuMemory::UpdatePageWatchers(VAddr addr, u64 size, PageOp write_op, PageOp read_op) {
    const u64 page_start = addr >> PAGE_BITS;
    const u64 page_end = Common::DivCeil(addr + size, BYTES_PER_PAGE);

    // Iterate requested pages
    const u64 aligned_addr = page_start << PAGE_BITS;
    const u64 aligned_end = page_end << PAGE_BITS;
    if (!rasterizer->IsMapped(aligned_addr, aligned_end - aligned_addr)) {
        LOG_WARNING(Common_Memory,
                    "Tracking memory region {:#x} - {:#x} which is not fully GPU mapped.",
                    aligned_addr, aligned_end);
        EnsurePages(aligned_addr, aligned_end);
    }

    UpdateWatchers(page_start, page_end, [&](u64) { return std::make_pair(write_op, read_op); });
}

void GpuMemory::UpdatePageWatchersForRegion(VAddr base_addr, const Bounds& bounds,
                                            const RegionBits& write_mask,
                                            const RegionBits& read_mask, PageOp write_op,
                                            PageOp read_op) {
    const u64 base_page = base_addr >> PAGE_BITS;
    const u64 page_start = bounds.start_word * PAGES_PER_WORD + bounds.start_page;
    const u64 page_end = bounds.end_word * PAGES_PER_WORD + bounds.end_page + 1;

    UpdateWatchers(base_page + page_start, base_page + page_end, [&](u64 page) {
        const u64 local_page = page - base_page;
        return std::make_pair(write_mask.GetPage(local_page) ? write_op : PageOp::None,
                              read_mask.GetPage(local_page) ? read_op : PageOp::None);
    });
}

std::unique_ptr<PageProtector> CreatePageProtector(Vulkan::Rasterizer* rasterizer,
                                                   GpuMemory& gpu_memory) {
#ifdef __linux__
    if (EmulatorSettings.IsUserfaultfdTracking()) {
        try {
            auto protector = CreateUffdPageProtector(rasterizer, gpu_memory);
            LOG_INFO(Config, "Memory tracking method: userfaultfd");
            return protector;
        } catch (const std::runtime_error& e) {
            // if uffd is unsupported, falls back to signals
        }
    }
    LOG_INFO(Config, "Memory tracking method: signals");
#endif
    return CreateSignalPageProtector(rasterizer, gpu_memory);
}

} // namespace VideoCore
