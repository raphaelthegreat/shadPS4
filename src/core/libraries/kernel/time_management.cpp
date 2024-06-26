// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/native_clock.h"
#include "core/libraries/kernel/time_management.h"
#include "core/libraries/libs.h"

namespace Libraries::Kernel {

static u64 initial_ptc;
static std::unique_ptr<Common::NativeClock> clock;

u64 PS4_SYSV_ABI sceKernelGetProcessTime() {
    return clock->GetProcessTimeUS();
}

u64 PS4_SYSV_ABI sceKernelGetProcessTimeCounter() {
    return clock->GetUptime() - initial_ptc;
}

u64 PS4_SYSV_ABI sceKernelGetProcessTimeCounterFrequency() {
    return clock->GetTscFrequency();
}

u64 PS4_SYSV_ABI sceKernelReadTsc() {
    return clock->GetUptime();
}

void timeSymbolsRegister(Core::Loader::SymbolsResolver* sym) {
    clock = std::make_unique<Common::NativeClock>();
    initial_ptc = clock->GetUptime();
    LIB_FUNCTION("4J2sUJmuHZQ", "libkernel", 1, "libkernel", 1, 1, sceKernelGetProcessTime);
    LIB_FUNCTION("fgxnMeTNUtY", "libkernel", 1, "libkernel", 1, 1, sceKernelGetProcessTimeCounter);
    LIB_FUNCTION("BNowx2l588E", "libkernel", 1, "libkernel", 1, 1,
                 sceKernelGetProcessTimeCounterFrequency);
    LIB_FUNCTION("-2IRUCO--PM", "libkernel", 1, "libkernel", 1, 1, sceKernelReadTsc);
}

} // namespace Libraries::Kernel
