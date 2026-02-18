/*-
 * Copyright (c) 1998 Berkeley Software Design, Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Berkeley Software Design Inc's name may not be used to endorse or
 *    promote products derived from this software without specific prior
 *    written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY BERKELEY SOFTWARE DESIGN INC ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL BERKELEY SOFTWARE DESIGN INC BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	from BSDI $Id: mutex_witness.c,v 1.1.2.20 2000/04/27 03:10:27 cp Exp $
 *	and BSDI $Id: synch_machdep.c,v 2.3.2.39 2000/04/27 03:10:25 cp Exp $
 */

/*
 * Machine independent bits of mutex implementation.
 */
#include "lock.h"
#include "mutex.h"
#include "proc.h"
#include "systm.h"

#define	ADAPTIVE_MUTEXES

/*
 * Internal utility macros.
 */
#define mtx_unowned(m)	((m)->mtx_lock == MTX_UNOWNED)

#define	mtx_destroyed(m) ((m)->mtx_lock == MTX_DESTROYED)

#define	mtx_owner(m)	((struct thread *)((m)->mtx_lock & ~MTX_FLAGMASK))

static void	assert_mtx(struct lock_object *lock, int what);
static void	lock_mtx(struct lock_object *lock, int how);
static void	lock_spin(struct lock_object *lock, int how);
static int	unlock_mtx(struct lock_object *lock);
static int	unlock_spin(struct lock_object *lock);

/*
 * Lock classes for sleep and spin mutexes.
 */
struct lock_class lock_class_mtx_sleep = {
    .lc_name = "sleep mutex",
    .lc_flags = LC_SLEEPLOCK | LC_RECURSABLE,
    .lc_assert = assert_mtx,
    .lc_lock = lock_mtx,
    .lc_unlock = unlock_mtx,
};
struct lock_class lock_class_mtx_spin = {
    .lc_name = "spin mutex",
    .lc_flags = LC_SPINLOCK | LC_RECURSABLE,
    .lc_assert = assert_mtx,
    .lc_lock = lock_spin,
    .lc_unlock = unlock_spin,
};

/*
 * System-wide mutexes
 */
struct mtx blocked_lock;
struct mtx Giant;

void
assert_mtx(struct lock_object *lock, int what)
{

    mtx_assert((struct mtx *)lock, what);
}

void
lock_mtx(struct lock_object *lock, int how)
{

    mtx_lock((struct mtx *)lock);
}

void
lock_spin(struct lock_object *lock, int how)
{

    panic("spin locks can only use msleep_spin");
}

int
unlock_mtx(struct lock_object *lock)
{
    struct mtx *m;

    m = (struct mtx *)lock;
    mtx_assert(m, MA_OWNED | MA_NOTRECURSED);
    mtx_unlock(m);
    return (0);
}

int
unlock_spin(struct lock_object *lock)
{

    panic("spin locks can only use msleep_spin");
}

/*
 * Function versions of the inlined __mtx_* macros.  These are used by
 * modules and can also be called from assembly language if needed.
 */
void
_mtx_lock_flags(struct mtx *m, int opts, const char *file, int line)
{

    if (SCHEDULER_STOPPED())
        return;
    MPASS(curthread != NULL);
    KASSERT(m->mtx_lock != MTX_DESTROYED,
            ("mtx_lock() of destroyed mutex @ %s:%d", file, line));
    KASSERT(LOCK_CLASS(&m->lock_object) == &lock_class_mtx_sleep,
            ("mtx_lock() of spin mutex %s @ %s:%d", m->lock_object.lo_name,
             file, line));
    WITNESS_CHECKORDER(&m->lock_object, opts | LOP_NEWORDER | LOP_EXCLUSIVE,
                       file, line, NULL);

    __mtx_lock(m, curthread, opts, file, line);
    LOCK_LOG_LOCK("LOCK", &m->lock_object, opts, m->mtx_recurse, file,
                  line);
    WITNESS_LOCK(&m->lock_object, opts | LOP_EXCLUSIVE, file, line);
    curthread->td_locks++;
}

