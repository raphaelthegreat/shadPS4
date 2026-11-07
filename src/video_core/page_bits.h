// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <bit>
#include <utility>

#include "common/types.h"

namespace VideoCore {

constexpr u64 PAGES_PER_WORD = 64;
constexpr u64 BYTES_PER_PAGE = 4_KB;
constexpr u64 BYTES_PER_WORD = PAGES_PER_WORD * BYTES_PER_PAGE;

/// Address space is sharded in regions small enough for a single word to summarize them.
constexpr u64 HIGHER_PAGE_BITS = 24;
constexpr u64 HIGHER_PAGE_SIZE = 1ULL << HIGHER_PAGE_BITS;
constexpr u64 HIGHER_PAGE_MASK = HIGHER_PAGE_SIZE - 1ULL;

constexpr u64 NUM_REGION_PAGES = HIGHER_PAGE_SIZE / BYTES_PER_PAGE;
constexpr u64 NUM_REGION_WORDS = HIGHER_PAGE_SIZE / BYTES_PER_WORD;

enum class StateOp : u8 {
    None = 0,
    Set = 1,
    Clear = 2,
};

/// Word and page indices of the first and last page of an inclusive range.
struct Bounds {
    u64 start_word;
    u64 start_page;
    u64 end_word;
    u64 end_page;
};

/// Returns the bounds of the pages touching the range, relative to the start of a bit array.
constexpr Bounds GetBounds(u64 offset, u64 size) {
    const u64 end_address = offset + size - 1;
    return Bounds{
        .start_word = offset / BYTES_PER_WORD,
        .start_page = offset / BYTES_PER_PAGE,
        .end_word = end_address / BYTES_PER_WORD,
        .end_page = end_address / BYTES_PER_PAGE,
    };
}

/// Returns the masks selecting the live pages of the first and last word of a range.
constexpr std::pair<u64, u64> GetMasks(u64 start_page, u64 end_page) {
    const u64 start_mask = ~u64{0} << (start_page & (PAGES_PER_WORD - 1));
    const u64 end_mask = ~u64{0} >> (63 - (end_page & (PAGES_PER_WORD - 1)));
    return std::make_pair(start_mask, end_mask);
}

/// Calls 'func' with the index and live page mask of each word the bounds touch.
constexpr void IterateWords(Bounds bounds, auto&& func) {
    const auto [start_word, start_page, end_word, end_page] = bounds;
    const auto [start_mask, end_mask] = GetMasks(start_page, end_page);
    if (start_word == end_word) [[likely]] {
        func(start_word, start_mask & end_mask);
    } else {
        func(start_word, start_mask);
        for (s64 i = start_word + 1; i < end_word; ++i) {
            func(i, ~0ULL);
        }
        func(end_word, end_mask);
    }
}

/// Calls 'func' with the offset and length of each run of set bits in the word.
constexpr void IteratePages(u64 word, auto&& func) {
    u64 offset{};
    while (word != 0) {
        const u64 empty_bits = std::countr_zero(word);
        offset += empty_bits;
        word >>= empty_bits;
        const u64 set_bits = std::countr_one(word);
        func(offset, set_bits);
        word = set_bits < PAGES_PER_WORD ? (word >> set_bits) : 0;
        offset += set_bits;
    }
}

/**
 * A fixed size bitset with one bit per 4KB page, carrying summaries of which words are
 * fully clear and which are fully set so that queries over large ranges can skip them.
 */
template <u64 NumWords>
struct alignas(64) BitArray {
    static_assert(NumWords <= 64, "Summary words hold one bit per data word");

    static constexpr u64 NUM_WORDS = NumWords;
    static constexpr u64 NUM_PAGES = NumWords * PAGES_PER_WORD;
    static constexpr u64 SIZE = NUM_PAGES * BYTES_PER_PAGE;

    constexpr void Fill(bool bit) {
        const u64 value = bit ? ~u64{0} : u64{0};
        set_summary = value;
        clear_summary = ~value;
        data.fill(value);
    }

    constexpr bool GetPage(u64 page) const {
        return data[page / PAGES_PER_WORD] & (1ULL << (page % PAGES_PER_WORD));
    }

    constexpr u64& operator[](u64 index) {
        return data[index];
    }

    constexpr u64 operator[](u64 index) const {
        return data[index];
    }

    /// Returns true if any page in the bounds is set.
    constexpr bool AnySet(const Bounds& bounds) const noexcept {
        const auto [start_mask, end_mask] = GetMasks(bounds.start_page, bounds.end_page);
        if (bounds.start_word == bounds.end_word) {
            return data[bounds.start_word] & start_mask & end_mask;
        }
        return (data[bounds.start_word] & start_mask) || (data[bounds.end_word] & end_mask) ||
               (set_summary & InteriorMask(bounds));
    }

    /// Returns true if any page in the bounds is clear.
    constexpr bool AnyClear(const Bounds& bounds) const noexcept {
        const auto [start_mask, end_mask] = GetMasks(bounds.start_page, bounds.end_page);
        if (bounds.start_word == bounds.end_word) {
            const u64 mask = start_mask & end_mask;
            return (data[bounds.start_word] & mask) != mask;
        }
        return (data[bounds.start_word] & start_mask) != start_mask ||
               (data[bounds.end_word] & end_mask) != end_mask ||
               (clear_summary & InteriorMask(bounds));
    }

    /// Applies the operation to the masked pages of a word without updating summaries.
    template <StateOp op>
    static constexpr u64 Apply(u64 word, u64 mask) {
        if constexpr (op == StateOp::Clear) {
            return word & ~mask;
        } else if constexpr (op == StateOp::Set) {
            return word | mask;
        } else {
            static_assert(false);
        }
    }

    /// Applies the operation to the masked pages of a word and refreshes its summary bits.
    template <StateOp op>
    constexpr void Update(u64 index, u64 mask) {
        if constexpr (op == StateOp::None) {
            return;
        } else {
            data[index] = Apply<op>(data[index], mask);
            RefreshSummary(index);
        }
    }

    constexpr void RefreshSummary(u64 index) {
        const u64 summary_bit = u64{1} << index;
        set_summary = data[index] ? (set_summary | summary_bit) : (set_summary & ~summary_bit);
        clear_summary =
            data[index] != ~u64{0} ? (clear_summary | summary_bit) : (clear_summary & ~summary_bit);
    }

    u64 set_summary;
    u64 clear_summary;
    std::array<u64, NumWords> data;

private:
    /// Mask of the words fully contained between the first and last word of the bounds.
    static constexpr u64 InteriorMask(const Bounds& bounds) {
        return (u64{1} << bounds.end_word) - (u64{2} << bounds.start_word);
    }
};

using RegionBits = BitArray<NUM_REGION_WORDS>;

/// Empty bounds, widened by every range accumulated into it.
constexpr Bounds MIN_BOUNDS = {
    .start_word = NUM_REGION_WORDS - 1,
    .start_page = PAGES_PER_WORD - 1,
    .end_word = 0,
    .end_page = 0,
};

} // namespace VideoCore
