// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/types.h"

namespace Core::Loader {
class SymbolsResolver;
}

namespace Libraries::Kernel {

void ErrSceToPosix(int result);
int ErrnoToSceKernelError(int e);
void SetPosixErrno(int e);

struct OrbisKernelUuid {
    u32 timeLow;
    u16 timeMid;
    u16 timeHiAndVersion;
    u8 clockSeqHiAndReserved;
    u8 clockSeqLow;
    u8 node[6];
};

int* PS4_SYSV_ABI __Error();
int PS4_SYSV_ABI sceKernelGetCompiledSdkVersion(int* ver);

template <bool set_errno, class F, F f>
struct PosixWrapperImpl;

template <bool set_errno, class R, class... Args, PS4_SYSV_ABI R (*f)(Args...)>
struct PosixWrapperImpl<set_errno, PS4_SYSV_ABI R (*)(Args...), f> {
    static R PS4_SYSV_ABI wrap(Args... args) {
        s32 result = f(args...);
        if (result < 0) {
            result -= SCE_KERNEL_ERROR_UNKNOWN;
            if constexpr (set_errno) {
                *__Error() = result;
            }
        }
        return result;
    }
};

template <bool set_errno, class F, F f>
constexpr auto PosixWrapper = PosixWrapperImpl<set_errno, F, f>::wrap;

void LibKernel_Register(Core::Loader::SymbolsResolver* sym);

} // namespace Libraries::Kernel
