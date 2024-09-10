// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <atomic>
#include <list>
#include <limits>
#include <queue>
#include <shared_mutex>
#include <boost/intrusive/list.hpp>

#include "common/types.h"
#include "common/enum.h"
#include "core/kernel/time.h"

namespace Kernel {

struct Thread;

static constexpr size_t GoldenRatioPrime = 2654404609U;
static constexpr size_t UMtxChains = 512;
static constexpr size_t UmtxShifts = 32 - 9;

enum class UmtxKeyType : u32 {
    SimpleWait,
    Cv,
    Sem,
    SimpleLock,
    NormalUmutex,
    PiUmutex,
    PpUmutex,
    Rwlock,
};

/* Key to represent a unique userland synchronous object */
struct UmtxKey {
    int	hash;
    UmtxKeyType	type;
    int	shared;
    union {
        struct {
            struct vm_object *object;
            uintptr_t offset;
        } shared;
        struct {
            void* a;
            uintptr_t b;
        } both;
    } info;

    void Hash() {
        const u32 n = std::bit_cast<uintptr_t>(info.both.a) + info.both.b;
        hash = ((n * GoldenRatioPrime) >> UmtxShifts) % UMtxChains;
    }
};

struct UmtxqQueue;

/* A userland synchronous object user. */
struct UmtxQ {
    UmtxKey uq_key;
    int uq_flags;
    Thread* uq_thread;
    u8 uq_inherited_pri;
    UmtxqQueue* uq_spare_queue;
    UmtxqQueue* uq_cur_queue;
};

/* Per-key wait-queue */
struct UmtxqQueue {
    std::queue<UmtxQ> head;
    UmtxKey key;
    int length;
};

/* Userland lock object's wait-queue chain */
struct UmtxqChain {
    std::mutex uc_lock;
    std::array<std::list<UmtxqQueue>, 2> uc_queue;
    std::list<UmtxqQueue> uc_spare_queue;
    bool uc_busy;
    int uc_waiters;
};

struct UMtx {
    u32	u_owner;
};

enum UMutexOwner : u32 {
    Unowned = 0,
    Contested = 0x80000000,
    RbOwnerdead = Contested | 0x10,
    RbNotrecov = Contested | 0x11,
};

enum class UMutexFlags : u32 {
    Invalid = std::numeric_limits<u32>::max(),
    None = 0,
    PrioInherit = 4,
    PrioProtect = 8,
    Robust = 16,
    NonConsistent = 32,
};
DECLARE_ENUM_FLAG_OPERATORS(UMutexFlags)

enum class LockMode : u32 {
    Try = 1,
    Wait = 2,
};

class UMutex {
public:
    explicit UMutex();

    s32 TryLock(Thread* thread);
    s32 Lock(Thread *thread, Timespec* timeout);
    s32 Unlock(Thread *thread);
    s32 SetCeiling(Thread *thread, u32 ceiling, u32* old_ceiling);

private:
    s32 DoLock(Thread* thread, Timespec *timeout, LockMode mode);
    s32 DoLockNormal(Thread* thread, int timo, LockMode mode);

private:
    std::atomic<u32> m_owner;
    UMutexFlags	m_flags;
    std::array<u32, 2> m_ceilings;
    std::array<u32, 4> m_spare;
};

struct UCond {
    u32	c_has_waiters;
    u32 c_flags;
    u32 c_clockid;
    u32 c_spare[1];
};

struct URWlock {
    s32 rw_state;
    u32 rw_flags;
    u32 rw_blocked_readers;
    u32 rw_blocked_writers;
    u32 rw_spare[4];
};

struct USem {
    u32 _has_waiters;
    u32 _count;
    u32 _flags;
};

} // namespace Kernel
