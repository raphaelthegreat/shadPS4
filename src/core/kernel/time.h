// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/types.h"

namespace Kernel {

struct Timespec {
    u64	tv_sec;
    u64	tv_nsec;
};

struct Timeval {
    u64	tv_sec;
    u64	tv_usec;
};

} // namespace Kernel
