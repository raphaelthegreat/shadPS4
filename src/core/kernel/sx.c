/*-
 * Copyright (c) 2007 Attilio Rao <attilio@freebsd.org>
 * Copyright (c) 2001 Jason Evans <jasone@freebsd.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice(s), this list of conditions and the following disclaimer as
 *    the first lines of this file unmodified other than the possible
 *    addition of one or more copyright notices.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice(s), this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDER(S) ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT HOLDER(S) BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 */

/*
 * Shared/exclusive locks.  This implementation attempts to ensure
 * deterministic lock granting behavior, so that slocks and xlocks are
 * interleaved.
 *
 * Priority propagation will not generally raise the priority of lock holders,
 * so should not be relied upon in combination with sx locks.
 */

#include "sx.h"
#include "sleepq.h"
#include "proc.h"
#include "systm.h"

#define	ADAPTIVE_SX

/* Handy macros for sleep queues. */
#define	SQ_EXCLUSIVE_QUEUE	0
#define	SQ_SHARED_QUEUE		1

#ifdef ADAPTIVE_SX
#define	ASX_RETRIES		10
#define	ASX_LOOPS		10000
#endif

/*
 * Variations on DROP_GIANT()/PICKUP_GIANT() for use in this file.  We
 * drop Giant anytime we have to sleep or if we adaptively spin.
 */
#define	GIANT_DECLARE							\
int _giantcnt = 0;						\
    WITNESS_SAVE_DECL(Giant)					\

#define	GIANT_SAVE() do {						\
    if (mtx_owned(&Giant)) {					\
        WITNESS_SAVE(&Giant.lock_object, Giant);		\
        while (mtx_owned(&Giant)) {				\
            _giantcnt++;					\
            mtx_unlock(&Giant);				\
    }							\
}								\
} while (0)

#define GIANT_RESTORE() do {						\
    if (_giantcnt > 0) {						\
            mtx_assert(&Giant, MA_NOTOWNED);			\
            while (_giantcnt--)					\
            mtx_lock(&Giant);				\
            WITNESS_RESTORE(&Giant.lock_object, Giant);		\
    }								\
} while (0)

/*
 * Returns true if an exclusive lock is recursed.  It assumes
 * curthread currently has an exclusive lock.
 */
#define	sx_recurse		lock_object.lo_data
#define	sx_recursed(sx)		((sx)->sx_recurse != 0)

    static void	assert_sx(struct lock_object *lock, int what);
static void	db_show_sx(struct lock_object *lock);
static void	lock_sx(struct lock_object *lock, int how);
static int	unlock_sx(struct lock_object *lock);

struct lock_class lock_class_sx = {
    .lc_name = "sx",
    .lc_flags = LC_SLEEPLOCK | LC_SLEEPABLE | LC_RECURSABLE | LC_UPGRADABLE,
    .lc_assert = assert_sx,
    .lc_lock = lock_sx,
    .lc_unlock = unlock_sx,
};

#ifndef INVARIANTS
#define	_sx_assert(sx, what, file, line)
#endif

void
assert_sx(struct lock_object *lock, int what)
{

    sx_assert((struct sx *)lock, what);
}

void
lock_sx(struct lock_object *lock, int how)
{
    struct sx *sx;

    sx = (struct sx *)lock;
    if (how)
        sx_xlock(sx);
    else
        sx_slock(sx);
}

int
unlock_sx(struct lock_object *lock)
{
    struct sx *sx;

    sx = (struct sx *)lock;
    sx_assert(sx, SA_LOCKED | SA_NOTRECURSED);
    if (sx_xlocked(sx)) {
        sx_xunlock(sx);
        return (1);
    } else {
        sx_sunlock(sx);
        return (0);
    }
}

void
sx_sysinit(void *arg)
{
    struct sx_args *sargs = (struct sx_args *)arg;

    sx_init_flags(sargs->sa_sx, sargs->sa_desc, sargs->sa_flags);
}

