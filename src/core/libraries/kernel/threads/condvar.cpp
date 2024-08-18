// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/libraries/kernel/threads/condvar.h"
#include "core/libraries/kernel/threads/mutex.h"
#include "core/libraries/libs.h"
#include "core/libraries/error_codes.h"

namespace Libraries::Kernel {

constexpr static PthreadCondAttrInternal CondAttrDefault = {
    .pshared = PTHREAD_PROCESS_PRIVATE,
    .clockid = CLOCK_REALTIME
};

static void EnsureInit(ScePthreadCond* cond) {
    if (cond == nullptr || *cond != nullptr) {
        return;
    }
    *cond = new PthreadCondInternal{nullptr, &CondAttrDefault};
}

s32 PS4_SYSV_ABI scePthreadCondInit(ScePthreadCond* cond, const ScePthreadCondattr* cond_attr,
                                    const char* name) {
    if (cond == nullptr) {
        return SCE_KERNEL_ERROR_EINVAL;
    }

    const PthreadCondAttrInternal* attr;
    if (cond_attr == nullptr) {
        attr = &CondAttrDefault;
    } else {
        if (*cond_attr == nullptr) {
            attr = &CondAttrDefault;
        } else {
            attr = *cond_attr;
        }
    }

    static constexpr size_t MaxNameLen = 32;
    if (name && std::strlen(name) > MaxNameLen) {
        return SCE_KERNEL_ERROR_ENAMETOOLONG;
    }

    *cond = new PthreadCondInternal{name, attr};
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI scePthreadCondattrInit(ScePthreadCondattr* out_attr) {
    auto attr = new PthreadCondAttrInternal{};
    std::memcpy(attr, &CondAttrDefault, sizeof(PthreadCondAttrInternal));
    *out_attr = attr;
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI scePthreadCondBroadcast(ScePthreadCond* cond) {
    EnsureInit(cond);
    if (cond == nullptr) {
        return SCE_KERNEL_ERROR_EINVAL;
    }
    (*cond)->cond.notify_all();
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI scePthreadCondTimedwait(ScePthreadCond* cond, ScePthreadMutex* mutex, u64 usec) {
    EnsureInit(cond);
    if (cond == nullptr || mutex == nullptr || *mutex == nullptr) {
        return SCE_KERNEL_ERROR_EINVAL;
    }
    if ((*mutex)->owner != scePthreadSelf()) {
        return SCE_KERNEL_ERROR_EPERM;
    }
    const auto status = (*cond)->cond.wait_for((*mutex)->mutex, std::chrono::microseconds(usec));
    if (status == std::cv_status::timeout) {
        return SCE_KERNEL_ERROR_ETIMEDOUT;
    }
    return ORBIS_OK;
}

int PS4_SYSV_ABI scePthreadCondDestroy(ScePthreadCond* cond) {
    if (cond == nullptr) {
        return SCE_KERNEL_ERROR_EINVAL;
    }
    delete *cond;
    *cond = nullptr;
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI scePthreadCondSignal(ScePthreadCond* cond) {
    EnsureInit(cond);
    if (cond == nullptr) {
        return SCE_KERNEL_ERROR_EINVAL;
    }
    (*cond)->cond.notify_one();
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI scePthreadCondWait(ScePthreadCond* cond, ScePthreadMutex* mutex) {
    EnsureInit(cond);
    if (cond == nullptr || mutex == nullptr || *mutex == nullptr) {
        return SCE_KERNEL_ERROR_EINVAL;
    }
    (*cond)->cond.wait((*mutex)->mutex);
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI scePthreadCondattrDestroy(ScePthreadCondattr* attr) {
    if (attr == nullptr) {
        return SCE_KERNEL_ERROR_EINVAL;
    }
    delete *attr;
    *attr = nullptr;
    return ORBIS_OK;
}

int PS4_SYSV_ABI posix_pthread_cond_init(ScePthreadCond* cond, const ScePthreadCondattr* attr) {
    s32 result = scePthreadCondInit(cond, attr, "NoName");
    if (result < 0) {
        result -= SCE_KERNEL_ERROR_UNKNOWN;
    }
    return result;
}

s32 PS4_SYSV_ABI posix_pthread_cond_signal(ScePthreadCond* cond) {
    s32 result = scePthreadCondSignal(cond);
    if (result < 0) {
        result -= SCE_KERNEL_ERROR_UNKNOWN;
    }
    return result;
}

s32 PS4_SYSV_ABI posix_pthread_cond_wait(ScePthreadCond* cond, ScePthreadMutex* mutex) {
    s32 result = scePthreadCondWait(cond, mutex);
    if (result < 0) {
        result -= SCE_KERNEL_ERROR_UNKNOWN;
    }
    return result;
}

s32 PS4_SYSV_ABI posix_pthread_cond_timedwait(ScePthreadCond* cond, ScePthreadMutex* mutex,
                                              u64 usec) {
    s32 result = scePthreadCondTimedwait(cond, mutex, usec);
    if (result < 0) {
        result -= SCE_KERNEL_ERROR_UNKNOWN;
    }
    return result;
}

s32 PS4_SYSV_ABI posix_pthread_cond_broadcast(ScePthreadCond* cond) {
    s32 result = scePthreadCondBroadcast(cond);
    if (result != 0) {
        result -= SCE_KERNEL_ERROR_UNKNOWN;
    }
    return result;
}

s32 PS4_SYSV_ABI posix_pthread_cond_destroy(ScePthreadCond* cond) {
    s32 result = scePthreadCondDestroy(cond);
    if (result < 0) {
        result -= SCE_KERNEL_ERROR_UNKNOWN;
    }
    return result;
}

s32 PS4_SYSV_ABI posix_pthread_condattr_init(ScePthreadCondattr* attr) {
    s32 result = scePthreadCondattrInit(attr);
    if (result < 0) {
        result -= SCE_KERNEL_ERROR_UNKNOWN;
    }
    return result;
}

s32 PS4_SYSV_ABI posix_pthread_condattr_destroy(ScePthreadCondattr* attr) {
    s32 result = scePthreadCondattrDestroy(attr);
    if (result < 0) {
        result -= SCE_KERNEL_ERROR_UNKNOWN;
    }
    return result;
}

s32 PS4_SYSV_ABI posix_pthread_condattr_setclock(ScePthreadCondattr* attr, s32 clock) {
    if (!attr || !*attr || clock >= 5 || clock == 3) {
        return SCE_KERNEL_ERROR_EINVAL;
    }
    (*attr)->clockid = clock;
    return SCE_OK;
}

void RegisterCondvar(Core::Loader::SymbolsResolver* sym) {
    LIB_FUNCTION("2Tb92quprl0", "libkernel", 1, "libkernel", 1, 1, scePthreadCondInit);
    LIB_FUNCTION("m5-2bsNfv7s", "libkernel", 1, "libkernel", 1, 1, scePthreadCondattrInit);
    LIB_FUNCTION("JGgj7Uvrl+A", "libkernel", 1, "libkernel", 1, 1, scePthreadCondBroadcast);
    LIB_FUNCTION("WKAXJ4XBPQ4", "libkernel", 1, "libkernel", 1, 1, scePthreadCondWait);
    LIB_FUNCTION("waPcxYiR3WA", "libkernel", 1, "libkernel", 1, 1, scePthreadCondattrDestroy);
    LIB_FUNCTION("kDh-NfxgMtE", "libkernel", 1, "libkernel", 1, 1, scePthreadCondSignal);
    LIB_FUNCTION("BmMjYxmew1w", "libkernel", 1, "libkernel", 1, 1, scePthreadCondTimedwait);
    LIB_FUNCTION("g+PZd2hiacg", "libkernel", 1, "libkernel", 1, 1, scePthreadCondDestroy);
    LIB_FUNCTION("0TyVk4MSLt0", "libScePosix", 1, "libkernel", 1, 1, posix_pthread_cond_init);
    LIB_FUNCTION("2MOy+rUfuhQ", "libScePosix", 1, "libkernel", 1, 1, posix_pthread_cond_signal);
    LIB_FUNCTION("RXXqi4CtF8w", "libScePosix", 1, "libkernel", 1, 1, posix_pthread_cond_destroy);
    LIB_FUNCTION("Op8TBGY5KHg", "libkernel", 1, "libkernel", 1, 1, posix_pthread_cond_wait);
    LIB_FUNCTION("Op8TBGY5KHg", "libScePosix", 1, "libkernel", 1, 1, posix_pthread_cond_wait);
    LIB_FUNCTION("mkx2fVhNMsg", "libkernel", 1, "libkernel", 1, 1, posix_pthread_cond_broadcast);
    LIB_FUNCTION("27bAgiJmOh0", "libScePosix", 1, "libkernel", 1, 1, posix_pthread_cond_timedwait);
    LIB_FUNCTION("mkx2fVhNMsg", "libScePosix", 1, "libkernel", 1, 1, posix_pthread_cond_broadcast);
    LIB_FUNCTION("mKoTx03HRWA", "libScePosix", 1, "libkernel", 1, 1, posix_pthread_condattr_init);
    LIB_FUNCTION("dJcuQVn6-Iw", "libScePosix", 1, "libkernel", 1, 1,
                 posix_pthread_condattr_destroy);
    LIB_FUNCTION("EjllaAqAPZo", "libScePosix", 1, "libkernel", 1, 1,
                 posix_pthread_condattr_setclock);
}

} // namespace Libraries::Kernel
