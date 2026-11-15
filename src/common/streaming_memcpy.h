// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/types.h"

namespace Common {

/// Copies from write-combined memory using streaming loads, which are much faster than regular
/// loads on uncached memory.
void StreamingLoadMemcpy(u8* dst, const u8* src, u64 size);

} // namespace Common