void
sx_init_flags(struct sx *sx, const char *description, int opts)
{
    int flags;

    MPASS((opts & ~(SX_QUIET | SX_RECURSE | SX_NOWITNESS | SX_DUPOK |
                    SX_NOPROFILE | SX_NOADAPTIVE)) == 0);
    ASSERT_ATOMIC_LOAD_PTR(sx->sx_lock,
                           ("%s: sx_lock not aligned for %s: %p", __func__, description,
                            &sx->sx_lock));

    flags = LO_SLEEPABLE | LO_UPGRADABLE;
    if (opts & SX_DUPOK)
        flags |= LO_DUPOK;
    if (opts & SX_NOPROFILE)
        flags |= LO_NOPROFILE;
    if (!(opts & SX_NOWITNESS))
        flags |= LO_WITNESS;
    if (opts & SX_RECURSE)
        flags |= LO_RECURSABLE;
    if (opts & SX_QUIET)
        flags |= LO_QUIET;

    flags |= opts & SX_NOADAPTIVE;
    sx->sx_lock = SX_LOCK_UNLOCKED;
    sx->sx_recurse = 0;
    lock_init(&sx->lock_object, &lock_class_sx, description, NULL, flags);
}

void
sx_destroy(struct sx *sx)
{

    KASSERT(sx->sx_lock == SX_LOCK_UNLOCKED, ("sx lock still held"));
    KASSERT(sx->sx_recurse == 0, ("sx lock still recursed"));
    sx->sx_lock = SX_LOCK_DESTROYED;
    lock_destroy(&sx->lock_object);
}

int
_sx_slock(struct sx *sx, int opts, const char *file, int line)
{
    int error = 0;

    if (SCHEDULER_STOPPED())
        return (0);
    MPASS(curthread != NULL);
    KASSERT(sx->sx_lock != SX_LOCK_DESTROYED,
            ("sx_slock() of destroyed sx @ %s:%d", file, line));
    WITNESS_CHECKORDER(&sx->lock_object, LOP_NEWORDER, file, line, NULL);
    error = __sx_slock(sx, opts, file, line);
    if (!error) {
        LOCK_LOG_LOCK("SLOCK", &sx->lock_object, 0, 0, file, line);
        WITNESS_LOCK(&sx->lock_object, 0, file, line);
        curthread->td_locks++;
    }

    return (error);
}

int
_sx_try_slock(struct sx *sx, const char *file, int line)
{
    uintptr_t x;

    if (SCHEDULER_STOPPED())
        return (1);

    for (;;) {
        x = sx->sx_lock;
        KASSERT(x != SX_LOCK_DESTROYED,
                ("sx_try_slock() of destroyed sx @ %s:%d", file, line));
        if (!(x & SX_LOCK_SHARED))
            break;
        if (atomic_cmpset_acq_ptr(&sx->sx_lock, x, x + SX_ONE_SHARER)) {
            LOCK_LOG_TRY("SLOCK", &sx->lock_object, 0, 1, file, line);
            WITNESS_LOCK(&sx->lock_object, LOP_TRYLOCK, file, line);
            curthread->td_locks++;
            return (1);
        }
    }

    LOCK_LOG_TRY("SLOCK", &sx->lock_object, 0, 0, file, line);
    return (0);
}

int
_sx_xlock(struct sx *sx, int opts, const char *file, int line)
{
    int error = 0;

    if (SCHEDULER_STOPPED())
        return (0);
    MPASS(curthread != NULL);
    KASSERT(sx->sx_lock != SX_LOCK_DESTROYED,
            ("sx_xlock() of destroyed sx @ %s:%d", file, line));
    WITNESS_CHECKORDER(&sx->lock_object, LOP_NEWORDER | LOP_EXCLUSIVE, file,
                       line, NULL);
    error = __sx_xlock(sx, curthread, opts, file, line);
    if (!error) {
        LOCK_LOG_LOCK("XLOCK", &sx->lock_object, 0, sx->sx_recurse,
                      file, line);
        WITNESS_LOCK(&sx->lock_object, LOP_EXCLUSIVE, file, line);
        curthread->td_locks++;
    }

    return (error);
}

