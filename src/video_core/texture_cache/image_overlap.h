// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/small_vector.h"
#include "video_core/texture_cache/image_info.h"
#include "video_core/texture_cache/types.h"

namespace VideoCore {

/// A run of subresources of one image that holds the same guest memory, and the same bytes within
/// it, as a run of subresources of another. Layers are contiguous in both images, so an arbitrary
/// overlap decomposes into a handful of these.
///
/// One of the two row offsets is always zero; they express an image that begins part way down the
/// rows of the other, which is how a guest renders into a subrect of a larger allocation.
struct SubresourceCopy {
    SubresourceBase src;
    SubresourceBase dst;
    u16 num_layers;
    u32 src_offset_y;
    u32 dst_offset_y;
    u32 height;
};

using SubresourceCopies = SmallVector<SubresourceCopy, 8>;

/// True when both images lay their texels out identically, so that a subresource of one can be
/// copied into a subresource of the other without retiling.
bool IsLayoutCompatible(const ImageInfo& lhs, const ImageInfo& rhs);

/// Collects the subresources of `dst` that `src` holds the contents of. Only overlaps that fall on
/// a slice boundary are reported; a partial overlap within a single slice needs a retiling pass.
///
/// Slices of a volume image are addressed through its depth rather than its layer count, so an
/// overlap that starts past the first slice of one is not reported and the caller has to fall back
/// to filling the image from guest memory. An overlap that begins part way down the rows of
/// the other is reported with a row offset, but only when the shift covers whole tiles; a finer
/// one lands the same texels in different banks and no plain copy can express it.
void CollectOverlapCopies(const ImageInfo& src, const ImageInfo& dst, SubresourceCopies& copies);

/// Narrows copies so that they only read the part of `src` that lies within the guest range
/// [begin, end), dropping any that fall outside it entirely. Copies are trimmed at whole slices,
/// or at whole tile rows when they carry a row offset, so a range that cuts through either leaves
/// the texels it only partly covers behind.
void ClipCopiesToRange(const ImageInfo& dst, SubresourceCopies& copies, VAddr begin, VAddr end);

/// True when base can serve the request through a plain image view.
bool IsViewOf(const ImageInfo& base, const ImageInfo& view, SubresourceRange& out_range);

/// Extent of a single mip of an image, in texels.
Extent3D MipExtent(const ImageInfo& info, u32 level);

} // namespace VideoCore
