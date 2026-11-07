// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>

#include "video_core/texture_cache/host_compatibility.h"
#include "video_core/texture_cache/image_overlap.h"

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
            // Equal padded dimensions mean the tiling within the mip is identical, which is what
            // makes a copy of it valid. The visible extents may still differ and are clamped by
            // the caller.
            if (src_mip.pitch != dst_mip.pitch || src_mip.height != dst_mip.height ||
                src_mip.slice_size != dst_mip.slice_size) {
                continue;
            }
            const s64 delta = static_cast<s64>(dst.guest_address + dst_mip.offset) -
                              static_cast<s64>(src.guest_address + src_mip.offset);
            if (delta % static_cast<s64>(dst_mip.slice_size) != 0) {
                continue;
            }
            // Slice n of this destination mip holds the same bytes as slice n + shift of the
            // source mip, for as long as both images still have such a slice.
            const s64 shift = delta / static_cast<s64>(dst_mip.slice_size);
            const s64 first = std::max<s64>(0, -shift);
            const s64 last =
                std::min<s64>(dst.resources.layers, static_cast<s64>(src.resources.layers) - shift);
            if (first >= last) {
                continue;
            }
            copies.push_back(SubresourceCopy{
                .src{.level = static_cast<u16>(src_level),
                     .layer = static_cast<u16>(first + shift)},
                .dst{.level = static_cast<u16>(dst_level), .layer = static_cast<u16>(first)},
                .num_layers = static_cast<u16>(last - first),
            });
            // Mips occupy disjoint byte ranges, so no other source mip can hold this one.
            break;
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