int
_sx_try_xlock(struct sx *sx, const char *file, int line)
{
    int rval;

    if (SCHEDULER_STOPPED())
        return (1);

    MPASS(curthread != NULL);
    KASSERT(sx->sx_lock != SX_LOCK_DESTROYED,
            ("sx_try_xlock() of destroyed sx @ %s:%d", file, line));

    if (sx_xlocked(sx) &&
        (sx->lock_object.lo_flags & LO_RECURSABLE) != 0) {
        sx->sx_recurse++;
        atomic_set_ptr(&sx->sx_lock, SX_LOCK_RECURSED);
        rval = 1;
    } else
        rval = atomic_cmpset_acq_ptr(&sx->sx_lock, SX_LOCK_UNLOCKED,
                                     (uintptr_t)curthread);
    LOCK_LOG_TRY("XLOCK", &sx->lock_object, 0, rval, file, line);
    if (rval) {
        WITNESS_LOCK(&sx->lock_object, LOP_EXCLUSIVE | LOP_TRYLOCK,
                     file, line);
        curthread->td_locks++;
    }

    return (rval);
}

void
_sx_sunlock(struct sx *sx, const char *file, int line)
{

    if (SCHEDULER_STOPPED())
        return;
    MPASS(curthread != NULL);
    KASSERT(sx->sx_lock != SX_LOCK_DESTROYED,
            ("sx_sunlock() of destroyed sx @ %s:%d", file, line));
    _sx_assert(sx, SA_SLOCKED, file, line);
    curthread->td_locks--;
    WITNESS_UNLOCK(&sx->lock_object, 0, file, line);
    LOCK_LOG_LOCK("SUNLOCK", &sx->lock_object, 0, 0, file, line);
    __sx_sunlock(sx, file, line);
    LOCKSTAT_PROFILE_RELEASE_LOCK(LS_SX_SUNLOCK_RELEASE, sx);
}

void
_sx_xunlock(struct sx *sx, const char *file, int line)
{

    if (SCHEDULER_STOPPED())
        return;
    MPASS(curthread != NULL);
    KASSERT(sx->sx_lock != SX_LOCK_DESTROYED,
            ("sx_xunlock() of destroyed sx @ %s:%d", file, line));
    _sx_assert(sx, SA_XLOCKED, file, line);
    curthread->td_locks--;
    WITNESS_UNLOCK(&sx->lock_object, LOP_EXCLUSIVE, file, line);
    LOCK_LOG_LOCK("XUNLOCK", &sx->lock_object, 0, sx->sx_recurse, file,
                  line);
    if (!sx_recursed(sx))
        LOCKSTAT_PROFILE_RELEASE_LOCK(LS_SX_XUNLOCK_RELEASE, sx);
    __sx_xunlock(sx, curthread, file, line);
}

/*
 * Try to do a non-blocking upgrade from a shared lock to an exclusive lock.
 * This will only succeed if this thread holds a single shared lock.
 * Return 1 if if the upgrade succeed, 0 otherwise.
 */
int
_sx_try_upgrade(struct sx *sx, const char *file, int line)
{
    uintptr_t x;
    int success;

    if (SCHEDULER_STOPPED())
        return (1);

    KASSERT(sx->sx_lock != SX_LOCK_DESTROYED,
            ("sx_try_upgrade() of destroyed sx @ %s:%d", file, line));
    _sx_assert(sx, SA_SLOCKED, file, line);

    /*
     * Try to switch from one shared lock to an exclusive lock.  We need
     * to maintain the SX_LOCK_EXCLUSIVE_WAITERS flag if set so that
     * we will wake up the exclusive waiters when we drop the lock.
     */
    x = sx->sx_lock & SX_LOCK_EXCLUSIVE_WAITERS;
    success = atomic_cmpset_ptr(&sx->sx_lock, SX_SHARERS_LOCK(1) | x,
                                (uintptr_t)curthread | x);
    LOCK_LOG_TRY("XUPGRADE", &sx->lock_object, 0, success, file, line);
    if (success) {
        WITNESS_UPGRADE(&sx->lock_object, LOP_EXCLUSIVE | LOP_TRYLOCK,
                        file, line);
    }
    return (success);
}

/*
 * Downgrade an unrecursed exclusive lock into a single shared lock.
 */
