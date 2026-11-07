// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <optional>

#include "video_core/texture_cache/host_compatibility.h"
#include "video_core/texture_cache/image_overlap.h"
#include "video_core/texture_cache/tile.h"

namespace VideoCore {

Extent3D MipExtent(const ImageInfo& info, u32 level) {
    return Extent3D{
        .width = std::max(info.size.width >> level, 1u),
        .height = std::max(info.size.height >> level, 1u),
        .depth = std::max(info.size.depth >> level, 1u),
    };
}

bool IsLayoutCompatible(const ImageInfo& lhs, const ImageInfo& rhs) {
    return lhs.array_mode == rhs.array_mode && lhs.num_bits == rhs.num_bits &&
           lhs.num_samples == rhs.num_samples && lhs.props.is_block == rhs.props.is_block &&
           lhs.alt_tile == rhs.alt_tile && lhs.bank_swizzle == rhs.bank_swizzle;
}

namespace {

/// How a mip is addressed row by row, which is what decides whether an image starting part way
/// down another one can be reached with a plain copy.
struct RowLayout {
    u32 row_bytes;   ///< Bytes spanned by a single row of elements
    u32 granularity; ///< Rows a vertical shift has to be a whole multiple of
};

std::optional<RowLayout> GetRowLayout(const ImageInfo& info, u32 level) {
    const auto& mip = info.mips_layout[level];
    const u32 block = info.props.is_block ? 4 : 1;
    const u32 pitch_elements = mip.pitch / block;
    const u32 height_elements = mip.height / block;
    const u32 row_bytes = pitch_elements * info.num_bits / 8;
    // Very small mips have their stored extents clamped to a minimum, which no longer describes
    // the memory they occupy. Fall back to whole slice matching when that is the case.
    if (info.num_samples != 1 || row_bytes == 0 || height_elements == 0 ||
        row_bytes * height_elements != mip.slice_size) {
        return std::nullopt;
    }
    if (info.array_mode == AmdGpu::ArrayMode::ArrayLinearAligned) {
        return RowLayout{row_bytes, 1};
    }
    // Thick tiles interleave slices within a tile row, which a vertical shift would break.
    if (AmdGpu::GetMicroTileThickness(info.array_mode) != 1) {
        return std::nullopt;
    }
    // Which bank and pipe a tile lands in repeats with the macro tile, and a whole row of macro
    // tiles is contiguous, so a shift by a multiple of one is a plain translation of the bytes.
    // Anything finer lands the same texels in different banks.
    if (AmdGpu::IsMacroTiled(info.array_mode) && ((info.micro_tiled_mips >> level) & 1) == 0) {
        const auto [macro_pitch, macro_height] =
            GetMacroTileExtents(info.tile_mode, info.num_bits, info.num_samples, info.alt_tile);
        return RowLayout{row_bytes, macro_height};
    }
    return RowLayout{row_bytes, micro_tile_extent.second};
}

/// Reports the overlap of two mips that cover the same rows of the same pitch, with one of them
/// possibly starting further down than the other.
bool CollectRowShifted(const ImageInfo& src, u32 src_level, const ImageInfo& dst, u32 dst_level,
                       s64 delta, SubresourceCopies& copies) {
    // A shift within a slice makes the layers of the two images interleave, which no single run
    // of subresources describes. Guests only do this for plain single layer render targets.
    if (src.resources.layers != 1 || dst.resources.layers != 1 || src.size.depth != 1 ||
        dst.size.depth != 1) {
        return false;
    }
    const auto src_rows = GetRowLayout(src, src_level);
    const auto dst_rows = GetRowLayout(dst, dst_level);
    if (!src_rows || !dst_rows || src_rows->row_bytes != dst_rows->row_bytes ||
        src_rows->granularity != dst_rows->granularity) {
        return false;
    }
    const s64 row_bytes = src_rows->row_bytes;
    if (delta % row_bytes != 0) {
        return false;
    }
    const s64 shift = delta / row_bytes;
    const u32 granularity = src_rows->granularity;
    if (shift % granularity != 0) {
        return false;
    }

    const u32 block = src.props.is_block ? 4 : 1;
    const auto src_extent = MipExtent(src, src_level);
    const auto dst_extent = MipExtent(dst, dst_level);
    const u32 src_offset_y = static_cast<u32>(std::max<s64>(0, shift)) * block;
    const u32 dst_offset_y = static_cast<u32>(std::max<s64>(0, -shift)) * block;
    if (src_offset_y >= src_extent.height || dst_offset_y >= dst_extent.height) {
        return false;
    }
    copies.push_back(SubresourceCopy{
        .src{.level = static_cast<u16>(src_level), .layer = 0},
        .dst{.level = static_cast<u16>(dst_level), .layer = 0},
        .num_layers = 1,
        .src_offset_y = src_offset_y,
        .dst_offset_y = dst_offset_y,
        .height = std::min(src_extent.height - src_offset_y, dst_extent.height - dst_offset_y),
    });
    return true;
}

} // Anonymous namespace

void CollectOverlapCopies(const ImageInfo& src, const ImageInfo& dst, SubresourceCopies& copies) {
    if (!IsLayoutCompatible(src, dst)) {
        return;
    }
    for (u32 dst_level = 0; dst_level < dst.resources.levels; ++dst_level) {
        const auto& dst_mip = dst.mips_layout[dst_level];
        if (dst_mip.slice_size == 0) {
            continue;
        }
        for (u32 src_level = 0; src_level < src.resources.levels; ++src_level) {
            const auto& src_mip = src.mips_layout[src_level];
            // Rows of both mips have to be the same length for any of them to line up at all.
            if (src_mip.pitch != dst_mip.pitch) {
                continue;
            }
            const s64 delta = static_cast<s64>(dst.guest_address + dst_mip.offset) -
                              static_cast<s64>(src.guest_address + src_mip.offset);

            // Equally shaped mips at a whole slice apart line up layer for layer, which is the
            // only form the overlap can take once more than one layer is involved.
            if (src_mip.height == dst_mip.height && src_mip.slice_size == dst_mip.slice_size &&
                delta % static_cast<s64>(dst_mip.slice_size) == 0) {
                const s64 shift = delta / static_cast<s64>(dst_mip.slice_size);
                const s64 first = std::max<s64>(0, -shift);
                const s64 last = std::min<s64>(dst.resources.layers,
                                               static_cast<s64>(src.resources.layers) - shift);
                if (first >= last) {
                    continue;
                }
                copies.push_back(SubresourceCopy{
                    .src{.level = static_cast<u16>(src_level),
                         .layer = static_cast<u16>(first + shift)},
                    .dst{.level = static_cast<u16>(dst_level), .layer = static_cast<u16>(first)},
                    .num_layers = static_cast<u16>(last - first),
                    .src_offset_y = 0,
                    .dst_offset_y = 0,
                    .height = std::min(MipExtent(src, src_level).height,
                                       MipExtent(dst, dst_level).height),
                });
                // Mips occupy disjoint byte ranges, so no other source mip can hold this one.
                break;
            }

            // Otherwise one of them may begin part way down the rows of the other, which is how a
            // guest renders into a subrect of a larger allocation.
            if (CollectRowShifted(src, src_level, dst, dst_level, delta, copies)) {
                break;
            }
        }
    }
}

bool IsViewOf(const ImageInfo& base, const ImageInfo& view, SubresourceRange& out_range) {
    if (view.resources.levels > base.resources.levels ||
        view.resources.layers > base.resources.layers) {
        return false;
    }
    if (!IsVulkanFormatCompatible(base.pixel_format, view.pixel_format)) {
        return false;
    }

    SubresourceCopies copies;
    CollectOverlapCopies(base, view, copies);
    if (copies.size() != view.resources.levels) {
        return false;
    }

    const auto& first = copies.front();
    for (u32 level = 0; level < copies.size(); ++level) {
        const auto& copy = copies[level];
        // A view cannot reorder, crop or skip subresources, so the whole of every destination
        // level has to map onto a matching level of the base at the same layer and extent.
        if (copy.dst.level != level || copy.dst.layer != 0 ||
            copy.num_layers != view.resources.layers) {
            return false;
        }
        // A view cannot start part way down the rows of the image it is taken from.
        if (copy.src_offset_y != 0 || copy.dst_offset_y != 0) {
            return false;
        }
        if (copy.src.level != first.src.level + level || copy.src.layer != first.src.layer) {
            return false;
        }
        if (!(MipExtent(base, copy.src.level) == MipExtent(view, level))) {
            return false;
        }
    }

    out_range = SubresourceRange{
        .base = first.src,
        .extent{.levels = view.resources.levels, .layers = view.resources.layers},
    };
    return true;
}

} // namespace VideoCore
