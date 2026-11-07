// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <algorithm>
#include <vector>

#include "common/small_vector.h"
#include "common/types.h"

namespace VideoCore {

enum class OwnerKind : u32 {
    None = 0,
    Buffer = 1,
    Image = 2,
};

/// Identifies the GPU resource holding the newest copy of a range of guest memory.
struct Owner {
    OwnerKind kind = OwnerKind::None;
    u32 index = 0;

    bool operator==(const Owner&) const noexcept = default;

    explicit operator bool() const noexcept {
        return kind != OwnerKind::None;
    }
};

/**
 * Records which resource owns each byte of a region of guest memory.
 *
 * Ownership is stored as boundaries sorted by offset, each starting a run that reaches the next
 * boundary or the end of the region. Bytes before the first boundary are owned by nobody, so an
 * empty map means the guest holds the only copy. Neighbouring runs never share an owner, which
 * keeps the map as short as the ownership pattern genuinely is.
 *
 * Assigning a range therefore needs no interval splitting: the boundary that ends the assigned
 * run carries the owner that used to be there, which is read before the swallowed boundaries are
 * dropped.
 */
class OwnerMap {
public:
    /// Offsets are stored in 24 bits, so a region can span at most this many bytes. A boundary at
    /// the very end of one is never stored, since there is no run left for it to start.
    static constexpr u32 MAX_REGION_SIZE = 1u << 24;

    /// Gives ownership of [begin, end) to 'owner', taking it away from whoever held it before.
    void Assign(u32 begin, u32 end, Owner owner) {
        if (begin >= end) {
            return;
        }
        const Owner tail = At(end);
        const auto first = std::ranges::lower_bound(boundaries, begin, {}, OffsetOf);
        const auto last = std::ranges::upper_bound(boundaries, end, {}, OffsetOf);
        const Owner head = first == boundaries.begin() ? Owner{} : OwnerOf(*(first - 1));

        Boundary replacement[2];
        u32 num_replaced = 0;
        if (head != owner) {
            replacement[num_replaced++] = Make(begin, owner);
        }
        if (end < MAX_REGION_SIZE && tail != owner) {
            replacement[num_replaced++] = Make(end, tail);
        }
        const auto pos = boundaries.erase(first, last);
        boundaries.insert(pos, replacement, replacement + num_replaced);
    }

    /// Takes ownership of [begin, end) away from 'owner', leaving other owners alone.
    void Release(u32 begin, u32 end, Owner owner) {
        SmallVector<std::pair<u32, u32>, 8> runs;
        ForEachRun(begin, end, [&](u32 from, u32 to, Owner run_owner) {
            if (run_owner == owner) {
                runs.emplace_back(from, to);
            }
        });
        for (const auto& [from, to] : runs) {
            Assign(from, to, Owner{});
        }
    }

    [[nodiscard]] Owner At(u32 offset) const {
        const auto it = std::ranges::upper_bound(boundaries, offset, {}, OffsetOf);
        return it == boundaries.begin() ? Owner{} : OwnerOf(*(it - 1));
    }

    [[nodiscard]] bool Empty() const noexcept {
        return boundaries.empty();
    }

    /// Calls 'func' with the bounds and owner of every owned run within [begin, end).
    void ForEachRun(u32 begin, u32 end, auto&& func) const {
        if (begin >= end) {
            return;
        }
        auto it = std::ranges::upper_bound(boundaries, begin, {}, OffsetOf);
        Owner owner = it == boundaries.begin() ? Owner{} : OwnerOf(*(it - 1));
        for (u32 run_begin = begin; run_begin < end;) {
            const u32 run_end = it == boundaries.end() ? end : std::min(u32(it->offset), end);
            if (owner) {
                func(run_begin, run_end, owner);
            }
            if (run_end == end) {
                break;
            }
            owner = OwnerOf(*it);
            run_begin = run_end;
            ++it;
        }
    }

    void Clear() {
        boundaries.clear();
    }

private:
    struct Boundary {
        u32 offset : 24;
        u32 kind : 8;
        u32 index;
    };
    static_assert(sizeof(Boundary) == 8);

    static u32 OffsetOf(const Boundary& boundary) {
        return boundary.offset;
    }

    static Boundary Make(u32 offset, Owner owner) {
        return Boundary{offset, static_cast<u32>(owner.kind), owner.index};
    }

    static Owner OwnerOf(const Boundary& boundary) {
        return Owner{static_cast<OwnerKind>(boundary.kind), boundary.index};
    }

    std::vector<Boundary> boundaries;
};

} // namespace VideoCore