void
_sx_downgrade(struct sx *sx, const char *file, int line)
{
    uintptr_t x;
    int wakeup_swapper;

    if (SCHEDULER_STOPPED())
        return;

    KASSERT(sx->sx_lock != SX_LOCK_DESTROYED,
            ("sx_downgrade() of destroyed sx @ %s:%d", file, line));
    _sx_assert(sx, SA_XLOCKED | SA_NOTRECURSED, file, line);
#ifndef INVARIANTS
    if (sx_recursed(sx))
        panic("downgrade of a recursed lock");
#endif

    WITNESS_DOWNGRADE(&sx->lock_object, 0, file, line);

    /*
     * Try to switch from an exclusive lock with no shared waiters
     * to one sharer with no shared waiters.  If there are
     * exclusive waiters, we don't need to lock the sleep queue so
     * long as we preserve the flag.  We do one quick try and if
     * that fails we grab the sleepq lock to keep the flags from
     * changing and do it the slow way.
     *      * We have to lock the sleep queue if there are shared waiters
     * so we can wake them up.
     */
    x = sx->sx_lock;
    if (!(x & SX_LOCK_SHARED_WAITERS) &&
        atomic_cmpset_rel_ptr(&sx->sx_lock, x, SX_SHARERS_LOCK(1) |
                                                   (x & SX_LOCK_EXCLUSIVE_WAITERS))) {
        LOCK_LOG_LOCK("XDOWNGRADE", &sx->lock_object, 0, 0, file, line);
        return;
    }

    /*
     * Lock the sleep queue so we can read the waiters bits
     * without any races and wakeup any shared waiters.
     */
    sleepq_lock(&sx->lock_object);

    /*
     * Preserve SX_LOCK_EXCLUSIVE_WAITERS while downgraded to a single
     * shared lock.  If there are any shared waiters, wake them up.
     */
    wakeup_swapper = 0;
    x = sx->sx_lock;
    atomic_store_rel_ptr(&sx->sx_lock, SX_SHARERS_LOCK(1) |
                                           (x & SX_LOCK_EXCLUSIVE_WAITERS));
    if (x & SX_LOCK_SHARED_WAITERS)
        wakeup_swapper = sleepq_broadcast(&sx->lock_object, SLEEPQ_SX,
                                          0, SQ_SHARED_QUEUE);
    sleepq_release(&sx->lock_object);

    LOCK_LOG_LOCK("XDOWNGRADE", &sx->lock_object, 0, 0, file, line);

    //if (wakeup_swapper)
    //    kick_proc0();
}

/*
 * This function represents the so-called 'hard case' for sx_xlock
 * operation.  All 'easy case' failures are redirected to this.  Note
 * that ideally this would be a static function, but it needs to be
 * accessible from at least sx.h.
 */
