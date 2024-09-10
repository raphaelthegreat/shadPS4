// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/assert.h"
#include "common/native_clock.h"
#include "core/kernel/thread.h"
#include "core/kernel/kern_umtx.h"

namespace Kernel {

static std::unique_ptr<Common::NativeClock> clock{std::make_unique<Common::NativeClock>()};
static std::array<std::array<UmtxqChain, UMtxChains>, 2> umtxq_chains;

#define UMTX_SHARED_QUEUE	0
#define UMTX_EXCLUSIVE_QUEUE	1

static inline UmtxqChain* umtxq_getchain(UmtxKey *key) {
    if (key->type <= UmtxKeyType::Sem) {
        return (&umtxq_chains[1][key->hash]);
    }
    return (&umtxq_chains[0][key->hash]);
}

/*
 * Lock a chain.
 */
static inline void umtxq_lock(UmtxKey* key)
{
    UmtxqChain* uc = umtxq_getchain(key);
    uc->uc_lock.lock();
}

/*
 * Unlock a chain.
 */
static inline void umtxq_unlock(UmtxKey *key)
{
    UmtxqChain* uc = umtxq_getchain(key);
    uc->uc_lock.unlock();
}

static __inline void cpu_spinwait(void) {
    __asm __volatile("pause");
}

/*
 * Set chain to busy state when following operation
 * may be blocked (kernel mutex can not be used).
 */
static inline void umtxq_busy(UmtxKey* key)
{
    UmtxqChain *uc;

    uc = umtxq_getchain(key);
    mtx_assert(&uc->uc_lock, MA_OWNED);
    if (uc->uc_busy) {
        int count = BUSY_SPINS;
        if (count > 0) {
            umtxq_unlock(key);
            while (uc->uc_busy && --count > 0)
                cpu_spinwait();
            umtxq_lock(key);
        }
        while (uc->uc_busy) {
            uc->uc_waiters++;
            msleep(uc, &uc->uc_lock, 0, "umtxqb", 0);
            uc->uc_waiters--;
        }
    }
    uc->uc_busy = 1;
}

/*
 * Unbusy a chain.
 */
static inline void umtxq_unbusy(UmtxKey *key)
{
    UmtxqChain *uc;

    uc = umtxq_getchain(key);
    mtx_assert(&uc->uc_lock, MA_OWNED);
    ASSERT_MSG(uc->uc_busy != 0, "not busy");
    uc->uc_busy = 0;
    if (uc->uc_waiters)
        wakeup_one(uc);
}

static inline void umtxq_insert_queue(UmtxQ* uq, int q)
{
    UmtxqQueue *uh;
    UmtxqChain *uc;

    uc = umtxq_getchain(&uq->uq_key);
    UMTXQ_LOCKED_ASSERT(uc);
    ASSERT_MSG((uq->uq_flags & UQF_UMTXQ) == 0, "umtx_q is already on queue");
    uh = umtxq_queue_lookup(&uq->uq_key, q);
    if (uh != NULL) {
        LIST_INSERT_HEAD(&uc->uc_spare_queue, uq->uq_spare_queue, link);
    } else {
        uh = uq->uq_spare_queue;
        uh->key = uq->uq_key;
        LIST_INSERT_HEAD(&uc->uc_queue[q], uh, link);
    }
    uq->uq_spare_queue = NULL;

    TAILQ_INSERT_TAIL(&uh->head, uq, uq_link);
    uh->length++;
    uq->uq_flags |= UQF_UMTXQ;
    uq->uq_cur_queue = uh;
    return;
}

static inline void
umtxq_remove_queue(UmtxQ *uq, int q)
{
    UmtxqQueue *uh;
    UmtxqChain *uc;

    uc = umtxq_getchain(&uq->uq_key);
    UMTXQ_LOCKED_ASSERT(uc);
    if (uq->uq_flags & UQF_UMTXQ) {
        uh = uq->uq_cur_queue;
        TAILQ_REMOVE(&uh->head, uq, uq_link);
        uh->length--;
        uq->uq_flags &= ~UQF_UMTXQ;
        if (TAILQ_EMPTY(&uh->head)) {
            ASSERT_MSG(uh->length == 0, "inconsistent umtxq_queue length");
            LIST_REMOVE(uh, link);
        } else {
            uh = LIST_FIRST(&uc->uc_spare_queue);
            ASSERT_MSG(uh != NULL, "uc_spare_queue is empty");
            LIST_REMOVE(uh, link);
        }
        uq->uq_spare_queue = uh;
        uq->uq_cur_queue = NULL;
    }
}

static inline int umtxq_sleep(UmtxQ* uq, const char *wmesg, int timo)
{
    UmtxqChain *uc;
    int error;

    uc = umtxq_getchain(&uq->uq_key);
    UMTXQ_LOCKED_ASSERT(uc);
    if (!(uq->uq_flags & UQF_UMTXQ))
        return (0);
    error = msleep(uq, &uc->uc_lock, PCATCH, wmesg, timo);
    if (error == EWOULDBLOCK)
        error = ETIMEDOUT;
    return (error);
}

#define umtxq_insert(uq)	umtxq_insert_queue((uq), UMTX_SHARED_QUEUE)
#define umtxq_remove(uq)	umtxq_remove_queue((uq), UMTX_SHARED_QUEUE)

s32 UMutex::TryLock(Thread* thread) {

}

s32 UMutex::Lock(Thread *thread, Timespec* timeout) {
    if (timeout && timeout->tv_nsec >= 1000000000 || timeout->tv_nsec < 0) {
        return EINVAL;
    }

}

s32 UMutex::Unlock(Thread *thread) {

}

s32 UMutex::SetCeiling(Thread *thread, u32 ceiling, u32* old_ceiling) {

}

s32 UMutex::DoLock(Thread* thread, Timespec *timeout, LockMode mode) {
    const auto do_lock_mutex = [&] {
        switch(m_flags & (UMutexFlags::PrioInherit | UMutexFlags::PrioProtect)) {
        case UMutexFlags::None:
            return DoLockNormal(thread, timo, mode);
        case UMutexFlags::PrioInherit:
            return _do_lock_pi(thread, timo, mode);
        case UMutexFlags::PrioProtect:
            return _do_lock_pp(thread, timo, mode);
        default:
            return EINVAL;
        }
    };

    if (m_flags == UMutexFlags::Invalid) {
        return EFAULT;
    }

    if (!timeout) {
        s32 error = do_lock_mutex();
        /* Mutex locking is restarted if it is interrupted. */
        if (error == EINTR && mode != LockMode::Wait) {
            error = ERESTART;
        }
        return error;
    }

    s32 error = 0;
    const u64 start = clock->GetTimeNS();
    //getnanouptime(&ts);
    timespecadd(&ts, timeout);
    TIMESPEC_TO_TIMEVAL(&tv, timeout);
    for (;;) {
        error = do_lock_mutex();
        if (error != ETIMEDOUT) {
            break;
        }
        const u64 now = clock->GetTimeNS();
        if (timespeccmp(&ts2, &ts, >=)) {
            error = ETIMEDOUT;
            break;
        }
        ts3 = ts;
        timespecsub(&ts3, &ts2);
        TIMESPEC_TO_TIMEVAL(&tv, &ts3);
    }
    /* Timed-locking is not restarted. */
    if (error == ERESTART) {
        error = EINTR;
    }
    return error;
}

s32 UMutex::DoLockNormal(Thread* thread, int timo, LockMode mode) {
    UmtxQ *uq;
    uint32_t owner, old;
    int error = 0;

    const u32 id = thread->td_tid;
    uq = td->td_umtxq;

    /*
     * Care must be exercised when dealing with umtx structure. It
     * can fault on any access.
     */
    for (;;) {
        if (mode == LockMode::Wait) {
            if (m_owner == UMutexOwner::Unowned || m_owner == UMutexOwner::Contested) {
                return 0;
            }
        } else {
            owner = casuword32(&m->m_owner, UMUTEX_UNOWNED, id);

            /* The acquire succeeded. */
            if (owner == UMutexOwner::Unowned) {
                return (0);
            }

            /* The address was invalid. */
            if (owner == -1)
                return (EFAULT);

            /* If no one owns it but it is contested try to acquire it. */
            if (owner == UMutexOwner::Contested) {
                owner = casuword32(&m->m_owner,
                                   UMUTEX_CONTESTED, id | UMUTEX_CONTESTED);

                if (owner == UMutexOwner::Contested) {
                    return (0);
                }

                /* The address was invalid. */
                if (owner == -1) {
                    return (EFAULT);
                }

                /* If this failed the lock has changed, restart. */
                continue;
            }
        }

        if ((m_flags & UMUTEX_ERROR_CHECK) != 0 &&
            (owner & ~UMutexOwner::Contested) == id)
            return (EDEADLK);

        if (mode == LockMode::Try) {
            return EBUSY;
        }

        /*
         * If we caught a signal, we have retried and now
         * exit immediately.
         */
        if (error != 0) {
            return (error);
        }

        if ((error = umtx_key_get(m, TYPE_NORMAL_UMUTEX,
                                  GET_SHARE(flags), &uq->uq_key)) != 0)
            return (error);

        umtxq_lock(&uq->uq_key);
        umtxq_busy(&uq->uq_key);
        umtxq_insert(uq);
        umtxq_unlock(&uq->uq_key);

        /*
         * Set the contested bit so that a release in user space
         * knows to use the system call for unlock.  If this fails
         * either some one else has acquired the lock or it has been
         * released.
         */
        old = casuword32(&m->m_owner, owner, owner | UMUTEX_CONTESTED);

        /* The address was invalid. */
        if (old == -1) {
            umtxq_lock(&uq->uq_key);
            umtxq_remove(uq);
            umtxq_unbusy(&uq->uq_key);
            umtxq_unlock(&uq->uq_key);
            umtx_key_release(&uq->uq_key);
            return EFAULT;
        }

        /*
         * We set the contested bit, sleep. Otherwise the lock changed
         * and we need to retry or we lost a race to the thread
         * unlocking the umtx.
         */
        umtxq_lock(&uq->uq_key);
        umtxq_unbusy(&uq->uq_key);
        if (old == owner)
            error = umtxq_sleep(uq, "umtxn", timo);
        umtxq_remove(uq);
        umtxq_unlock(&uq->uq_key);
        umtx_key_release(&uq->uq_key);
    }

    return (0);
}

} // namespace Kernel
