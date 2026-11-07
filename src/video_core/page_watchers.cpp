// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/div_ceil.h"
#include "core/emulator_settings.h"
#include "video_core/page_watchers.h"
#include "video_core/renderer_vulkan/vk_rasterizer.h"

namespace VideoCore {

void PageWatchers::EnsurePages(VAddr begin, VAddr end) {
    end = std::min(end, VAddr{1} << ADDRESS_BITS) - 1;
    const size_t start_page = begin >> PM_PAGE_BITS;
    const size_t end_page = end >> PM_PAGE_BITS;
    cached_pages.reserve(start_page, end_page);
    locks.reserve(start_page, end_page);
}

void PageWatchers::UpdatePageWatchers(VAddr addr, u64 size, PageOp write_op) {
    const u64 page_start = addr >> PM_PAGE_BITS;
    const u64 page_end = Common::DivCeil(addr + size, PM_PAGE_SIZE);

    Core::MemoryPermission perms{};
    u64 range_begin = page_start;
    u64 range_pages = 0;
    u64 potential_pages = 0;

    const auto release_pending = [&] {
        if (range_pages > 0) {
            Protect(range_begin << PM_PAGE_BITS, range_pages << PM_PAGE_BITS, perms);
            range_pages = 0;
            potential_pages = 0;
        }
    };

    // Iterate requested pages
    const u64 aligned_addr = page_start << PM_PAGE_BITS;
    const u64 aligned_end = page_end << PM_PAGE_BITS;
    if (!rasterizer->IsMapped(aligned_addr, aligned_end - aligned_addr)) {
        LOG_WARNING(Common_Memory,
                    "Tracking memory region {:#x} - {:#x} which is not fully GPU mapped.",
                    aligned_addr, aligned_end);
        EnsurePages(aligned_addr, aligned_end);
    }

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
        const auto new_perms = state->Update(write_op);
        if (new_perms != perms) [[unlikely]] {
            // If the protection changed add pending (un)protect action
            release_pending();
            perms = new_perms;
        } else if (range_pages != 0) {
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

    for (u64 page = page_start; page != page_end; ++page) {
        if (auto* lock = locks.find(page)) {
            lock->unlock();
        }
    }
}

void PageWatchers::UpdatePageWatchersForRegion(VAddr base_addr, const Bounds& bounds,
                                               const RegionBits& write_mask,
                                               const RegionBits& read_mask, PageOp write_op,
                                               PageOp read_op) {
    const u64 base_page = base_addr >> PM_PAGE_BITS;
    const u64 page_start = bounds.start_word * PAGES_PER_WORD + bounds.start_page;
    const u64 page_end = bounds.end_word * PAGES_PER_WORD + bounds.end_page + 1;

    Core::MemoryPermission perms{};
    u64 range_begin = base_page + page_start;
    u64 range_pages = 0;
    u64 potential_pages = 0;

    const auto release_pending = [&] {
        if (range_pages > 0) {
            Protect(range_begin << PM_PAGE_BITS, range_pages << PM_PAGE_BITS, perms);
            range_pages = 0;
            potential_pages = 0;
        }
    };

    for (u64 page = page_start; page != page_end; ++page) {
        PageState* state = cached_pages.find(base_page + page);
        if (!state) {
            continue;
        }

        locks[base_page + page].lock();

        const auto old_perms = state->Perms();
        if (page == page_start) {
            perms = old_perms;
        }

        // Apply the change to the page state
        const bool update_write = write_op != PageOp::None && write_mask.GetPage(page);
        const bool update_read = read_op != PageOp::None && read_mask.GetPage(page);
        const auto new_perms = state->Update(write_op, update_write, read_op, update_read);

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
                range_begin = base_page + page;
                potential_pages = 1;
            }
            // Extend current rango up to potential range
            range_pages = potential_pages;
        }
    }

    // Add pending (un)protect action
    release_pending();

    for (u64 page = page_start; page != page_end; ++page) {
        if (auto* lock = locks.find(base_page + page)) {
            lock->unlock();
        }
    }
}

std::unique_ptr<PageWatchers> CreatePageWatchers(Vulkan::Rasterizer* rasterizer) {
#ifdef __linux__
    if (EmulatorSettings.IsUserfaultfdTracking()) {
        try {
            auto watchers = CreateUffdPageWatchers(rasterizer);
            LOG_INFO(Config, "Memory tracking method: userfaultfd");
            return watchers;
        } catch (const std::runtime_error& e) {
            // if uffd is unsupported, falls back to signals
        }
    }
    LOG_INFO(Config, "Memory tracking method: signals");
#endif
    return CreateSignalPageWatchers(rasterizer);
}

} // namespace VideoCore