int
_sx_xlock_hard(struct sx *sx, uintptr_t tid, int opts, const char *file,
               int line)
{
    GIANT_DECLARE;
#ifdef ADAPTIVE_SX
    volatile struct thread *owner;
    u_int i, spintries = 0;
#endif
    uintptr_t x;
    int error = 0;

    if (SCHEDULER_STOPPED())
        return (0);

    /* If we already hold an exclusive lock, then recurse. */
    if (sx_xlocked(sx)) {
        KASSERT((sx->lock_object.lo_flags & LO_RECURSABLE) != 0,
                ("_sx_xlock_hard: recursed on non-recursive sx %s @ %s:%d\n",
                 sx->lock_object.lo_name, file, line));
        sx->sx_recurse++;
        atomic_set_ptr(&sx->sx_lock, SX_LOCK_RECURSED);
        return (0);
    }

    while (!atomic_cmpset_acq_ptr(&sx->sx_lock, SX_LOCK_UNLOCKED, tid)) {
        lock_profile_obtain_lock_failed(&sx->lock_object, &contested,
                                        &waittime);
#ifdef ADAPTIVE_SX
        /*
         * If the lock is write locked and the owner is
         * running on another CPU, spin until the owner stops
         * running or the state of the lock changes.
         */
        x = sx->sx_lock;
        if ((sx->lock_object.lo_flags & SX_NOADAPTIVE) == 0) {
            if ((x & SX_LOCK_SHARED) == 0) {
                x = SX_OWNER(x);
                owner = (struct thread *)x;
                if (TD_IS_RUNNING(owner)) {
                    GIANT_SAVE();
                    while (SX_OWNER(sx->sx_lock) == x &&
                           TD_IS_RUNNING(owner)) {
                        cpu_spinwait();
                    }
                    continue;
                }
            } else if (SX_SHARERS(x) && spintries < ASX_RETRIES) {
                GIANT_SAVE();
                spintries++;
                for (i = 0; i < ASX_LOOPS; i++) {
                    x = sx->sx_lock;
                    if ((x & SX_LOCK_SHARED) == 0 ||
                        SX_SHARERS(x) == 0)
                        break;
                    cpu_spinwait();
                }
                if (i != ASX_LOOPS)
                    continue;
            }
        }
#endif

        sleepq_lock(&sx->lock_object);
        x = sx->sx_lock;

        /*
         * If the lock was released while spinning on the
         * sleep queue chain lock, try again.
         */
        if (x == SX_LOCK_UNLOCKED) {
            sleepq_release(&sx->lock_object);
            continue;
        }

#ifdef ADAPTIVE_SX
        /*
         * The current lock owner might have started executing
         * on another CPU (or the lock could have changed
         * owners) while we were waiting on the sleep queue
         * chain lock.  If so, drop the sleep queue lock and try
         * again.
         */
        if (!(x & SX_LOCK_SHARED) &&
            (sx->lock_object.lo_flags & SX_NOADAPTIVE) == 0) {
            owner = (struct thread *)SX_OWNER(x);
            if (TD_IS_RUNNING(owner)) {
                sleepq_release(&sx->lock_object);
                continue;
            }
        }
#endif

        /*
         * If an exclusive lock was released with both shared
         * and exclusive waiters and a shared waiter hasn't
         * woken up and acquired the lock yet, sx_lock will be
         * set to SX_LOCK_UNLOCKED | SX_LOCK_EXCLUSIVE_WAITERS.
         * If we see that value, try to acquire it once.  Note
         * that we have to preserve SX_LOCK_EXCLUSIVE_WAITERS
         * as there are other exclusive waiters still.  If we
         * fail, restart the loop.
         */
        if (x == (SX_LOCK_UNLOCKED | SX_LOCK_EXCLUSIVE_WAITERS)) {
            if (atomic_cmpset_acq_ptr(&sx->sx_lock,
                                      SX_LOCK_UNLOCKED | SX_LOCK_EXCLUSIVE_WAITERS,
                                      tid | SX_LOCK_EXCLUSIVE_WAITERS)) {
                sleepq_release(&sx->lock_object);
                break;
            }
            sleepq_release(&sx->lock_object);
            continue;
        }

        /*
         * Try to set the SX_LOCK_EXCLUSIVE_WAITERS.  If we fail,
         * than loop back and retry.
         */
        if (!(x & SX_LOCK_EXCLUSIVE_WAITERS)) {
            if (!atomic_cmpset_ptr(&sx->sx_lock, x,
                                   x | SX_LOCK_EXCLUSIVE_WAITERS)) {
                sleepq_release(&sx->lock_object);
                continue;
            }
        }

        /*
         * Since we have been unable to acquire the exclusive
         * lock and the exclusive waiters flag is set, we have
         * to sleep.
         */
        GIANT_SAVE();
        sleepq_add(&sx->lock_object, NULL, sx->lock_object.lo_name,
                   SLEEPQ_SX | ((opts & SX_INTERRUPTIBLE) ?
                                    SLEEPQ_INTERRUPTIBLE : 0), SQ_EXCLUSIVE_QUEUE);
        if (!(opts & SX_INTERRUPTIBLE))
            sleepq_wait(&sx->lock_object, 0);
        else
            error = sleepq_wait_sig(&sx->lock_object, 0);
        if (error) {
            break;
        }
    }

    GIANT_RESTORE();
    if (!error)
        LOCKSTAT_PROFILE_OBTAIN_LOCK_SUCCESS(LS_SX_XLOCK_ACQUIRE, sx,
                                             contested, waittime, file, line);
    return (error);
}

/*
 * This function represents the so-called 'hard case' for sx_xunlock
 * operation.  All 'easy case' failures are redirected to this.  Note
 * that ideally this would be a static function, but it needs to be
 * accessible from at least sx.h.
 */