void
_mtx_unlock_flags(struct mtx *m, int opts, const char *file, int line)
{

    if (SCHEDULER_STOPPED())
        return;
    MPASS(curthread != NULL);
    KASSERT(m->mtx_lock != MTX_DESTROYED,
            ("mtx_unlock() of destroyed mutex @ %s:%d", file, line));
    KASSERT(LOCK_CLASS(&m->lock_object) == &lock_class_mtx_sleep,
            ("mtx_unlock() of spin mutex %s @ %s:%d", m->lock_object.lo_name,
             file, line));
    curthread->td_locks--;
    WITNESS_UNLOCK(&m->lock_object, opts | LOP_EXCLUSIVE, file, line);
    LOCK_LOG_LOCK("UNLOCK", &m->lock_object, opts, m->mtx_recurse, file,
                  line);
    mtx_assert(m, MA_OWNED);

    if (m->mtx_recurse == 0)
        LOCKSTAT_PROFILE_RELEASE_LOCK(LS_MTX_UNLOCK_RELEASE, m);
    __mtx_unlock(m, curthread, opts, file, line);
}

void
_mtx_lock_spin_flags(struct mtx *m, int opts, const char *file, int line)
{

    if (SCHEDULER_STOPPED())
        return;
    MPASS(curthread != NULL);
    KASSERT(m->mtx_lock != MTX_DESTROYED,
            ("mtx_lock_spin() of destroyed mutex @ %s:%d", file, line));
    KASSERT(LOCK_CLASS(&m->lock_object) == &lock_class_mtx_spin,
            ("mtx_lock_spin() of sleep mutex %s @ %s:%d",
             m->lock_object.lo_name, file, line));
    if (mtx_owned(m))
        KASSERT((m->lock_object.lo_flags & LO_RECURSABLE) != 0,
                ("mtx_lock_spin: recursed on non-recursive mutex %s @ %s:%d\n",
                 m->lock_object.lo_name, file, line));
    WITNESS_CHECKORDER(&m->lock_object, opts | LOP_NEWORDER | LOP_EXCLUSIVE,
                       file, line, NULL);
    __mtx_lock_spin(m, curthread, opts, file, line);
    LOCK_LOG_LOCK("LOCK", &m->lock_object, opts, m->mtx_recurse, file,
                  line);
    WITNESS_LOCK(&m->lock_object, opts | LOP_EXCLUSIVE, file, line);
}

void
_mtx_unlock_spin_flags(struct mtx *m, int opts, const char *file, int line)
{

    if (SCHEDULER_STOPPED())
        return;
    MPASS(curthread != NULL);
    KASSERT(m->mtx_lock != MTX_DESTROYED,
            ("mtx_unlock_spin() of destroyed mutex @ %s:%d", file, line));
    KASSERT(LOCK_CLASS(&m->lock_object) == &lock_class_mtx_spin,
            ("mtx_unlock_spin() of sleep mutex %s @ %s:%d",
             m->lock_object.lo_name, file, line));
    WITNESS_UNLOCK(&m->lock_object, opts | LOP_EXCLUSIVE, file, line);
    LOCK_LOG_LOCK("UNLOCK", &m->lock_object, opts, m->mtx_recurse, file,
                  line);
    mtx_assert(m, MA_OWNED);

    __mtx_unlock_spin(m);
}

/*
 * The important part of mtx_trylock{,_flags}()
 * Tries to acquire lock `m.'  If this function is called on a mutex that
 * is already owned, it will recursively acquire the lock.
 */
int
_mtx_trylock(struct mtx *m, int opts, const char *file, int line)
{
    int rval;

    if (SCHEDULER_STOPPED())
        return (1);

    MPASS(curthread != NULL);
    KASSERT(m->mtx_lock != MTX_DESTROYED,
            ("mtx_trylock() of destroyed mutex @ %s:%d", file, line));
    KASSERT(LOCK_CLASS(&m->lock_object) == &lock_class_mtx_sleep,
            ("mtx_trylock() of spin mutex %s @ %s:%d", m->lock_object.lo_name,
             file, line));

    if (mtx_owned(m) && (m->lock_object.lo_flags & LO_RECURSABLE) != 0) {
        m->mtx_recurse++;
        atomic_set_ptr(&m->mtx_lock, MTX_RECURSED);
        rval = 1;
    } else
        rval = _mtx_obtain_lock(m, (uintptr_t)curthread);

    LOCK_LOG_TRY("LOCK", &m->lock_object, opts, rval, file, line);
    if (rval) {
        WITNESS_LOCK(&m->lock_object, opts | LOP_EXCLUSIVE | LOP_TRYLOCK,
                     file, line);
        curthread->td_locks++;
        if (m->mtx_recurse == 0)
            LOCKSTAT_PROFILE_OBTAIN_LOCK_SUCCESS(LS_MTX_LOCK_ACQUIRE,
                                                 m, contested, waittime, file, line);

    }

    return (rval);
}

