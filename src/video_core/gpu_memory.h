// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <memory>

#include "common/types.h"
#include "video_core/page_watchers.h"

namespace Vulkan {
class Rasterizer;
}

namespace VideoCore {

/**
 * Coordinates coherency between guest memory and the GPU caches that shadow it.
 */
class GpuMemory {
public:
    explicit GpuMemory(Vulkan::Rasterizer* rasterizer);
    ~GpuMemory();

    /// Register a range of mapped gpu memory.
    void OnGpuMap(VAddr address, size_t size);

    /// Unregister a range of gpu memory that was unmapped.
    void OnGpuUnmap(VAddr address, size_t size);

    /// Updates watchers in the pages touching the specified region.
    void UpdatePageWatchers(VAddr addr, u64 size, PageOp write_op) const;

    /// Updates watchers in the pages touching the inclusive bounds using a mask.
    void UpdatePageWatchersForRegion(VAddr base_addr, const Bounds& bounds,
                                     const RegionBits& write_mask, const RegionBits& read_mask,
                                     PageOp write_op, PageOp read_op) const;

private:
    std::unique_ptr<PageWatchers> watchers;
};

} // namespace VideoCore