void
_sx_xunlock_hard(struct sx *sx, uintptr_t tid, const char *file, int line)
{
    uintptr_t x;
    int queue, wakeup_swapper;

    if (SCHEDULER_STOPPED())
        return;

    MPASS(!(sx->sx_lock & SX_LOCK_SHARED));

    /* If the lock is recursed, then unrecurse one level. */
    if (sx_xlocked(sx) && sx_recursed(sx)) {
        if ((--sx->sx_recurse) == 0)
            atomic_clear_ptr(&sx->sx_lock, SX_LOCK_RECURSED);
        return;
    }
    MPASS(sx->sx_lock & (SX_LOCK_SHARED_WAITERS |
                         SX_LOCK_EXCLUSIVE_WAITERS));

    sleepq_lock(&sx->lock_object);
    x = SX_LOCK_UNLOCKED;

    /*
     * The wake up algorithm here is quite simple and probably not
     * ideal.  It gives precedence to shared waiters if they are
     * present.  For this condition, we have to preserve the
     * state of the exclusive waiters flag.
     * If interruptible sleeps left the shared queue empty avoid a
     * starvation for the threads sleeping on the exclusive queue by giving
     * them precedence and cleaning up the shared waiters bit anyway.
     */
    if ((sx->sx_lock & SX_LOCK_SHARED_WAITERS) != 0 &&
        sleepq_sleepcnt(&sx->lock_object, SQ_SHARED_QUEUE) != 0) {
        queue = SQ_SHARED_QUEUE;
        x |= (sx->sx_lock & SX_LOCK_EXCLUSIVE_WAITERS);
    } else
        queue = SQ_EXCLUSIVE_QUEUE;

    /* Wake up all the waiters for the specific queue. */
    atomic_store_rel_ptr(&sx->sx_lock, x);
    wakeup_swapper = sleepq_broadcast(&sx->lock_object, SLEEPQ_SX, 0,
                                      queue);
    sleepq_release(&sx->lock_object);
    //if (wakeup_swapper)
    //    kick_proc0();
}

/*
 * This function represents the so-called 'hard case' for sx_slock
 * operation.  All 'easy case' failures are redirected to this.  Note
 * that ideally this would be a static function, but it needs to be
 * accessible from at least sx.h.
 */
int
_sx_slock_hard(struct sx *sx, int opts, const char *file, int line)
{
    GIANT_DECLARE;
#ifdef ADAPTIVE_SX
    volatile struct thread *owner;
#endif
    uintptr_t x;
    int error = 0;

    if (SCHEDULER_STOPPED())
        return (0);

    /*
     * As with rwlocks, we don't make any attempt to try to block
     * shared locks once there is an exclusive waiter.
     */
    for (;;) {
        x = sx->sx_lock;

        /*
         * If no other thread has an exclusive lock then try to bump up
         * the count of sharers.  Since we have to preserve the state
         * of SX_LOCK_EXCLUSIVE_WAITERS, if we fail to acquire the
         * shared lock loop back and retry.
         */
        if (x & SX_LOCK_SHARED) {
            MPASS(!(x & SX_LOCK_SHARED_WAITERS));
            if (atomic_cmpset_acq_ptr(&sx->sx_lock, x,
                                      x + SX_ONE_SHARER)) {
                break;
            }
            continue;
        }
        lock_profile_obtain_lock_failed(&sx->lock_object, &contested,
                                        &waittime);

#ifdef ADAPTIVE_SX
        /*
         * If the owner is running on another CPU, spin until
         * the owner stops running or the state of the lock
         * changes.
         */
        if ((sx->lock_object.lo_flags & SX_NOADAPTIVE) == 0) {
            x = SX_OWNER(x);
            owner = (struct thread *)x;
            if (TD_IS_RUNNING(owner)) {
                GIANT_SAVE();
                while (SX_OWNER(sx->sx_lock) == x &&
                       TD_IS_RUNNING(owner)) {
                    cpu_spinwait();
                }
                continue;
            }
        }
#endif

        /*
         * Some other thread already has an exclusive lock, so
         * start the process of blocking.
         */
        sleepq_lock(&sx->lock_object);
        x = sx->sx_lock;

        /*
         * The lock could have been released while we spun.
         * In this case loop back and retry.
         */
        if (x & SX_LOCK_SHARED) {
            sleepq_release(&sx->lock_object);
            continue;
        }

#ifdef ADAPTIVE_SX
        /*
         * If the owner is running on another CPU, spin until
         * the owner stops running or the state of the lock
         * changes.
         */
        if (!(x & SX_LOCK_SHARED) &&
            (sx->lock_object.lo_flags & SX_NOADAPTIVE) == 0) {
            owner = (struct thread *)SX_OWNER(x);
            if (TD_IS_RUNNING(owner)) {
                sleepq_release(&sx->lock_object);
                continue;
            }
        }
#endif

        /*
         * Try to set the SX_LOCK_SHARED_WAITERS flag.  If we
         * fail to set it drop the sleep queue lock and loop
         * back.
         */
        if (!(x & SX_LOCK_SHARED_WAITERS)) {
            if (!atomic_cmpset_ptr(&sx->sx_lock, x,
                                   x | SX_LOCK_SHARED_WAITERS)) {
                sleepq_release(&sx->lock_object);
                continue;
            }
        }

        /*
         * Since we have been unable to acquire the shared lock,
         * we have to sleep.
         */
        GIANT_SAVE();
        sleepq_add(&sx->lock_object, NULL, sx->lock_object.lo_name,
                   SLEEPQ_SX | ((opts & SX_INTERRUPTIBLE) ?
                                    SLEEPQ_INTERRUPTIBLE : 0), SQ_SHARED_QUEUE);
        if (!(opts & SX_INTERRUPTIBLE))
            sleepq_wait(&sx->lock_object, 0);
        else
            error = sleepq_wait_sig(&sx->lock_object, 0);
        if (error) {
            break;
        }
    }
    if (error == 0)
        LOCKSTAT_PROFILE_OBTAIN_LOCK_SUCCESS(LS_SX_SLOCK_ACQUIRE, sx,
                                             contested, waittime, file, line);
    GIANT_RESTORE();
    return (error);
}

