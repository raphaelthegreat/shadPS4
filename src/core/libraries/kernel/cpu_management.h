// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/types.h"

namespace Libraries::Kernel {

static constexpr int CPU_MAXSIZE = 128;

struct CpuSet {
    long bits[2];
};

int PS4_SYSV_ABI sceKernelIsNeoMode();

} // namespace Libraries::Kernel
