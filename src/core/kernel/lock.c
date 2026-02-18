/*-
 * Copyright (c) 2008 Attilio Rao <attilio@FreeBSD.org>
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

#include "amd64/param.h"
#include "atomic.h"
#include "mutex.h"
#include "proc.h"
#include "errno.h"
#include "sleepq.h"
#include "systm.h"
#include "lock.h"

#define	SQ_EXCLUSIVE_QUEUE	0
#define	SQ_SHARED_QUEUE		1

#ifdef ADAPTIVE_LOCKMGRS
#define	ALK_RETRIES		10
#define	ALK_LOOPS		10000
#endif

#ifndef INVARIANTS
#define	_lockmgr_assert(lk, what, file, line)
#define	TD_LOCKS_INC(td)
#define	TD_LOCKS_DEC(td)
#else
#define	TD_LOCKS_INC(td)	((td)->td_locks++)
#define	TD_LOCKS_DEC(td)	((td)->td_locks--)
#endif
#define	TD_SLOCKS_INC(td)	((td)->td_lk_slocks++)
#define	TD_SLOCKS_DEC(td)	((td)->td_lk_slocks--)

#ifndef DEBUG_LOCKS
#define	STACK_PRINT(lk)
#define	STACK_SAVE(lk)
#define	STACK_ZERO(lk)
#else
#define	STACK_PRINT(lk)	stack_print_ddb(&(lk)->lk_stack)
#define	STACK_SAVE(lk)	stack_save(&(lk)->lk_stack)
#define	STACK_ZERO(lk)	stack_zero(&(lk)->lk_stack)
#endif

#define	LOCK_LOG2(lk, string, arg1, arg2)				\
if (LOCK_LOG_TEST(&(lk)->lock_object, 0))			\
        CTR2(KTR_LOCK, (string), (arg1), (arg2))
#define	LOCK_LOG3(lk, string, arg1, arg2, arg3)				\
        if (LOCK_LOG_TEST(&(lk)->lock_object, 0))			\
        CTR3(KTR_LOCK, (string), (arg1), (arg2), (arg3))

#define	GIANT_DECLARE							\
        int _i = 0;							\
    WITNESS_SAVE_DECL(Giant)
#define	GIANT_RESTORE() do {						\
    if (_i > 0) {							\
        while (_i--)						\
        mtx_lock(&Giant);				\
        WITNESS_RESTORE(&Giant.lock_object, Giant);		\
}								\
} while (0)
#define	GIANT_SAVE() do {						\
    if (mtx_owned(&Giant)) {					\
            WITNESS_SAVE(&Giant.lock_object, Giant);		\
            while (mtx_owned(&Giant)) {				\
                _i++;						\
                mtx_unlock(&Giant);				\
        }							\
    }								\
} while (0)

#define	LK_CAN_SHARE(x)							\
    (((x) & LK_SHARE) && (((x) & LK_EXCLUSIVE_WAITERS) == 0 ||	\
                                                               ((x) & LK_EXCLUSIVE_SPINNERS) == 0 ||				\
                                                                                                      curthread->td_lk_slocks || (curthread->td_pflags & TDP_DEADLKTREAT)))
#define	LK_TRYOP(x)							\
        ((x) & LK_NOWAIT)

#define	LK_CAN_WITNESS(x)						\
        (((x) & LK_NOWITNESS) == 0 && !LK_TRYOP(x))
#define	LK_TRYWIT(x)							\
        (LK_TRYOP(x) ? LOP_TRYLOCK : 0)

#define	LK_CAN_ADAPT(lk, f)						\
        (((lk)->lock_object.lo_flags & LK_ADAPTIVE) != 0 &&		\
                                                                 ((f) & LK_SLEEPFAIL) == 0)

#define	lockmgr_disowned(lk)						\
        (((lk)->lk_lock & ~(LK_FLAGMASK & ~LK_SHARE)) == LK_KERNPROC)

#define	lockmgr_xlocked(lk)						\
        (((lk)->lk_lock & ~(LK_FLAGMASK & ~LK_SHARE)) == (uintptr_t)curthread)

        static void	 assert_lockmgr(struct lock_object *lock, int how);
static void	 lock_lockmgr(struct lock_object *lock, int how);
static int	 unlock_lockmgr(struct lock_object *lock);

struct lock_class lock_class_lockmgr = {
    .lc_name = "lockmgr",
    .lc_flags = LC_RECURSABLE | LC_SLEEPABLE | LC_SLEEPLOCK | LC_UPGRADABLE,
    .lc_assert = assert_lockmgr,
    .lc_lock = lock_lockmgr,
    .lc_unlock = unlock_lockmgr,
};

static __inline struct thread *
lockmgr_xholder(struct lock *lk)
{
    uintptr_t x;

    x = lk->lk_lock;
    return ((x & LK_SHARE) ? NULL : (struct thread *)LK_HOLDER(x));
}

/*
 * It assumes sleepq_lock held and returns with this one unheld.
 * It also assumes the generic interlock is sane and previously checked.
 * If LK_INTERLOCK is specified the interlock is not reacquired after the
 * sleep.
 */