/*
 * This function represents the so-called 'hard case' for sx_sunlock
 * operation.  All 'easy case' failures are redirected to this.  Note
 * that ideally this would be a static function, but it needs to be
 * accessible from at least sx.h.
 */
void
_sx_sunlock_hard(struct sx *sx, const char *file, int line)
{
    uintptr_t x;
    int wakeup_swapper;

    if (SCHEDULER_STOPPED())
        return;

    for (;;) {
        x = sx->sx_lock;

        /*
         * We should never have sharers while at least one thread
         * holds a shared lock.
         */
        KASSERT(!(x & SX_LOCK_SHARED_WAITERS),
                ("%s: waiting sharers", __func__));

        /*
         * See if there is more than one shared lock held.  If
         * so, just drop one and return.
         */
        if (SX_SHARERS(x) > 1) {
            if (atomic_cmpset_rel_ptr(&sx->sx_lock, x,
                                      x - SX_ONE_SHARER)) {
                break;
            }
            continue;
        }

        /*
         * If there aren't any waiters for an exclusive lock,
         * then try to drop it quickly.
         */
        if (!(x & SX_LOCK_EXCLUSIVE_WAITERS)) {
            MPASS(x == SX_SHARERS_LOCK(1));
            if (atomic_cmpset_rel_ptr(&sx->sx_lock,
                                      SX_SHARERS_LOCK(1), SX_LOCK_UNLOCKED)) {
                break;
            }
            continue;
        }

        /*
         * At this point, there should just be one sharer with
         * exclusive waiters.
         */
        MPASS(x == (SX_SHARERS_LOCK(1) | SX_LOCK_EXCLUSIVE_WAITERS));

        sleepq_lock(&sx->lock_object);

        /*
         * Wake up semantic here is quite simple:
         * Just wake up all the exclusive waiters.
         * Note that the state of the lock could have changed,
         * so if it fails loop back and retry.
         */
        if (!atomic_cmpset_rel_ptr(&sx->sx_lock,
                                   SX_SHARERS_LOCK(1) | SX_LOCK_EXCLUSIVE_WAITERS,
                                   SX_LOCK_UNLOCKED)) {
            sleepq_release(&sx->lock_object);
            continue;
        }
        wakeup_swapper = sleepq_broadcast(&sx->lock_object, SLEEPQ_SX,
                                          0, SQ_EXCLUSIVE_QUEUE);
        sleepq_release(&sx->lock_object);
        //if (wakeup_swapper)
        //    kick_proc0();
        break;
    }
}
