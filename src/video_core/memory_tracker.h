// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <memory>
#include "common/types.h"

namespace VideoCore {

class MemoryTracker {
public:
    explicit MemoryTracker();
    ~MemoryTracker();

    void OnGpuMap(VAddr address, size_t size);
    void OnGpuUnmap(VAddr address, size_t size);

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

} // namespace VideoCore