/*
 * _mtx_lock_sleep: the tougher part of acquiring an MTX_DEF lock.
 *
 * We call this if the lock is either contested (i.e. we need to go to
 * sleep waiting for it), or if we need to recurse on it.
 */
void
_mtx_lock_sleep(struct mtx *m, uintptr_t tid, int opts, const char *file,
                int line)
{
    struct turnstile *ts;
    uintptr_t v;
#ifdef ADAPTIVE_MUTEXES
    volatile struct thread *owner;
#endif

    if (SCHEDULER_STOPPED())
        return;

    if (mtx_owned(m)) {
        KASSERT((m->lock_object.lo_flags & LO_RECURSABLE) != 0,
                ("_mtx_lock_sleep: recursed on non-recursive mutex %s @ %s:%d\n",
                 m->lock_object.lo_name, file, line));
        m->mtx_recurse++;
        atomic_set_ptr(&m->mtx_lock, MTX_RECURSED);
        return;
    }

    lock_profile_obtain_lock_failed(&m->lock_object,
                                    &contested, &waittime);

    while (!_mtx_obtain_lock(m, tid)) {
#ifdef ADAPTIVE_MUTEXES
        /*
         * If the owner is running on another CPU, spin until the
         * owner stops running or the state of the lock changes.
         */
        v = m->mtx_lock;
        if (v != MTX_UNOWNED) {
            owner = (struct thread *)(v & ~MTX_FLAGMASK);
            if (TD_IS_RUNNING(owner)) {
                while (mtx_owner(m) == owner &&
                       TD_IS_RUNNING(owner)) {
                    cpu_spinwait();
                }
                continue;
            }
        }
#endif

        ts = turnstile_trywait(&m->lock_object);
        v = m->mtx_lock;

        /*
         * Check if the lock has been released while spinning for
         * the turnstile chain lock.
         */
        if (v == MTX_UNOWNED) {
            turnstile_cancel(ts);
            continue;
        }

#ifdef ADAPTIVE_MUTEXES
        /*
         * The current lock owner might have started executing
         * on another CPU (or the lock could have changed
         * owners) while we were waiting on the turnstile
         * chain lock.  If so, drop the turnstile lock and try
         * again.
         */
        owner = (struct thread *)(v & ~MTX_FLAGMASK);
        if (TD_IS_RUNNING(owner)) {
            turnstile_cancel(ts);
            continue;
        }
#endif

        /*
         * If the mutex isn't already contested and a failure occurs
         * setting the contested bit, the mutex was either released
         * or the state of the MTX_RECURSED bit changed.
         */
        if ((v & MTX_CONTESTED) == 0 &&
            !atomic_cmpset_ptr(&m->mtx_lock, v, v | MTX_CONTESTED)) {
            turnstile_cancel(ts);
            continue;
        }

        /*
         * We definitely must sleep for this lock.
         */
        mtx_assert(m, MA_NOTOWNED);

        /*
         * Block on the turnstile.
         */
        turnstile_wait(ts, mtx_owner(m), TS_EXCLUSIVE_QUEUE);
    }
    LOCKSTAT_PROFILE_OBTAIN_LOCK_SUCCESS(LS_MTX_LOCK_ACQUIRE, m, contested,
                                         waittime, file, line);
}

extern int kdb_active;			/* Non-zero while in debugger. */
extern const char *panicstr;	/* panic message */

static void
_mtx_lock_spin_failed(struct mtx *m)
{
    struct thread *td;

    td = mtx_owner(m);

    /* If the mutex is unlocked, try again. */
    if (td == NULL)
        return;

    panic("spin lock held too long");
}

/*
 * _mtx_lock_spin: the tougher part of acquiring an MTX_SPIN lock.
 *
 * This is only called if we need to actually spin for the lock. Recursion
 * is handled inline.
 */
void
_mtx_lock_spin(struct mtx *m, uintptr_t tid, int opts, const char *file,
               int line)
{
    int i = 0;

    if (SCHEDULER_STOPPED())
        return;

    lock_profile_obtain_lock_failed(&m->lock_object, &contested, &waittime);
    while (!_mtx_obtain_lock(m, tid)) {

        /* Give interrupts a chance while we spin. */
        spinlock_exit();
        while (m->mtx_lock != MTX_UNOWNED) {
            if (i++ < 10000000) {
                cpu_spinwait();
                continue;
            }
            if (i < 60000000 || kdb_active || panicstr != NULL)
                DELAY(1);
            else
                _mtx_lock_spin_failed(m);
            cpu_spinwait();
        }
        spinlock_enter();
    }
}

