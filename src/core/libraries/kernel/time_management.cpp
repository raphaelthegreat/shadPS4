// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <thread>
#include <pthread.h>
#include "common/assert.h"
#include "common/native_clock.h"
#include "core/libraries/kernel/time_management.h"
#include "core/libraries/libs.h"

#ifdef _WIN64
#include <windows.h>
#endif

namespace Libraries::Kernel {

static u64 initial_ptc;
static std::unique_ptr<Common::NativeClock> clock;

u64 PS4_SYSV_ABI sceKernelGetTscFrequency() {
    return clock->GetTscFrequency();
}

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

int PS4_SYSV_ABI sceKernelUsleep(u32 microseconds) {
    ASSERT(microseconds >= 1000);
    std::this_thread::sleep_for(std::chrono::microseconds(microseconds));
    return 0;
}

int PS4_SYSV_ABI posix_usleep(u32 microseconds) {
    ASSERT(microseconds >= 1000);
    std::this_thread::sleep_for(std::chrono::microseconds(microseconds));
    return 0;
}

u32 PS4_SYSV_ABI sceKernelSleep(u32 seconds) {
    std::this_thread::sleep_for(std::chrono::seconds(seconds));
    return 0;
}

#define FILETIME_1970 116444736000000000ull /* seconds between 1/1/1601 and 1/1/1970 */
#define HECTONANOSEC_PER_SEC 10000000ull

struct SceKernelTimeval {
    time_t tv_sec;
    s64 tv_usec;
};

struct timezone {
    int tz_minuteswest; /* minutes W of Greenwich */
    int tz_dsttime;     /* type of dst correction */
};

struct timeval {
    u64 tv_sec;
    u64 tv_usec;
};

#if defined(_MSC_VER) || defined(_MSC_EXTENSIONS)
#define DELTA_EPOCH_IN_MICROSECS 11644473600000000Ui64
#else
#define DELTA_EPOCH_IN_MICROSECS 11644473600000000ULL
#endif

int PS4_SYSV_ABI gettimeofday(struct timeval* tv, struct timezone* tz) {
    FILETIME ft;
    unsigned __int64 tmpres = 0;
    static int tzflag = 0;

    if (NULL != tv) {
        GetSystemTimePreciseAsFileTime(&ft);

        tmpres |= ft.dwHighDateTime;
        tmpres <<= 32;
        tmpres |= ft.dwLowDateTime;

        tmpres /= 10; /*convert into microseconds*/
        /*converting file time to unix epoch*/
        tmpres -= DELTA_EPOCH_IN_MICROSECS;
        tv->tv_sec = (long)(tmpres / 1000000UL);
        tv->tv_usec = (long)(tmpres % 1000000UL);
    }

    if (NULL != tz) {
        if (!tzflag) {
            _tzset();
            tzflag++;
        }
        tz->tz_minuteswest = _timezone / 60;
        tz->tz_dsttime = _daylight;
    }

    return 0;
}

int PS4_SYSV_ABI posix_nanosleep(timespec* requested_time, timespec* remaining) {
    const s32 ret = nanosleep(requested_time, remaining);
    if (ret == -1) {
        LOG_ERROR(Lib_Kernel, "Bad\n");
    }
    return ret;
}

void timeSymbolsRegister(Core::Loader::SymbolsResolver* sym) {
    clock = std::make_unique<Common::NativeClock>();
    initial_ptc = clock->GetUptime();
    LIB_FUNCTION("4J2sUJmuHZQ", "libkernel", 1, "libkernel", 1, 1, sceKernelGetProcessTime);
    LIB_FUNCTION("fgxnMeTNUtY", "libkernel", 1, "libkernel", 1, 1, sceKernelGetProcessTimeCounter);
    LIB_FUNCTION("BNowx2l588E", "libkernel", 1, "libkernel", 1, 1,
                 sceKernelGetProcessTimeCounterFrequency);
    LIB_FUNCTION("-2IRUCO--PM", "libkernel", 1, "libkernel", 1, 1, sceKernelReadTsc);
    LIB_FUNCTION("1j3S3n-tTW4", "libkernel", 1, "libkernel", 1, 1, sceKernelGetTscFrequency);
    LIB_FUNCTION("n88vx3C5nW8", "libScePosix", 1, "libkernel", 1, 1, gettimeofday);
    LIB_FUNCTION("n88vx3C5nW8", "libkernel", 1, "libkernel", 1, 1, gettimeofday);
    LIB_FUNCTION("1jfXLRVzisc", "libkernel", 1, "libkernel", 1, 1, sceKernelUsleep);
    LIB_FUNCTION("QcteRwbsnV0", "libScePosix", 1, "libkernel", 1, 1, posix_usleep);
    LIB_FUNCTION("-ZR+hG7aDHw", "libkernel", 1, "libkernel", 1, 1, sceKernelSleep);
    LIB_FUNCTION("0wu33hunNdE", "libScePosix", 1, "libkernel", 1, 1, sceKernelSleep);
    LIB_FUNCTION("yS8U2TGCe1A", "libkernel", 1, "libkernel", 1, 1, posix_nanosleep);
}

} // namespace Libraries::Kernel
