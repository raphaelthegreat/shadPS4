// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/libraries/error_codes.h"
#include "core/libraries/libs.h"
#include "core/libraries/kernel/threads/threads.h"

namespace Libraries::Kernel {

static std::mutex RwlockStaticLock;

#define	THR_RWLOCK_INITIALIZER ((PthreadRwlock*)NULL)
#define	THR_RWLOCK_DESTROYED ((PthreadRwlock*)1)

#define CHECK_AND_INIT_RWLOCK							\
if (__predict_false((prwlock = (*rwlock)) <= THR_RWLOCK_DESTROYED)) {	\
        if (prwlock == THR_RWLOCK_INITIALIZER) {			\
            int ret;						\
            ret = init_static(_get_curthread(), rwlock);		\
            if (ret)						\
            return (ret);					\
    } else if (prwlock == THR_RWLOCK_DESTROYED) {			\
            return (EINVAL);					\
    }								\
        prwlock = *rwlock;						\
}

static int rwlock_init(PthreadRwlockT* rwlock, const PthreadRwlockAttrT *attr) {
    PthreadRwlock* prwlock = (PthreadRwlock*)calloc(1, sizeof(PthreadRwlock));
    if (prwlock == nullptr) {
        return POSIX_ENOMEM;
    }
    std::construct_at(prwlock);
    *rwlock = prwlock;
    return 0;
}

int PS4_SYSV_ABI posix_pthread_rwlock_destroy(PthreadRwlockT *rwlock) {
    PthreadRwlockT prwlock = *rwlock;
    if (prwlock == THR_RWLOCK_INITIALIZER) {
        return 0;
    }
    if (prwlock == THR_RWLOCK_DESTROYED) {
        return POSIX_EINVAL;
    }
    *rwlock = THR_RWLOCK_DESTROYED;
    free(prwlock);
    return 0;
}

static int init_static(Pthread* thread, PthreadRwlockT* rwlock) {
    std::scoped_lock lk{RwlockStaticLock};
    if (*rwlock == THR_RWLOCK_INITIALIZER) {
        return rwlock_init(rwlock, nullptr);
    }
    return 0;
}

int PS4_SYSV_ABI posix_pthread_rwlock_init(PthreadRwlockT* rwlock, const PthreadRwlockAttrT* attr) {
    *rwlock = nullptr;
    return (rwlock_init(rwlock, attr));
}

static int rwlock_rdlock_common(PthreadRwlockT *rwlock, const struct timespec *abstime) {
    Pthread* curthread = _get_curthread();
    PthreadRwlockT prwlock;
    struct timespec ts, ts2, *tsp;
    int flags;
    int ret;

    CHECK_AND_INIT_RWLOCK

    if (curthread->rdlock_count) {
        /*
         * To avoid having to track all the rdlocks held by
         * a thread or all of the threads that hold a rdlock,
         * we keep a simple count of all the rdlocks held by
         * a thread.  If a thread holds any rdlocks it is
         * possible that it is attempting to take a recursive
         * rdlock.  If there are blocked writers and precedence
         * is given to them, then that would result in the thread
         * deadlocking.  So allowing a thread to take the rdlock
         * when it already has one or more rdlocks avoids the
         * deadlock.  I hope the reader can follow that logic ;-)
         */
        flags = URWLOCK_PREFER_READER;
    } else {
        flags = 0;
    }

    /*
     * POSIX said the validity of the abstimeout parameter need
     * not be checked if the lock can be immediately acquired.
     */
    ret = _thr_rwlock_tryrdlock(&prwlock->lock, flags);
    if (ret == 0) {
        curthread->rdlock_count++;
        return (ret);
    }

    if (__predict_false(abstime &&
                        (abstime->tv_nsec >= 1000000000 || abstime->tv_nsec < 0)))
        return (EINVAL);

    for (;;) {
        if (abstime) {
            clock_gettime(CLOCK_REALTIME, &ts);
            TIMESPEC_SUB(&ts2, abstime, &ts);
            if (ts2.tv_sec < 0 ||
                (ts2.tv_sec == 0 && ts2.tv_nsec <= 0))
                return (ETIMEDOUT);
            tsp = &ts2;
        } else
            tsp = NULL;

        /* goto kernel and lock it */
        ret = __thr_rwlock_rdlock(&prwlock->lock, flags, tsp);
        if (ret != EINTR)
            break;

        /* if interrupted, try to lock it in userland again. */
        if (_thr_rwlock_tryrdlock(&prwlock->lock, flags) == 0) {
            ret = 0;
            break;
        }
    }
    if (ret == 0)
        curthread->rdlock_count++;
    return (ret);
}

int PS4_SYSV_ABI posix_pthread_rwlock_rdlock(PthreadRwlockT *rwlock)
{
    return rwlock_rdlock_common(rwlock, nullptr);
}

int PS4_SYSV_ABI posix_pthread_rwlock_timedrdlock(PthreadRwlockT *rwlock, const timespec *abstime) {
    return rwlock_rdlock_common(rwlock, abstime);
}

int PS4_SYSV_ABI posix_pthread_rwlock_tryrdlock(PthreadRwlockT* rwlock)
{
    struct pthread *curthread = _get_curthread();
    PthreadRwlockT prwlock;
    int flags;
    int ret;

    CHECK_AND_INIT_RWLOCK

    if (curthread->rdlock_count) {
        /*
         * To avoid having to track all the rdlocks held by
         * a thread or all of the threads that hold a rdlock,
         * we keep a simple count of all the rdlocks held by
         * a thread.  If a thread holds any rdlocks it is
         * possible that it is attempting to take a recursive
         * rdlock.  If there are blocked writers and precedence
         * is given to them, then that would result in the thread
         * deadlocking.  So allowing a thread to take the rdlock
         * when it already has one or more rdlocks avoids the
         * deadlock.  I hope the reader can follow that logic ;-)
         */
        flags = URWLOCK_PREFER_READER;
    } else {
        flags = 0;
    }

    ret = _thr_rwlock_tryrdlock(&prwlock->lock, flags);
    if (ret == 0)
        curthread->rdlock_count++;
    return (ret);
}

int
_PthreadRwlockTrywrlock (PthreadRwlockT *rwlock)
{
    struct pthread *curthread = _get_curthread();
    PthreadRwlockT prwlock;
    int ret;

    CHECK_AND_INIT_RWLOCK

            ret = _thr_rwlock_trywrlock(&prwlock->lock);
    if (ret == 0)
        prwlock->owner = curthread;
    return (ret);
}

static int
rwlock_wrlock_common (PthreadRwlockT *rwlock, const struct timespec *abstime)
{
    struct pthread *curthread = _get_curthread();
    PthreadRwlockT prwlock;
    struct timespec ts, ts2, *tsp;
    int ret;

    CHECK_AND_INIT_RWLOCK

            /*
             * POSIX said the validity of the abstimeout parameter need
             * not be checked if the lock can be immediately acquired.
             */
                ret = _thr_rwlock_trywrlock(&prwlock->lock);
    if (ret == 0) {
        prwlock->owner = curthread;
        return (ret);
    }

    if (__predict_false(abstime &&
                        (abstime->tv_nsec >= 1000000000 || abstime->tv_nsec < 0)))
        return (EINVAL);

    for (;;) {
        if (abstime != NULL) {
            clock_gettime(CLOCK_REALTIME, &ts);
            TIMESPEC_SUB(&ts2, abstime, &ts);
            if (ts2.tv_sec < 0 ||
                (ts2.tv_sec == 0 && ts2.tv_nsec <= 0))
                return (ETIMEDOUT);
            tsp = &ts2;
        } else
            tsp = NULL;

        /* goto kernel and lock it */
        ret = __thr_rwlock_wrlock(&prwlock->lock, tsp);
        if (ret == 0) {
            prwlock->owner = curthread;
            break;
        }

        if (ret != EINTR)
            break;

        /* if interrupted, try to lock it in userland again. */
        if (_thr_rwlock_trywrlock(&prwlock->lock) == 0) {
            ret = 0;
            prwlock->owner = curthread;
            break;
        }
    }
    return (ret);
}

int
_pthread_rwlock_wrlock (PthreadRwlockT *rwlock)
{
    return (rwlock_wrlock_common (rwlock, NULL));
}

int
_PthreadRwlockTimedwrlock (PthreadRwlockT *rwlock,
                            const struct timespec *abstime)
{
    return (rwlock_wrlock_common (rwlock, abstime));
}

int
_pthread_rwlock_unlock (PthreadRwlockT *rwlock)
{
    struct pthread *curthread = _get_curthread();
    PthreadRwlockT prwlock;
    int ret;
    int32_t state;

    prwlock = *rwlock;

    if (__predict_false(prwlock <= THR_RWLOCK_DESTROYED))
        return (EINVAL);

    state = prwlock->lock.rw_state;
    if (state & URWLOCK_WRITE_OWNER) {
        if (__predict_false(prwlock->owner != curthread))
            return (EPERM);
        prwlock->owner = NULL;
    }

    ret = _thr_rwlock_unlock(&prwlock->lock);
    if (ret == 0 && (state & URWLOCK_WRITE_OWNER) == 0)
        curthread->rdlock_count--;

    return (ret);
}

void RwlockSymbolsRegister(Core::Loader::SymbolsResolver* sym) {
    LIB_FUNCTION("1471ajPzxh0", "libkernel", 1, "libkernel", 1, 1, posix_pthread_rwlock_destroy);
    LIB_FUNCTION("ytQULN-nhL4", "libkernel", 1, "libkernel", 1, 1, posix_pthread_rwlock_init);
    LIB_FUNCTION("iGjsr1WAtI0", "libkernel", 1, "libkernel", 1, 1, posix_pthread_rwlock_rdlock);
    LIB_FUNCTION("dYv-+If2GPk", "libkernel", 1, "libkernel", 1, 1,
                 posix_pthread_rwlock_reltimedrdlock_np);
    LIB_FUNCTION("RRnSj8h8VR4", "libkernel", 1, "libkernel", 1, 1,
                 posix_pthread_rwlock_reltimedwrlock_np);
    LIB_FUNCTION("Uwxgnsi3xeM", "libkernel", 1, "libkernel", 1, 1, posix_pthread_rwlock_setname_np);
    LIB_FUNCTION("lb8lnYo-o7k", "libkernel", 1, "libkernel", 1, 1,
                 posix_PthreadRwlockTimedrdlock);
    LIB_FUNCTION("9zklzAl9CGM", "libkernel", 1, "libkernel", 1, 1,
                 posix_PthreadRwlockTimedwrlock);
    LIB_FUNCTION("SFxTMOfuCkE", "libkernel", 1, "libkernel", 1, 1, posix_PthreadRwlockTryrdlock);
    LIB_FUNCTION("XhWHn6P5R7U", "libkernel", 1, "libkernel", 1, 1, posix_PthreadRwlockTrywrlock);
    LIB_FUNCTION("EgmLo6EWgso", "libkernel", 1, "libkernel", 1, 1, posix_pthread_rwlock_unlock);
    LIB_FUNCTION("sIlRvQqsN2Y", "libkernel", 1, "libkernel", 1, 1, posix_pthread_rwlock_wrlock);
    LIB_FUNCTION("qsdmgXjqSgk", "libkernel", 1, "libkernel", 1, 1,
                 posix_pthread_rwlockattr_destroy);
    LIB_FUNCTION("VqEMuCv-qHY", "libkernel", 1, "libkernel", 1, 1,
                 posix_pthread_rwlockattr_getpshared);
    LIB_FUNCTION("l+bG5fsYkhg", "libkernel", 1, "libkernel", 1, 1,
                 posix_pthread_rwlockattr_gettype_np);
    LIB_FUNCTION("xFebsA4YsFI", "libkernel", 1, "libkernel", 1, 1, posix_pthread_rwlockattr_init);
    LIB_FUNCTION("OuKg+kRDD7U", "libkernel", 1, "libkernel", 1, 1,
                 posix_pthread_rwlockattr_setpshared);
    LIB_FUNCTION("8NuOHiTr1Vw", "libkernel", 1, "libkernel", 1, 1,
                 posix_pthread_rwlockattr_settype_np);
    LIB_FUNCTION("1471ajPzxh0", "libScePosix", 1, "libkernel", 1, 1, posix_pthread_rwlock_destroy);
    LIB_FUNCTION("ytQULN-nhL4", "libScePosix", 1, "libkernel", 1, 1, posix_pthread_rwlock_init);
    LIB_FUNCTION("iGjsr1WAtI0", "libScePosix", 1, "libkernel", 1, 1, posix_pthread_rwlock_rdlock);
    LIB_FUNCTION("lb8lnYo-o7k", "libScePosix", 1, "libkernel", 1, 1,
                 posix_PthreadRwlockTimedrdlock);
    LIB_FUNCTION("9zklzAl9CGM", "libScePosix", 1, "libkernel", 1, 1,
                 posix_PthreadRwlockTimedwrlock);
    LIB_FUNCTION("SFxTMOfuCkE", "libScePosix", 1, "libkernel", 1, 1,
                 posix_PthreadRwlockTryrdlock);
    LIB_FUNCTION("XhWHn6P5R7U", "libScePosix", 1, "libkernel", 1, 1,
                 posix_PthreadRwlockTrywrlock);
    LIB_FUNCTION("EgmLo6EWgso", "libScePosix", 1, "libkernel", 1, 1, posix_pthread_rwlock_unlock);
    LIB_FUNCTION("sIlRvQqsN2Y", "libScePosix", 1, "libkernel", 1, 1, posix_pthread_rwlock_wrlock);
    LIB_FUNCTION("qsdmgXjqSgk", "libScePosix", 1, "libkernel", 1, 1,
                 posix_pthread_rwlockattr_destroy);
    LIB_FUNCTION("VqEMuCv-qHY", "libScePosix", 1, "libkernel", 1, 1,
                 posix_pthread_rwlockattr_getpshared);
    LIB_FUNCTION("l+bG5fsYkhg", "libScePosix", 1, "libkernel", 1, 1,
                 posix_pthread_rwlockattr_gettype_np);
    LIB_FUNCTION("xFebsA4YsFI", "libScePosix", 1, "libkernel", 1, 1, posix_pthread_rwlockattr_init);
    LIB_FUNCTION("OuKg+kRDD7U", "libScePosix", 1, "libkernel", 1, 1,
                 posix_pthread_rwlockattr_setpshared);
    LIB_FUNCTION("8NuOHiTr1Vw", "libScePosix", 1, "libkernel", 1, 1,
                 posix_pthread_rwlockattr_settype_np);
    LIB_FUNCTION("i2ifZ3fS2fo", "libkernel", 1, "libkernel", 1, 1, scePthreadRwlockattrDestroy);
    LIB_FUNCTION("LcOZBHGqbFk", "libkernel", 1, "libkernel", 1, 1, scePthreadRwlockattrGetpshared);
    LIB_FUNCTION("Kyls1ChFyrc", "libkernel", 1, "libkernel", 1, 1, scePthreadRwlockattrGettype);
    LIB_FUNCTION("yOfGg-I1ZII", "libkernel", 1, "libkernel", 1, 1, scePthreadRwlockattrInit);
    LIB_FUNCTION("-ZvQH18j10c", "libkernel", 1, "libkernel", 1, 1, scePthreadRwlockattrSetpshared);
    LIB_FUNCTION("h-OifiouBd8", "libkernel", 1, "libkernel", 1, 1, scePthreadRwlockattrSettype);
    LIB_FUNCTION("BB+kb08Tl9A", "libkernel", 1, "libkernel", 1, 1, scePthreadRwlockDestroy);
    LIB_FUNCTION("6ULAa0fq4jA", "libkernel", 1, "libkernel", 1, 1, scePthreadRwlockInit);
    LIB_FUNCTION("Ox9i0c7L5w0", "libkernel", 1, "libkernel", 1, 1, scePthreadRwlockRdlock);
    LIB_FUNCTION("iPtZRWICjrM", "libkernel", 1, "libkernel", 1, 1, scePthreadRwlockTimedrdlock);
    LIB_FUNCTION("adh--6nIqTk", "libkernel", 1, "libkernel", 1, 1, scePthreadRwlockTimedwrlock);
    LIB_FUNCTION("XD3mDeybCnk", "libkernel", 1, "libkernel", 1, 1, scePthreadRwlockTryrdlock);
    LIB_FUNCTION("bIHoZCTomsI", "libkernel", 1, "libkernel", 1, 1, scePthreadRwlockTrywrlock);
    LIB_FUNCTION("+L98PIbGttk", "libkernel", 1, "libkernel", 1, 1, scePthreadRwlockUnlock);
    LIB_FUNCTION("mqdNorrB+gI", "libkernel", 1, "libkernel", 1, 1, scePthreadRwlockWrlock);
}
} // namespace Libraries::Kernel