void
_thread_lock_flags(struct thread *td, int opts, const char *file, int line)
{
    struct mtx *m;
    uintptr_t tid;
    int i;

    i = 0;
    tid = (uintptr_t)curthread;

    if (SCHEDULER_STOPPED())
        return;

    for (;;) {
    retry:
        spinlock_enter();
        m = td->td_lock;
        KASSERT(m->mtx_lock != MTX_DESTROYED,
                ("thread_lock() of destroyed mutex @ %s:%d", file, line));
        KASSERT(LOCK_CLASS(&m->lock_object) == &lock_class_mtx_spin,
                ("thread_lock() of sleep mutex %s @ %s:%d",
                 m->lock_object.lo_name, file, line));
        if (mtx_owned(m))
            KASSERT((m->lock_object.lo_flags & LO_RECURSABLE) != 0,
                    ("thread_lock: recursed on non-recursive mutex %s @ %s:%d\n",
                     m->lock_object.lo_name, file, line));
        WITNESS_CHECKORDER(&m->lock_object,
                           opts | LOP_NEWORDER | LOP_EXCLUSIVE, file, line, NULL);
        while (!_mtx_obtain_lock(m, tid)) {
            if (m->mtx_lock == tid) {
                m->mtx_recurse++;
                break;
            }
            lock_profile_obtain_lock_failed(&m->lock_object,
                                            &contested, &waittime);
            /* Give interrupts a chance while we spin. */
            spinlock_exit();
            while (m->mtx_lock != MTX_UNOWNED) {
                if (i++ < 10000000)
                    cpu_spinwait();
                else if (i < 60000000 || kdb_active || panicstr != NULL)
                    DELAY(1);
                else
                    _mtx_lock_spin_failed(m);
                cpu_spinwait();
                if (m != td->td_lock)
                    goto retry;
            }
            spinlock_enter();
        }
        if (m == td->td_lock)
            break;
        __mtx_unlock_spin(m);	/* does spinlock_exit() */
    }
    if (m->mtx_recurse == 0)
        LOCKSTAT_PROFILE_OBTAIN_LOCK_SUCCESS(LS_MTX_SPIN_LOCK_ACQUIRE,
                                             m, contested, waittime, (file), (line));
    LOCK_LOG_LOCK("LOCK", &m->lock_object, opts, m->mtx_recurse, file,
                  line);
    WITNESS_LOCK(&m->lock_object, opts | LOP_EXCLUSIVE, file, line);
}

struct mtx *
thread_lock_block(struct thread *td)
{
    struct mtx *lock;

    THREAD_LOCK_ASSERT(td, MA_OWNED);
    lock = td->td_lock;
    td->td_lock = &blocked_lock;
    mtx_unlock_spin(lock);

    return (lock);
}

void
thread_lock_unblock(struct thread *td, struct mtx *new)
{
    mtx_assert(new, MA_OWNED);
    MPASS(td->td_lock == &blocked_lock);
    atomic_store_rel_ptr((volatile void *)&td->td_lock, (uintptr_t)new);
}

void
thread_lock_set(struct thread *td, struct mtx *new)
{
    struct mtx *lock;

    mtx_assert(new, MA_OWNED);
    THREAD_LOCK_ASSERT(td, MA_OWNED);
    lock = td->td_lock;
    td->td_lock = new;
    mtx_unlock_spin(lock);
}

/*
 * _mtx_unlock_sleep: the tougher part of releasing an MTX_DEF lock.
 *
 * We are only called here if the lock is recursed or contested (i.e. we
 * need to wake up a blocked thread).
 */
void
_mtx_unlock_sleep(struct mtx *m, int opts, const char *file, int line)
{
    struct turnstile *ts;

    if (SCHEDULER_STOPPED())
        return;

    if (mtx_recursed(m)) {
        if (--(m->mtx_recurse) == 0)
            atomic_clear_ptr(&m->mtx_lock, MTX_RECURSED);
        return;
    }

    /*
     * We have to lock the chain before the turnstile so this turnstile
     * can be removed from the hash list if it is empty.
     */
    turnstile_chain_lock(&m->lock_object);
    ts = turnstile_lookup(&m->lock_object);
    if (LOCK_LOG_TEST(&m->lock_object, opts))
        CTR1(KTR_LOCK, "_mtx_unlock_sleep: %p contested", m);
    MPASS(ts != NULL);
    turnstile_broadcast(ts, TS_EXCLUSIVE_QUEUE);
    _mtx_release_lock_quick(m);

    /*
     * This turnstile is now no longer associated with the mutex.  We can
     * unlock the chain lock so a new turnstile may take it's place.
     */
    turnstile_unpend(ts, TS_EXCLUSIVE_LOCK);
    turnstile_chain_unlock(&m->lock_object);
}