static __inline int
sleeplk(struct lock *lk, u_int flags, struct lock_object *ilk,
        const char *wmesg, int pri, int timo, int queue)
{
    GIANT_DECLARE;
    struct lock_class *class;
    int catch, error;

    class = (flags & LK_INTERLOCK) ? LOCK_CLASS(ilk) : NULL;
    catch = pri & PCATCH;
    pri &= PRIMASK;
    error = 0;

    if (flags & LK_INTERLOCK)
        class->lc_unlock(ilk);
    if (queue == SQ_EXCLUSIVE_QUEUE && (flags & LK_SLEEPFAIL) != 0)
        lk->lk_exslpfail++;
    GIANT_SAVE();
    sleepq_add(&lk->lock_object, NULL, wmesg, SLEEPQ_LK | (catch ?
                                                               SLEEPQ_INTERRUPTIBLE : 0), queue);
    if ((flags & LK_TIMELOCK) && timo)
        sleepq_set_timeout(&lk->lock_object, timo);

    /*
     * Decisional switch for real sleeping.
     */
    if ((flags & LK_TIMELOCK) && timo && catch)
        error = sleepq_timedwait_sig(&lk->lock_object, pri);
    else if ((flags & LK_TIMELOCK) && timo)
        error = sleepq_timedwait(&lk->lock_object, pri);
    else if (catch)
        error = sleepq_wait_sig(&lk->lock_object, pri);
    else
        sleepq_wait(&lk->lock_object, pri);
    GIANT_RESTORE();
    if ((flags & LK_SLEEPFAIL) && error == 0)
        error = ENOLCK;

    return (error);
}

static __inline int
wakeupshlk(struct lock *lk, const char *file, int line)
{
    uintptr_t v, x;
    u_int realexslp;
    int queue, wakeup_swapper;

    TD_LOCKS_DEC(curthread);
    TD_SLOCKS_DEC(curthread);
    WITNESS_UNLOCK(&lk->lock_object, 0, file, line);
    LOCK_LOG_LOCK("SUNLOCK", &lk->lock_object, 0, 0, file, line);

    wakeup_swapper = 0;
    for (;;) {
        x = lk->lk_lock;

        /*
         * If there is more than one shared lock held, just drop one
         * and return.
         */
        if (LK_SHARERS(x) > 1) {
            if (atomic_cmpset_rel_ptr(&lk->lk_lock, x,
                                      x - LK_ONE_SHARER))
                break;
            continue;
        }

        /*
         * If there are not waiters on the exclusive queue, drop the
         * lock quickly.
         */
        if ((x & LK_ALL_WAITERS) == 0) {
            MPASS((x & ~LK_EXCLUSIVE_SPINNERS) ==
                  LK_SHARERS_LOCK(1));
            if (atomic_cmpset_rel_ptr(&lk->lk_lock, x, LK_UNLOCKED))
                break;
            continue;
        }

        /*
         * We should have a sharer with waiters, so enter the hard
         * path in order to handle wakeups correctly.
         */
        sleepq_lock(&lk->lock_object);
        x = lk->lk_lock & (LK_ALL_WAITERS | LK_EXCLUSIVE_SPINNERS);
        v = LK_UNLOCKED;

        /*
         * If the lock has exclusive waiters, give them preference in
         * order to avoid deadlock with shared runners up.
         * If interruptible sleeps left the exclusive queue empty
         * avoid a starvation for the threads sleeping on the shared
         * queue by giving them precedence and cleaning up the
         * exclusive waiters bit anyway.
         * Please note that lk_exslpfail count may be lying about
         * the real number of waiters with the LK_SLEEPFAIL flag on
         * because they may be used in conjuction with interruptible
         * sleeps so lk_exslpfail might be considered an 'upper limit'
         * bound, including the edge cases.
         */
        realexslp = sleepq_sleepcnt(&lk->lock_object,
                                    SQ_EXCLUSIVE_QUEUE);
        if ((x & LK_EXCLUSIVE_WAITERS) != 0 && realexslp != 0) {
            if (lk->lk_exslpfail < realexslp) {
                lk->lk_exslpfail = 0;
                queue = SQ_EXCLUSIVE_QUEUE;
                v |= (x & LK_SHARED_WAITERS);
            } else {
                lk->lk_exslpfail = 0;
                wakeup_swapper =
                    sleepq_broadcast(&lk->lock_object,
                                     SLEEPQ_LK, 0, SQ_EXCLUSIVE_QUEUE);
                queue = SQ_SHARED_QUEUE;
            }

        } else {

            /*
             * Exclusive waiters sleeping with LK_SLEEPFAIL on
             * and using interruptible sleeps/timeout may have
             * left spourious lk_exslpfail counts on, so clean
             * it up anyway.
             */
            lk->lk_exslpfail = 0;
            queue = SQ_SHARED_QUEUE;
        }

        if (!atomic_cmpset_rel_ptr(&lk->lk_lock, LK_SHARERS_LOCK(1) | x,
                                   v)) {
            sleepq_release(&lk->lock_object);
            continue;
        }
        wakeup_swapper |= sleepq_broadcast(&lk->lock_object, SLEEPQ_LK,
                                           0, queue);
        sleepq_release(&lk->lock_object);
        break;
    }

    lock_profile_release_lock(&lk->lock_object);
    return (wakeup_swapper);
}

static void
assert_lockmgr(struct lock_object *lock, int what)
{

    panic("lockmgr locks do not support assertions");
}

static void
lock_lockmgr(struct lock_object *lock, int how)
{

    panic("lockmgr locks do not support sleep interlocking");
}

