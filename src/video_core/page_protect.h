// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <memory>

#include "common/types.h"
#include "core/address_space.h"

namespace Vulkan {
class Rasterizer;
}

namespace VideoCore {

class GpuMemory;

/**
 * Applies host access protection to guest pages and reports the resulting faults back to the
 * rasterizer. The policy of which pages need protecting lives in GpuMemory.
 */
class PageProtector {
public:
    virtual ~PageProtector() = default;

    /// Called when a range of guest memory is mapped for gpu access.
    virtual void OnMap(VAddr address, size_t size) {}

    /// Called when a range of guest memory is unmapped.
    virtual void OnUnmap(VAddr address, size_t size) {}

    /// Applies host protection to a range of guest memory.
    virtual void Protect(VAddr address, size_t size, Core::MemoryPermission perms) = 0;

protected:
    inline static Vulkan::Rasterizer* rasterizer;
    inline static GpuMemory* gpu_memory;
};

/// Creates the protector backed by the best available host protection method.
std::unique_ptr<PageProtector> CreatePageProtector(Vulkan::Rasterizer* rasterizer,
                                                   GpuMemory& gpu_memory);

#ifdef __linux__
/// Creates a protector backed by userfaultfd, throws if unsupported.
std::unique_ptr<PageProtector> CreateUffdPageProtector(Vulkan::Rasterizer* rasterizer,
                                                       GpuMemory& gpu_memory);
#endif

/// Creates a protector backed by access violation signals.
std::unique_ptr<PageProtector> CreateSignalPageProtector(Vulkan::Rasterizer* rasterizer,
                                                         GpuMemory& gpu_memory);

} // namespace VideoCore