/*
 * All the unlocking of MTX_SPIN locks is done inline.
 * See the __mtx_unlock_spin() macro for the details.
 */


/*
 * General init routine used by the MTX_SYSINIT() macro.
 */
void
mtx_sysinit(void *arg)
{
    struct mtx_args *margs = arg;

    mtx_init(margs->ma_mtx, margs->ma_desc, NULL, margs->ma_opts);
}

/*
 * Mutex initialization routine; initialize lock `m' of type contained in
 * `opts' with options contained in `opts' and name `name.'  The optional
 * lock type `type' is used as a general lock category name for use with
 * witness.
 */
void
mtx_init(struct mtx *m, const char *name, const char *type, int opts)
{
    struct lock_class *class;
    int flags;

    MPASS((opts & ~(MTX_SPIN | MTX_QUIET | MTX_RECURSE |
                    MTX_NOWITNESS | MTX_DUPOK | MTX_NOPROFILE)) == 0);
    ASSERT_ATOMIC_LOAD_PTR(m->mtx_lock,
                           ("%s: mtx_lock not aligned for %s: %p", __func__, name,
                            &m->mtx_lock));

    /* Determine lock class and lock flags. */
    if (opts & MTX_SPIN)
        class = &lock_class_mtx_spin;
    else
        class = &lock_class_mtx_sleep;
    flags = 0;
    if (opts & MTX_QUIET)
        flags |= LO_QUIET;
    if (opts & MTX_RECURSE)
        flags |= LO_RECURSABLE;
    if ((opts & MTX_NOWITNESS) == 0)
        flags |= LO_WITNESS;
    if (opts & MTX_DUPOK)
        flags |= LO_DUPOK;
    if (opts & MTX_NOPROFILE)
        flags |= LO_NOPROFILE;

    /* Initialize mutex. */
    m->mtx_lock = MTX_UNOWNED;
    m->mtx_recurse = 0;

    lock_init(&m->lock_object, class, name, type, flags);
}

/*
 * Remove lock `m' from all_mtx queue.  We don't allow MTX_QUIET to be
 * passed in as a flag here because if the corresponding mtx_init() was
 * called with MTX_QUIET set, then it will already be set in the mutex's
 * flags.
 */
void
mtx_destroy(struct mtx *m)
{

    if (!mtx_owned(m))
        MPASS(mtx_unowned(m));
    else {
        MPASS((m->mtx_lock & (MTX_RECURSED|MTX_CONTESTED)) == 0);

        /* Perform the non-mtx related part of mtx_unlock_spin(). */
        if (LOCK_CLASS(&m->lock_object) == &lock_class_mtx_spin)
            spinlock_exit();
        else
            curthread->td_locks--;

        lock_profile_release_lock(&m->lock_object);
        /* Tell witness this isn't locked to make it happy. */
        WITNESS_UNLOCK(&m->lock_object, LOP_EXCLUSIVE, __FILE__,
                       __LINE__);
    }

    m->mtx_lock = MTX_DESTROYED;
    lock_destroy(&m->lock_object);
}

/*
 * Intialize the mutex code and system mutexes.  This is called from the MD
 * startup code prior to mi_startup().  The per-CPU data space needs to be
 * setup before this is called.
 */
void
mutex_init(void)
{

    /* Setup turnstiles so that sleep mutexes work. */
    init_turnstiles();

    /*
     * Initialize mutexes.
     */
    mtx_init(&Giant, "Giant", NULL, MTX_DEF | MTX_RECURSE);
    mtx_init(&blocked_lock, "blocked lock", NULL, MTX_SPIN);
    blocked_lock.mtx_lock = 0xdeadc0de;	/* Always blocked. */
    mtx_init(&proc0.p_mtx, "process lock", NULL, MTX_DEF | MTX_DUPOK);
    mtx_init(&proc0.p_slock, "process slock", NULL, MTX_SPIN | MTX_RECURSE);
    mtx_init(&devmtx, "cdev", NULL, MTX_DEF);
    mtx_lock(&Giant);
}