static int
unlock_lockmgr(struct lock_object *lock)
{

    panic("lockmgr locks do not support sleep interlocking");
}

#ifdef KDTRACE_HOOKS
static int
owner_lockmgr(struct lock_object *lock, struct thread **owner)
{

    panic("lockmgr locks do not support owner inquiring");
}
#endif

void
lockinit(struct lock *lk, int pri, const char *wmesg, int timo, int flags)
{
    int iflags;

    MPASS((flags & ~LK_INIT_MASK) == 0);
    ASSERT_ATOMIC_LOAD_PTR(lk->lk_lock,
                           ("%s: lockmgr not aligned for %s: %p", __func__, wmesg,
                            &lk->lk_lock));

    iflags = LO_SLEEPABLE | LO_UPGRADABLE;
    if (flags & LK_CANRECURSE)
        iflags |= LO_RECURSABLE;
    if ((flags & LK_NODUP) == 0)
        iflags |= LO_DUPOK;
    if (flags & LK_NOPROFILE)
        iflags |= LO_NOPROFILE;
    if ((flags & LK_NOWITNESS) == 0)
        iflags |= LO_WITNESS;
    if (flags & LK_QUIET)
        iflags |= LO_QUIET;
    iflags |= flags & (LK_ADAPTIVE | LK_NOSHARE);

    lk->lk_lock = LK_UNLOCKED;
    lk->lk_recurse = 0;
    lk->lk_exslpfail = 0;
    lk->lk_timo = timo;
    lk->lk_pri = pri;
    lock_init(&lk->lock_object, &lock_class_lockmgr, wmesg, NULL, iflags);
    STACK_ZERO(lk);
}

/*
 * XXX: Gross hacks to manipulate external lock flags after
 * initialization.  Used for certain vnode and buf locks.
 */
void
lockallowshare(struct lock *lk)
{

    lockmgr_assert(lk, KA_XLOCKED);
    lk->lock_object.lo_flags &= ~LK_NOSHARE;
}

void
lockallowrecurse(struct lock *lk)
{

    lockmgr_assert(lk, KA_XLOCKED);
    lk->lock_object.lo_flags |= LO_RECURSABLE;
}

void
lockdisablerecurse(struct lock *lk)
{

    lockmgr_assert(lk, KA_XLOCKED);
    lk->lock_object.lo_flags &= ~LO_RECURSABLE;
}

void
lockdestroy(struct lock *lk)
{

    KASSERT(lk->lk_lock == LK_UNLOCKED, ("lockmgr still held"));
    KASSERT(lk->lk_recurse == 0, ("lockmgr still recursed"));
    KASSERT(lk->lk_exslpfail == 0, ("lockmgr still exclusive waiters"));
    lock_destroy(&lk->lock_object);
}

