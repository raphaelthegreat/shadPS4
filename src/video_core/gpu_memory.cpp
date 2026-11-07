// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "video_core/gpu_memory.h"

namespace VideoCore {

GpuMemory::GpuMemory(Vulkan::Rasterizer* rasterizer) : watchers{CreatePageWatchers(rasterizer)} {}

GpuMemory::~GpuMemory() = default;

void GpuMemory::OnGpuMap(VAddr address, size_t size) {
    watchers->OnMap(address, size);
}

void GpuMemory::OnGpuUnmap(VAddr address, size_t size) {
    watchers->OnUnmap(address, size);
}

void GpuMemory::UpdatePageWatchers(VAddr addr, u64 size, PageOp write_op) const {
    watchers->UpdatePageWatchers(addr, size, write_op);
}

void GpuMemory::UpdatePageWatchersForRegion(VAddr base_addr, const Bounds& bounds,
                                            const RegionBits& write_mask,
                                            const RegionBits& read_mask, PageOp write_op,
                                            PageOp read_op) const {
    watchers->UpdatePageWatchersForRegion(base_addr, bounds, write_mask, read_mask, write_op,
                                          read_op);
}

} // namespace VideoCore
