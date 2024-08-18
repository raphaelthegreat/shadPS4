// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/assert.h"
#include "core/libraries/kernel/threads/mutex.h"
#include "core/libraries/error_codes.h"
#include "core/libraries/libs.h"

namespace Libraries::Kernel {

constexpr static PthreadMutexattrInternal MutexattrDefault = {
    .type = MutexType::ErrorCheck,
    .protocol = 0,
    .ceiling = 0
};

static void EnsureInit(ScePthreadMutex* mutex) {
    if (mutex == nullptr || *mutex != nullptr) {
        return;
    }
    *mutex = new PthreadMutexInternal{nullptr, &MutexattrDefault};
}

int PS4_SYSV_ABI scePthreadMutexInit(ScePthreadMutex* mutex, const ScePthreadMutexattr* mutex_attr,
                                     const char* name) {
    if (mutex == nullptr) {
        return SCE_KERNEL_ERROR_EINVAL;
    }

    const PthreadMutexattrInternal* attr;
    if (mutex_attr == nullptr) {
        attr = &MutexattrDefault;
    } else {
        if (*mutex_attr == nullptr) {
            attr = &MutexattrDefault;
        } else {
            attr = *mutex_attr;
        }
    }

    if (attr->type > MutexType::Adaptive) {
        return SCE_KERNEL_ERROR_EINVAL;
    }
    if (attr->protocol > 2) {
        return SCE_KERNEL_ERROR_EINVAL;
    }

    *mutex = new PthreadMutexInternal{name, attr};
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI scePthreadMutexDestroy(ScePthreadMutex* mutex) {
    if (mutex == nullptr || *mutex == nullptr) {
        return SCE_KERNEL_ERROR_EINVAL;
    }

    delete *mutex;
    *mutex = nullptr;
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI scePthreadMutexLock(ScePthreadMutex* mutex_ptr) {
    EnsureInit(mutex_ptr);
    if (mutex_ptr == nullptr) {
        return SCE_KERNEL_ERROR_EINVAL;
    }
    ScePthreadMutex mutex = *mutex_ptr;
    if (mutex->attr.type != MutexType::Recursive && mutex->owner == scePthreadSelf()) {
        return SCE_KERNEL_ERROR_EDEADLK;
    }
    mutex->mutex.lock();
    mutex->owner = scePthreadSelf();
    mutex->lock_count++;
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI scePthreadMutexTrylock(ScePthreadMutex* mutex_ptr) {
    EnsureInit(mutex_ptr);
    if (mutex_ptr == nullptr) {
        return ORBIS_KERNEL_ERROR_EINVAL;
    }
    ScePthreadMutex mutex = *mutex_ptr;
    if (mutex->attr.type != MutexType::Recursive && mutex->owner == scePthreadSelf()) {
        return SCE_KERNEL_ERROR_EDEADLK;
    }
    if (!mutex->mutex.try_lock()) {
        return SCE_KERNEL_ERROR_EBUSY;
    }
    mutex->owner = scePthreadSelf();
    mutex->lock_count++;
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI scePthreadMutexTimedlock(ScePthreadMutex* mutex_ptr, u64 usecs) {
    EnsureInit(mutex_ptr);
    if (mutex_ptr == nullptr) {
        return SCE_KERNEL_ERROR_EINVAL;
    }
    ScePthreadMutex mutex = *mutex_ptr;
    if (mutex->attr.type != MutexType::Recursive && mutex->owner == scePthreadSelf()) {
        return SCE_KERNEL_ERROR_EDEADLK;
    }
    if (!mutex->mutex.try_lock_for(std::chrono::microseconds(usecs))) {
        return SCE_KERNEL_ERROR_ETIMEDOUT;
    }
    mutex->owner = scePthreadSelf();
    mutex->lock_count++;
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI scePthreadMutexUnlock(ScePthreadMutex* mutex_ptr) {
    EnsureInit(mutex_ptr);
    if (mutex_ptr == nullptr) {
        return SCE_KERNEL_ERROR_EINVAL;
    }
    ScePthreadMutex mutex = *mutex_ptr;
    if (mutex->owner != scePthreadSelf()) {
        return SCE_KERNEL_ERROR_EPERM;
    }
    if (--mutex->lock_count == 0) {
        mutex->owner = nullptr;
    }
    mutex->mutex.unlock();
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI scePthreadMutexattrInit(ScePthreadMutexattr* out_attr) {
    auto attr = new PthreadMutexattrInternal{};
    std::memcpy(attr, &MutexattrDefault, sizeof(PthreadMutexattrInternal));
    *out_attr = attr;
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI scePthreadMutexattrSettype(ScePthreadMutexattr* attr, MutexType type) {
    if (!attr || !*attr || type > MutexType::Adaptive) {
        return SCE_KERNEL_ERROR_EINVAL;
    }
    (*attr)->type = type;
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI scePthreadMutexattrSetprotocol(ScePthreadMutexattr* attr, int protocol) {
    if (!attr || !*attr || protocol >= 3) {
        return SCE_KERNEL_ERROR_EINVAL;
    }
    (*attr)->protocol = protocol;
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI scePthreadMutexattrDestroy(ScePthreadMutexattr* attr) {
    if (!attr || !*attr) {
        return SCE_KERNEL_ERROR_EINVAL;
    }
    delete *attr;
    *attr = nullptr;
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI posix_pthread_mutex_init(ScePthreadMutex* mutex, const ScePthreadMutexattr* attr) {
    s32 result = scePthreadMutexInit(mutex, attr, nullptr);
    if (result < 0) {
        result -= SCE_KERNEL_ERROR_UNKNOWN;
    }
    return result;
}

s32 PS4_SYSV_ABI posix_pthread_mutex_lock(ScePthreadMutex* mutex) {
    s32 result = scePthreadMutexLock(mutex);
    if (result < 0) {
        result -= SCE_KERNEL_ERROR_UNKNOWN;
    }
    return result;
}

s32 PS4_SYSV_ABI posix_pthread_mutex_trylock(ScePthreadMutex* mutex) {
    s32 result = scePthreadMutexTrylock(mutex);
    if (result < 0) {
        result -= SCE_KERNEL_ERROR_UNKNOWN;
    }
    return result;
}

s32 PS4_SYSV_ABI posix_pthread_mutex_unlock(ScePthreadMutex* mutex) {
    s32 result = scePthreadMutexUnlock(mutex);
    if (result < 0) {
        result -= SCE_KERNEL_ERROR_UNKNOWN;
    }
    return result;
}

s32 PS4_SYSV_ABI posix_pthread_mutex_destroy(ScePthreadMutex* mutex) {
    s32 result = scePthreadMutexDestroy(mutex);
    if (result < 0) {
        result -= SCE_KERNEL_ERROR_UNKNOWN;
    }
    return result;
}

s32 PS4_SYSV_ABI posix_pthread_mutexattr_init(ScePthreadMutexattr* attr) {
    s32 result = scePthreadMutexattrInit(attr);
    if (result < 0) {
        result -= SCE_KERNEL_ERROR_UNKNOWN;
    }
    return result;
}

s32 PS4_SYSV_ABI posix_pthread_mutexattr_settype(ScePthreadMutexattr* attr, MutexType type) {
    s32 result = scePthreadMutexattrSettype(attr, type);
    if (result < 0) {
        result -= SCE_KERNEL_ERROR_UNKNOWN;
    }
    return result;
}

s32 PS4_SYSV_ABI posix_pthread_mutexattr_destroy(ScePthreadMutexattr* attr) {
    s32 result = scePthreadMutexattrDestroy(attr);
    if (result < 0) {
        result -= SCE_KERNEL_ERROR_UNKNOWN;
    }
    return result;
}

s32 PS4_SYSV_ABI posix_pthread_mutexattr_setprotocol(ScePthreadMutexattr* attr, int protocol) {
    s32 result = scePthreadMutexattrSetprotocol(attr, protocol);
    if (result < 0) {
        UNREACHABLE();
    }
    return result;
}

void RegisterMutex(Core::Loader::SymbolsResolver* sym) {
    LIB_FUNCTION("cmo1RIYva9o", "libkernel", 1, "libkernel", 1, 1, scePthreadMutexInit);
    LIB_FUNCTION("2Of0f+3mhhE", "libkernel", 1, "libkernel", 1, 1, scePthreadMutexDestroy);
    LIB_FUNCTION("F8bUHwAG284", "libkernel", 1, "libkernel", 1, 1, scePthreadMutexattrInit);
    LIB_FUNCTION("smWEktiyyG0", "libkernel", 1, "libkernel", 1, 1, scePthreadMutexattrDestroy);
    LIB_FUNCTION("iMp8QpE+XO4", "libkernel", 1, "libkernel", 1, 1, scePthreadMutexattrSettype);
    LIB_FUNCTION("1FGvU0i9saQ", "libkernel", 1, "libkernel", 1, 1, scePthreadMutexattrSetprotocol);
    LIB_FUNCTION("9UK1vLZQft4", "libkernel", 1, "libkernel", 1, 1, scePthreadMutexLock);
    LIB_FUNCTION("tn3VlD0hG60", "libkernel", 1, "libkernel", 1, 1, scePthreadMutexUnlock);
    LIB_FUNCTION("upoVrzMHFeE", "libkernel", 1, "libkernel", 1, 1, scePthreadMutexTrylock);
    LIB_FUNCTION("IafI2PxcPnQ", "libkernel", 1, "libkernel", 1, 1, scePthreadMutexTimedlock);
    LIB_FUNCTION("ttHNfU+qDBU", "libScePosix", 1, "libkernel", 1, 1, posix_pthread_mutex_init);
    LIB_FUNCTION("7H0iTOciTLo", "libScePosix", 1, "libkernel", 1, 1, posix_pthread_mutex_lock);
    LIB_FUNCTION("2Z+PpY6CaJg", "libScePosix", 1, "libkernel", 1, 1, posix_pthread_mutex_unlock);
    LIB_FUNCTION("ltCfaGr2JGE", "libScePosix", 1, "libkernel", 1, 1, posix_pthread_mutex_destroy);
    LIB_FUNCTION("7H0iTOciTLo", "libkernel", 1, "libkernel", 1, 1, posix_pthread_mutex_lock);
    LIB_FUNCTION("2Z+PpY6CaJg", "libkernel", 1, "libkernel", 1, 1, posix_pthread_mutex_unlock);
    LIB_FUNCTION("K-jXhbt2gn4", "libScePosix", 1, "libkernel", 1, 1, posix_pthread_mutex_trylock);
    LIB_FUNCTION("dQHWEsJtoE4", "libScePosix", 1, "libkernel", 1, 1, posix_pthread_mutexattr_init);
    LIB_FUNCTION("mDmgMOGVUqg", "libScePosix", 1, "libkernel", 1, 1,
                 posix_pthread_mutexattr_settype);
    LIB_FUNCTION("5txKfcMUAok", "libScePosix", 1, "libkernel", 1, 1,
                 posix_pthread_mutexattr_setprotocol);
    LIB_FUNCTION("HF7lK46xzjY", "libScePosix", 1, "libkernel", 1, 1,
                 posix_pthread_mutexattr_destroy);
}

} // namespace Libraries::Kernel