int
__lockmgr_args(struct lock *lk, u_int flags, struct lock_object *ilk,
               const char *wmesg, int pri, int timo, const char *file, int line)
{
    GIANT_DECLARE;
    struct lock_class *class;
    const char *iwmesg;
    uintptr_t tid, v, x;
    u_int op, realexslp;
    int error, ipri, itimo, queue, wakeup_swapper;
#ifdef ADAPTIVE_LOCKMGRS
    volatile struct thread *owner;
    u_int i, spintries = 0;
#endif

    error = 0;
    tid = (uintptr_t)curthread;
    op = (flags & LK_TYPE_MASK);
    iwmesg = (wmesg == LK_WMESG_DEFAULT) ? lk->lock_object.lo_name : wmesg;
    ipri = (pri == LK_PRIO_DEFAULT) ? lk->lk_pri : pri;
    itimo = (timo == LK_TIMO_DEFAULT) ? lk->lk_timo : timo;

    MPASS((flags & ~LK_TOTAL_MASK) == 0);
    KASSERT((op & (op - 1)) == 0,
            ("%s: Invalid requested operation @ %s:%d", __func__, file, line));
    KASSERT((flags & (LK_NOWAIT | LK_SLEEPFAIL)) == 0 ||
                (op != LK_DOWNGRADE && op != LK_RELEASE),
            ("%s: Invalid flags in regard of the operation desired @ %s:%d",
             __func__, file, line));
    KASSERT((flags & LK_INTERLOCK) == 0 || ilk != NULL,
            ("%s: LK_INTERLOCK passed without valid interlock @ %s:%d",
             __func__, file, line));

    class = (flags & LK_INTERLOCK) ? LOCK_CLASS(ilk) : NULL;
    if (panicstr != NULL) {
        if (flags & LK_INTERLOCK)
            class->lc_unlock(ilk);
        return (0);
    }

    if (lk->lock_object.lo_flags & LK_NOSHARE) {
        switch (op) {
        case LK_SHARED:
            op = LK_EXCLUSIVE;
            break;
        case LK_UPGRADE:
        case LK_DOWNGRADE:
            _lockmgr_assert(lk, KA_XLOCKED | KA_NOTRECURSED,
                            file, line);
            return (0);
        }
    }

    wakeup_swapper = 0;
    switch (op) {
    case LK_SHARED:
        if (LK_CAN_WITNESS(flags))
            WITNESS_CHECKORDER(&lk->lock_object, LOP_NEWORDER,
                               file, line, ilk);
        for (;;) {
            x = lk->lk_lock;

            /*
             * If no other thread has an exclusive lock, or
             * no exclusive waiter is present, bump the count of
             * sharers.  Since we have to preserve the state of
             * waiters, if we fail to acquire the shared lock
             * loop back and retry.
             */
            if (LK_CAN_SHARE(x)) {
                if (atomic_cmpset_acq_ptr(&lk->lk_lock, x,
                                          x + LK_ONE_SHARER))
                    break;
                continue;
            }
            lock_profile_obtain_lock_failed(&lk->lock_object,
                                            &contested, &waittime);

            /*
             * If the lock is already held by curthread in
             * exclusive way avoid a deadlock.
             */
            if (LK_HOLDER(x) == tid) {
                error = EDEADLK;
                break;
            }

            /*
             * If the lock is expected to not sleep just give up
             * and return.
             */
            if (LK_TRYOP(flags)) {
                error = EBUSY;
                break;
            }

#ifdef ADAPTIVE_LOCKMGRS
            /*
             * If the owner is running on another CPU, spin until
             * the owner stops running or the state of the lock
             * changes.  We need a double-state handle here
             * because for a failed acquisition the lock can be
             * either held in exclusive mode or shared mode
             * (for the writer starvation avoidance technique).
             */
            if (LK_CAN_ADAPT(lk, flags) && (x & LK_SHARE) == 0 &&
                LK_HOLDER(x) != LK_KERNPROC) {
                owner = (struct thread *)LK_HOLDER(x);
                if (LOCK_LOG_TEST(&lk->lock_object, 0))
                    CTR3(KTR_LOCK,
                         "%s: spinning on %p held by %p",
                         __func__, lk, owner);

                /*
                 * If we are holding also an interlock drop it
                 * in order to avoid a deadlock if the lockmgr
                 * owner is adaptively spinning on the
                 * interlock itself.
                 */
                if (flags & LK_INTERLOCK) {
                    class->lc_unlock(ilk);
                    flags &= ~LK_INTERLOCK;
                }
                GIANT_SAVE();
                while (LK_HOLDER(lk->lk_lock) ==
                           (uintptr_t)owner && TD_IS_RUNNING(owner))
                    cpu_spinwait();
                GIANT_RESTORE();
                continue;
            } else if (LK_CAN_ADAPT(lk, flags) &&
                       (x & LK_SHARE) != 0 && LK_SHARERS(x) &&
                       spintries < ALK_RETRIES) {
                if (flags & LK_INTERLOCK) {
                    class->lc_unlock(ilk);
                    flags &= ~LK_INTERLOCK;
                }
                GIANT_SAVE();
                spintries++;
                for (i = 0; i < ALK_LOOPS; i++) {
                    if (LOCK_LOG_TEST(&lk->lock_object, 0))
                        CTR4(KTR_LOCK,
                             "%s: shared spinning on %p with %u and %u",
                             __func__, lk, spintries, i);
                    x = lk->lk_lock;
                    if ((x & LK_SHARE) == 0 ||
                        LK_CAN_SHARE(x) != 0)
                        break;
                    cpu_spinwait();
                }
                GIANT_RESTORE();
                if (i != ALK_LOOPS)
                    continue;
            }
#endif

            /*
             * Acquire the sleepqueue chain lock because we
             * probabilly will need to manipulate waiters flags.
             */
            sleepq_lock(&lk->lock_object);
            x = lk->lk_lock;

            /*
             * if the lock can be acquired in shared mode, try
             * again.
             */
            if (LK_CAN_SHARE(x)) {
                sleepq_release(&lk->lock_object);
                continue;
            }

#ifdef ADAPTIVE_LOCKMGRS
            /*
             * The current lock owner might have started executing
             * on another CPU (or the lock could have changed
             * owner) while we were waiting on the turnstile
             * chain lock.  If so, drop the turnstile lock and try
             * again.
             */
            if (LK_CAN_ADAPT(lk, flags) && (x & LK_SHARE) == 0 &&
                LK_HOLDER(x) != LK_KERNPROC) {
                owner = (struct thread *)LK_HOLDER(x);
                if (TD_IS_RUNNING(owner)) {
                    sleepq_release(&lk->lock_object);
                    continue;
                }
            }
#endif

            /*
             * Try to set the LK_SHARED_WAITERS flag.  If we fail,
             * loop back and retry.
             */
            if ((x & LK_SHARED_WAITERS) == 0) {
                if (!atomic_cmpset_acq_ptr(&lk->lk_lock, x,
                                           x | LK_SHARED_WAITERS)) {
                    sleepq_release(&lk->lock_object);
                    continue;
                }
            }

            /*
             * As far as we have been unable to acquire the
             * shared lock and the shared waiters flag is set,
             * we will sleep.
             */
            error = sleeplk(lk, flags, ilk, iwmesg, ipri, itimo,
                            SQ_SHARED_QUEUE);
            flags &= ~LK_INTERLOCK;
            if (error) {
                break;
            }
        }
        if (error == 0) {
            lock_profile_obtain_lock_success(&lk->lock_object,
                                             contested, waittime, file, line);
            LOCK_LOG_LOCK("SLOCK", &lk->lock_object, 0, 0, file,
                          line);
            WITNESS_LOCK(&lk->lock_object, LK_TRYWIT(flags), file,
                         line);
            TD_LOCKS_INC(curthread);
            TD_SLOCKS_INC(curthread);
            STACK_SAVE(lk);
        }
        break;
    case LK_UPGRADE:
        _lockmgr_assert(lk, KA_SLOCKED, file, line);
        v = lk->lk_lock;
        x = v & LK_ALL_WAITERS;
        v &= LK_EXCLUSIVE_SPINNERS;

        /*
         * Try to switch from one shared lock to an exclusive one.
         * We need to preserve waiters flags during the operation.
         */
        if (atomic_cmpset_ptr(&lk->lk_lock, LK_SHARERS_LOCK(1) | x | v,
                              tid | x)) {
            LOCK_LOG_LOCK("XUPGRADE", &lk->lock_object, 0, 0, file,
                          line);
            WITNESS_UPGRADE(&lk->lock_object, LOP_EXCLUSIVE |
                                                  LK_TRYWIT(flags), file, line);
            TD_SLOCKS_DEC(curthread);
            break;
        }

        /*
         * We have been unable to succeed in upgrading, so just
         * give up the shared lock.
         */
        wakeup_swapper |= wakeupshlk(lk, file, line);

        /* FALLTHROUGH */
    case LK_EXCLUSIVE:
        if (LK_CAN_WITNESS(flags))
            WITNESS_CHECKORDER(&lk->lock_object, LOP_NEWORDER |
                                                     LOP_EXCLUSIVE, file, line, ilk);

        /*
         * If curthread already holds the lock and this one is
         * allowed to recurse, simply recurse on it.
         */
        if (lockmgr_xlocked(lk)) {
            if ((flags & LK_CANRECURSE) == 0 &&
                (lk->lock_object.lo_flags & LO_RECURSABLE) == 0) {

                /*
                 * If the lock is expected to not panic just
                 * give up and return.
                 */
                if (LK_TRYOP(flags)) {
                    error = EBUSY;
                    break;
                }
                if (flags & LK_INTERLOCK)
                    class->lc_unlock(ilk);
                panic("%s: recursing on non recursive lockmgr %s @ %s:%d\n",
                      __func__, iwmesg, file, line);
            }
            lk->lk_recurse++;
            LOCK_LOG_LOCK("XLOCK", &lk->lock_object, 0,
                          lk->lk_recurse, file, line);
            WITNESS_LOCK(&lk->lock_object, LOP_EXCLUSIVE |
                                               LK_TRYWIT(flags), file, line);
            TD_LOCKS_INC(curthread);
            break;
        }

        while (!atomic_cmpset_acq_ptr(&lk->lk_lock, LK_UNLOCKED,
                                      tid)) {
            lock_profile_obtain_lock_failed(&lk->lock_object,
                                            &contested, &waittime);

            /*
             * If the lock is expected to not sleep just give up
             * and return.
             */
            if (LK_TRYOP(flags)) {
                error = EBUSY;
                break;
            }

#ifdef ADAPTIVE_LOCKMGRS
            /*
             * If the owner is running on another CPU, spin until
             * the owner stops running or the state of the lock
             * changes.
             */
            x = lk->lk_lock;
            if (LK_CAN_ADAPT(lk, flags) && (x & LK_SHARE) == 0 &&
                LK_HOLDER(x) != LK_KERNPROC) {
                owner = (struct thread *)LK_HOLDER(x);
                if (LOCK_LOG_TEST(&lk->lock_object, 0))
                    CTR3(KTR_LOCK,
                         "%s: spinning on %p held by %p",
                         __func__, lk, owner);

                /*
                 * If we are holding also an interlock drop it
                 * in order to avoid a deadlock if the lockmgr
                 * owner is adaptively spinning on the
                 * interlock itself.
                 */
                if (flags & LK_INTERLOCK) {
                    class->lc_unlock(ilk);
                    flags &= ~LK_INTERLOCK;
                }
                GIANT_SAVE();
                while (LK_HOLDER(lk->lk_lock) ==
                           (uintptr_t)owner && TD_IS_RUNNING(owner))
                    cpu_spinwait();
                GIANT_RESTORE();
                continue;
            } else if (LK_CAN_ADAPT(lk, flags) &&
                       (x & LK_SHARE) != 0 && LK_SHARERS(x) &&
                       spintries < ALK_RETRIES) {
                if ((x & LK_EXCLUSIVE_SPINNERS) == 0 &&
                    !atomic_cmpset_ptr(&lk->lk_lock, x,
                                       x | LK_EXCLUSIVE_SPINNERS))
                    continue;
                if (flags & LK_INTERLOCK) {
                    class->lc_unlock(ilk);
                    flags &= ~LK_INTERLOCK;
                }
                GIANT_SAVE();
                spintries++;
                for (i = 0; i < ALK_LOOPS; i++) {
                    if (LOCK_LOG_TEST(&lk->lock_object, 0))
                        CTR4(KTR_LOCK,
                             "%s: shared spinning on %p with %u and %u",
                             __func__, lk, spintries, i);
                    if ((lk->lk_lock &
                         LK_EXCLUSIVE_SPINNERS) == 0)
                        break;
                    cpu_spinwait();
                }
                GIANT_RESTORE();
                if (i != ALK_LOOPS)
                    continue;
            }
#endif

            /*
             * Acquire the sleepqueue chain lock because we
             * probabilly will need to manipulate waiters flags.
             */
            sleepq_lock(&lk->lock_object);
            x = lk->lk_lock;

            /*
             * if the lock has been released while we spun on
             * the sleepqueue chain lock just try again.
             */
            if (x == LK_UNLOCKED) {
                sleepq_release(&lk->lock_object);
                continue;
            }

#ifdef ADAPTIVE_LOCKMGRS
            /*
             * The current lock owner might have started executing
             * on another CPU (or the lock could have changed
             * owner) while we were waiting on the turnstile
             * chain lock.  If so, drop the turnstile lock and try
             * again.
             */
            if (LK_CAN_ADAPT(lk, flags) && (x & LK_SHARE) == 0 &&
                LK_HOLDER(x) != LK_KERNPROC) {
                owner = (struct thread *)LK_HOLDER(x);
                if (TD_IS_RUNNING(owner)) {
                    sleepq_release(&lk->lock_object);
                    continue;
                }
            }
#endif

            /*
             * The lock can be in the state where there is a
             * pending queue of waiters, but still no owner.
             * This happens when the lock is contested and an
             * owner is going to claim the lock.
             * If curthread is the one successfully acquiring it
             * claim lock ownership and return, preserving waiters
             * flags.
             */
            v = x & (LK_ALL_WAITERS | LK_EXCLUSIVE_SPINNERS);
            if ((x & ~v) == LK_UNLOCKED) {
                v &= ~LK_EXCLUSIVE_SPINNERS;
                if (atomic_cmpset_acq_ptr(&lk->lk_lock, x,
                                          tid | v)) {
                    sleepq_release(&lk->lock_object);
                    break;
                }
                sleepq_release(&lk->lock_object);
                continue;
            }

            /*
             * Try to set the LK_EXCLUSIVE_WAITERS flag.  If we
             * fail, loop back and retry.
             */
            if ((x & LK_EXCLUSIVE_WAITERS) == 0) {
                if (!atomic_cmpset_ptr(&lk->lk_lock, x,
                                       x | LK_EXCLUSIVE_WAITERS)) {
                    sleepq_release(&lk->lock_object);
                    continue;
                }
            }

            /*
             * As far as we have been unable to acquire the
             * exclusive lock and the exclusive waiters flag
             * is set, we will sleep.
             */
            error = sleeplk(lk, flags, ilk, iwmesg, ipri, itimo,
                            SQ_EXCLUSIVE_QUEUE);
            flags &= ~LK_INTERLOCK;
            if (error) {
                break;
            }
        }
        if (error == 0) {
            lock_profile_obtain_lock_success(&lk->lock_object,
                                             contested, waittime, file, line);
            LOCK_LOG_LOCK("XLOCK", &lk->lock_object, 0,
                          lk->lk_recurse, file, line);
            WITNESS_LOCK(&lk->lock_object, LOP_EXCLUSIVE |
                                               LK_TRYWIT(flags), file, line);
            TD_LOCKS_INC(curthread);
            STACK_SAVE(lk);
        }
        break;
    case LK_DOWNGRADE:
        _lockmgr_assert(lk, KA_XLOCKED | KA_NOTRECURSED, file, line);
        LOCK_LOG_LOCK("XDOWNGRADE", &lk->lock_object, 0, 0, file, line);
        WITNESS_DOWNGRADE(&lk->lock_object, 0, file, line);
        TD_SLOCKS_INC(curthread);

        /*
         * In order to preserve waiters flags, just spin.
         */
        for (;;) {
            x = lk->lk_lock;
            MPASS((x & LK_EXCLUSIVE_SPINNERS) == 0);
            x &= LK_ALL_WAITERS;
            if (atomic_cmpset_rel_ptr(&lk->lk_lock, tid | x,
                                      LK_SHARERS_LOCK(1) | x))
                break;
            cpu_spinwait();
        }
        break;
    case LK_RELEASE:
        _lockmgr_assert(lk, KA_LOCKED, file, line);
        x = lk->lk_lock;

        if ((x & LK_SHARE) == 0) {

            /*
             * As first option, treact the lock as if it has not
             * any waiter.
             * Fix-up the tid var if the lock has been disowned.
             */
            if (LK_HOLDER(x) == LK_KERNPROC)
                tid = LK_KERNPROC;
            else {
                WITNESS_UNLOCK(&lk->lock_object, LOP_EXCLUSIVE,
                               file, line);
                TD_LOCKS_DEC(curthread);
            }
            LOCK_LOG_LOCK("XUNLOCK", &lk->lock_object, 0,
                          lk->lk_recurse, file, line);

            /*
             * The lock is held in exclusive mode.
             * If the lock is recursed also, then unrecurse it.
             */
            if (lockmgr_xlocked(lk) && lockmgr_recursed(lk)) {
                lk->lk_recurse--;
                break;
            }
            if (tid != LK_KERNPROC)
                lock_profile_release_lock(&lk->lock_object);

            if (atomic_cmpset_rel_ptr(&lk->lk_lock, tid,
                                      LK_UNLOCKED))
                break;

            sleepq_lock(&lk->lock_object);
            x = lk->lk_lock;
            v = LK_UNLOCKED;

            /*
             * If the lock has exclusive waiters, give them
             * preference in order to avoid deadlock with
             * shared runners up.
             * If interruptible sleeps left the exclusive queue
             * empty avoid a starvation for the threads sleeping
             * on the shared queue by giving them precedence
             * and cleaning up the exclusive waiters bit anyway.
             * Please note that lk_exslpfail count may be lying
             * about the real number of waiters with the
             * LK_SLEEPFAIL flag on because they may be used in
             * conjuction with interruptible sleeps so
             * lk_exslpfail might be considered an 'upper limit'
             * bound, including the edge cases.
             */
            MPASS((x & LK_EXCLUSIVE_SPINNERS) == 0);
            realexslp = sleepq_sleepcnt(&lk->lock_object,
                                        SQ_EXCLUSIVE_QUEUE);
            if ((x & LK_EXCLUSIVE_WAITERS) != 0 && realexslp != 0) {
                if (lk->lk_exslpfail < realexslp) {
                    lk->lk_exslpfail = 0;
                    queue = SQ_EXCLUSIVE_QUEUE;
                    v |= (x & LK_SHARED_WAITERS);
                } else {
                    lk->lk_exslpfail = 0;
                    wakeup_swapper =
                        sleepq_broadcast(&lk->lock_object,
                                         SLEEPQ_LK, 0, SQ_EXCLUSIVE_QUEUE);
                    queue = SQ_SHARED_QUEUE;
                }
            } else {

                /*
                 * Exclusive waiters sleeping with LK_SLEEPFAIL
                 * on and using interruptible sleeps/timeout
                 * may have left spourious lk_exslpfail counts
                 * on, so clean it up anyway.
                 */
                lk->lk_exslpfail = 0;
                queue = SQ_SHARED_QUEUE;
            }

            atomic_store_rel_ptr(&lk->lk_lock, v);
            wakeup_swapper |= sleepq_broadcast(&lk->lock_object,
                                               SLEEPQ_LK, 0, queue);
            sleepq_release(&lk->lock_object);
            break;
        } else
            wakeup_swapper = wakeupshlk(lk, file, line);
        break;
    case LK_DRAIN:
        if (LK_CAN_WITNESS(flags))
            WITNESS_CHECKORDER(&lk->lock_object, LOP_NEWORDER |
                                                     LOP_EXCLUSIVE, file, line, ilk);

        /*
         * Trying to drain a lock we already own will result in a
         * deadlock.
         */
        if (lockmgr_xlocked(lk)) {
            if (flags & LK_INTERLOCK)
                class->lc_unlock(ilk);
            panic("%s: draining %s with the lock held @ %s:%d\n",
                  __func__, iwmesg, file, line);
        }

        while (!atomic_cmpset_acq_ptr(&lk->lk_lock, LK_UNLOCKED, tid)) {
            lock_profile_obtain_lock_failed(&lk->lock_object,
                                            &contested, &waittime);

            /*
             * If the lock is expected to not sleep just give up
             * and return.
             */
            if (LK_TRYOP(flags)) {
                error = EBUSY;
                break;
            }

            /*
             * Acquire the sleepqueue chain lock because we
             * probabilly will need to manipulate waiters flags.
             */
            sleepq_lock(&lk->lock_object);
            x = lk->lk_lock;

            /*
             * if the lock has been released while we spun on
             * the sleepqueue chain lock just try again.
             */
            if (x == LK_UNLOCKED) {
                sleepq_release(&lk->lock_object);
                continue;
            }

            v = x & (LK_ALL_WAITERS | LK_EXCLUSIVE_SPINNERS);
            if ((x & ~v) == LK_UNLOCKED) {
                v = (x & ~LK_EXCLUSIVE_SPINNERS);

                /*
                 * If interruptible sleeps left the exclusive
                 * queue empty avoid a starvation for the
                 * threads sleeping on the shared queue by
                 * giving them precedence and cleaning up the
                 * exclusive waiters bit anyway.
                 * Please note that lk_exslpfail count may be
                 * lying about the real number of waiters with
                 * the LK_SLEEPFAIL flag on because they may
                 * be used in conjuction with interruptible
                 * sleeps so lk_exslpfail might be considered
                 * an 'upper limit' bound, including the edge
                 * cases.
                 */
                if (v & LK_EXCLUSIVE_WAITERS) {
                    queue = SQ_EXCLUSIVE_QUEUE;
                    v &= ~LK_EXCLUSIVE_WAITERS;
                } else {

                    /*
                     * Exclusive waiters sleeping with
                     * LK_SLEEPFAIL on and using
                     * interruptible sleeps/timeout may
                     * have left spourious lk_exslpfail
                     * counts on, so clean it up anyway.
                     */
                    MPASS(v & LK_SHARED_WAITERS);
                    lk->lk_exslpfail = 0;
                    queue = SQ_SHARED_QUEUE;
                    v &= ~LK_SHARED_WAITERS;
                }
                if (queue == SQ_EXCLUSIVE_QUEUE) {
                    realexslp =
                        sleepq_sleepcnt(&lk->lock_object,
                                        SQ_EXCLUSIVE_QUEUE);
                    if (lk->lk_exslpfail >= realexslp) {
                        lk->lk_exslpfail = 0;
                        queue = SQ_SHARED_QUEUE;
                        v &= ~LK_SHARED_WAITERS;
                        if (realexslp != 0) {
                            wakeup_swapper =
                                sleepq_broadcast(
                                    &lk->lock_object,
                                    SLEEPQ_LK, 0,
                                    SQ_EXCLUSIVE_QUEUE);
                        }
                    } else
                        lk->lk_exslpfail = 0;
                }
                if (!atomic_cmpset_ptr(&lk->lk_lock, x, v)) {
                    sleepq_release(&lk->lock_object);
                    continue;
                }
                wakeup_swapper |= sleepq_broadcast(
                    &lk->lock_object, SLEEPQ_LK, 0, queue);

                /*
                 * If shared waiters have been woken up we need
                 * to wait for one of them to acquire the lock
                 * before to set the exclusive waiters in
                 * order to avoid a deadlock.
                 */
                if (queue == SQ_SHARED_QUEUE) {
                    for (v = lk->lk_lock;
                         (v & LK_SHARE) && !LK_SHARERS(v);
                         v = lk->lk_lock)
                        cpu_spinwait();
                }
            }

            /*
             * Try to set the LK_EXCLUSIVE_WAITERS flag.  If we
             * fail, loop back and retry.
             */
            if ((x & LK_EXCLUSIVE_WAITERS) == 0) {
                if (!atomic_cmpset_ptr(&lk->lk_lock, x,
                                       x | LK_EXCLUSIVE_WAITERS)) {
                    sleepq_release(&lk->lock_object);
                    continue;
                }
            }

            /*
             * As far as we have been unable to acquire the
             * exclusive lock and the exclusive waiters flag
             * is set, we will sleep.
             */
            if (flags & LK_INTERLOCK) {
                class->lc_unlock(ilk);
                flags &= ~LK_INTERLOCK;
            }
            GIANT_SAVE();
            sleepq_add(&lk->lock_object, NULL, iwmesg, SLEEPQ_LK,
                       SQ_EXCLUSIVE_QUEUE);
            sleepq_wait(&lk->lock_object, ipri & PRIMASK);
            GIANT_RESTORE();
        }

        if (error == 0) {
            lock_profile_obtain_lock_success(&lk->lock_object,
                                             contested, waittime, file, line);
            LOCK_LOG_LOCK("DRAIN", &lk->lock_object, 0,
                          lk->lk_recurse, file, line);
            WITNESS_LOCK(&lk->lock_object, LOP_EXCLUSIVE |
                                               LK_TRYWIT(flags), file, line);
            TD_LOCKS_INC(curthread);
            STACK_SAVE(lk);
        }
        break;
    default:
        if (flags & LK_INTERLOCK)
            class->lc_unlock(ilk);
        panic("%s: unknown lockmgr request 0x%x\n", __func__, op);
    }

    if (flags & LK_INTERLOCK)
        class->lc_unlock(ilk);
    //if (wakeup_swapper)
    //    kick_proc0();

    return (error);
}

