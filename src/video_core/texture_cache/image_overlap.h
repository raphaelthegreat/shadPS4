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
struct SubresourceCopy {
    SubresourceBase src;
    SubresourceBase dst;
    u16 num_layers;
};

using SubresourceCopies = SmallVector<SubresourceCopy, 8>;

/// True when both images lay their texels out identically, so that a subresource of one can be
/// copied into a subresource of the other without retiling.
///
/// Tile mode is deliberately not compared. What decides the in-memory layout is the array mode;
/// a depth image and a colour image aliasing the same memory carry different tile modes while
/// being laid out the same, which guests rely on when sampling a depth buffer as R32.
bool IsLayoutCompatible(const ImageInfo& lhs, const ImageInfo& rhs);

/// Collects the subresources of `dst` that `src` holds the contents of. Only overlaps that fall on
/// a slice boundary are reported; a partial overlap within a single slice needs a retiling pass.
///
/// Slices of a volume image are addressed through its depth rather than its layer count, so an
/// overlap that starts past the first slice of one is not reported and the caller has to fall back
/// to filling the image from guest memory.
void CollectOverlapCopies(const ImageInfo& src, const ImageInfo& dst, SubresourceCopies& copies);

/// True when every subresource of `view` is a whole subresource of `base` at the same size, so
/// that `base` can serve the request through a plain image view. The addressed range of `base` is
/// written to `out_range`.
bool IsViewOf(const ImageInfo& base, const ImageInfo& view, SubresourceRange& out_range);

/// Extent of a single mip of an image, in texels.
Extent3D MipExtent(const ImageInfo& info, u32 level);

} // namespace VideoCore
