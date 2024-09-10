// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/types.h"

namespace Kernel {

using lwpid_t = s32;

struct Thread {
    lwpid_t td_tid;
};

} // namespace Kernel