void
_lockmgr_disown(struct lock *lk, const char *file, int line)
{
    uintptr_t tid, x;

    if (SCHEDULER_STOPPED())
        return;

    tid = (uintptr_t)curthread;
    _lockmgr_assert(lk, KA_XLOCKED | KA_NOTRECURSED, file, line);

    /*
     * If the owner is already LK_KERNPROC just skip the whole operation.
     */
    if (LK_HOLDER(lk->lk_lock) != tid)
        return;
    lock_profile_release_lock(&lk->lock_object);
    LOCK_LOG_LOCK("XDISOWN", &lk->lock_object, 0, 0, file, line);
    WITNESS_UNLOCK(&lk->lock_object, LOP_EXCLUSIVE, file, line);
    TD_LOCKS_DEC(curthread);
    STACK_SAVE(lk);

    /*
     * In order to preserve waiters flags, just spin.
     */
    for (;;) {
        x = lk->lk_lock;
        MPASS((x & LK_EXCLUSIVE_SPINNERS) == 0);
        x &= LK_ALL_WAITERS;
        if (atomic_cmpset_rel_ptr(&lk->lk_lock, tid | x,
                                  LK_KERNPROC | x))
            return;
        cpu_spinwait();
    }
}

#ifndef _UINTMAX_T_DECLARED
typedef	__uintmax_t		uintmax_t;
#define	_UINTMAX_T_DECLARED
#endif

