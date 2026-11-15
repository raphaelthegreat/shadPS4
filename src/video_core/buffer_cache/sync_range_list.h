// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <algorithm>
#include <span>
#include <vector>

#include "common/types.h"

namespace VideoCore {

/// Collects address ranges pending synchronization. Insertions are appended and the list is
/// sorted and coalesced lazily, as most insertions repeat ranges and queries are infrequent.
class SyncRangeList {
    static constexpr size_t MinUnsortedRanges = 64;

public:
    struct Range {
        VAddr start;
        VAddr end;
    };

    bool Empty() const noexcept {
        return ranges.empty();
    }

    void Clear() noexcept {
        ranges.clear();
        num_sorted = 0;
    }

    void Add(VAddr start, VAddr end) {
        if (!ranges.empty() && ranges.back().start <= start && end <= ranges.back().end) {
            return;
        }
        ranges.emplace_back(start, end);
        if (ranges.size() - num_sorted > std::max(num_sorted, MinUnsortedRanges)) {
            Normalize();
        }
    }

    bool Overlaps(VAddr start, VAddr end) {
        if (ranges.size() - num_sorted > MinUnsortedRanges) {
            Normalize();
        }
        const auto sorted = std::span{ranges}.first(num_sorted);
        const auto it = std::ranges::upper_bound(sorted, start, {}, &Range::end);
        if (it != sorted.end() && it->start < end) {
            return true;
        }
        return std::ranges::any_of(std::span{ranges}.subspan(num_sorted), [&](const Range& range) {
            return range.start < end && start < range.end;
        });
    }

    void ForEach(auto&& func) {
        Normalize();
        for (const auto& range : ranges) {
            func(range.start, range.end);
        }
    }

    void Normalize() {
        if (num_sorted == ranges.size()) {
            return;
        }
        const auto middle = ranges.begin() + num_sorted;
        std::ranges::sort(middle, ranges.end(), {}, &Range::start);
        std::ranges::inplace_merge(ranges, middle, {}, &Range::start);
        size_t last = 0;
        for (size_t i = 1; i < ranges.size(); ++i) {
            if (ranges[i].start <= ranges[last].end) {
                ranges[last].end = std::max(ranges[last].end, ranges[i].end);
            } else {
                ranges[++last] = ranges[i];
            }
        }
        ranges.resize(last + 1);
        num_sorted = ranges.size();
    }

private:
    std::vector<Range> ranges;
    size_t num_sorted{};
};

} // namespace VideoCore