void
lockmgr_printinfo(struct lock *lk)
{
    /*struct thread *td;
    uintptr_t x;

    if (lk->lk_lock == LK_UNLOCKED)
        printf("lock type %s: UNLOCKED\n", lk->lock_object.lo_name);
    else if (lk->lk_lock & LK_SHARE)
        printf("lock type %s: SHARED (count %ju)\n",
               lk->lock_object.lo_name,
               (uintmax_t)LK_SHARERS(lk->lk_lock));
    else {
        td = lockmgr_xholder(lk);
        printf("lock type %s: EXCL by thread %p (pid %d)\n",
               lk->lock_object.lo_name, td, td->td_proc->p_pid);
    }

    x = lk->lk_lock;
    if (x & LK_EXCLUSIVE_WAITERS)
        printf(" with exclusive waiters pending\n");
    if (x & LK_SHARED_WAITERS)
        printf(" with shared waiters pending\n");
    if (x & LK_EXCLUSIVE_SPINNERS)
        printf(" with exclusive spinners pending\n");

    STACK_PRINT(lk);*/
}

int
lockstatus(struct lock *lk)
{
    uintptr_t v, x;
    int ret;

    ret = LK_SHARED;
    x = lk->lk_lock;
    v = LK_HOLDER(x);

    if ((x & LK_SHARE) == 0) {
        if (v == (uintptr_t)curthread || v == LK_KERNPROC)
            ret = LK_EXCLUSIVE;
        else
            ret = LK_EXCLOTHER;
    } else if (x == LK_UNLOCKED)
        ret = 0;

    return (ret);
}
