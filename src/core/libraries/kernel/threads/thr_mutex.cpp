// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <chrono>
#include "common/types.h"
#include "core/libraries/error_codes.h"
#include "core/libraries/kernel/threads/threads.h"

namespace Libraries::Kernel {

static constexpr u32 MUTEX_ADAPTIVE_SPINS = 2000;
static std::mutex MutxStaticLock;

#define	THR_MUTEX_INITIALIZER ((PthreadMutex*)NULL)
#define	THR_ADAPTIVE_MUTEX_INITIALIZER ((PthreadMutex*)1)
#define	THR_MUTEX_DESTROYED	((PthreadMutex*)2)

#define CPU_SPINWAIT __asm__ volatile("pause")

#define CHECK_AND_INIT_MUTEX						\
if (PthreadMutex* m = *mutex; m <= THR_MUTEX_DESTROYED) [[unlikely]] { \
        if (m == THR_MUTEX_DESTROYED) { \
            return POSIX_EINVAL; \
    } \
        if (s32 ret = init_static(_get_curthread(), mutex); ret) { \
            return ret; \
    } \
        m = *mutex; \
} \

static constexpr PthreadMutexAttr PthreadMutexattrDefault = {
    .m_type = PthreadMutexType::ErrorCheck,
    .m_protocol = PthreadMutexProt::None,
    .m_ceiling = 0
};

static constexpr PthreadMutexAttr PthreadMutexattrAdaptiveDefault = {
    .m_type = PthreadMutexType::AdaptiveNp,
    .m_protocol = PthreadMutexProt::None,
    .m_ceiling = 0
};

using CallocFun = void *(*)(size_t, size_t);

static int mutex_init(PthreadMutexT* mutex, const PthreadMutexAttr* mutex_attr, CallocFun calloc_cb) {
    const PthreadMutexAttr* attr;
    if (mutex_attr == NULL) {
        attr = &PthreadMutexattrDefault;
    } else {
        attr = mutex_attr;
        if (attr->m_type < PthreadMutexType::ErrorCheck ||
            attr->m_type >= PthreadMutexType::Max) {
            return POSIX_EINVAL;
        }
        if (attr->m_protocol > PthreadMutexProt::Protect) {
            return POSIX_EINVAL;
        }
    }
    PthreadMutex* pmutex = (PthreadMutex*)std::calloc(1, sizeof(PthreadMutex));
    if (pmutex == nullptr) {
        return POSIX_ENOMEM;
    }

    std::construct_at(pmutex);
    pmutex->m_flags = PthreadMutexFlags(attr->m_type);
    pmutex->m_owner = NULL;
    pmutex->m_count = 0;
    pmutex->m_spinloops = 0;
    pmutex->m_yieldloops = 0;
    pmutex->m_protocol = attr->m_protocol;
    if (attr->m_type == PthreadMutexType::AdaptiveNp) {
        pmutex->m_spinloops = MUTEX_ADAPTIVE_SPINS;
        pmutex->m_yieldloops = _thr_yieldloops;
    }

    *mutex = pmutex;
    return 0;
}

static int init_static(Pthread* thread, PthreadMutexT* mutex) {
    std::scoped_lock lk{MutxStaticLock};

    if (*mutex == THR_MUTEX_INITIALIZER) {
        return mutex_init(mutex, &PthreadMutexattrDefault, calloc);
    } else if (*mutex == THR_ADAPTIVE_MUTEX_INITIALIZER) {
        return mutex_init(mutex, &PthreadMutexattrAdaptiveDefault, calloc);
    }
    return 0;
}

static void set_inherited_priority(Pthread* curthread, PthreadMutex* m) {
    PthreadMutex* m2 = TAILQ_LAST(&curthread->pp_mutexq, mutex_queue);
    if (m2 != NULL)
        m->m_lock.m_ceilings[1] = m2->m_lock.m_ceilings[0];
    else
        m->m_lock.m_ceilings[1] = -1;
}

int PS4_SYSV_ABI posix_pthread_mutex_init(PthreadMutexT* mutex, const PthreadMutexAttrT* mutex_attr) {
    return mutex_init(mutex, mutex_attr ? *mutex_attr : nullptr, calloc);
}

int PS4_SYSV_ABI posix_pthread_mutex_destroy(PthreadMutexT* mutex) {
    PthreadMutexT m = *mutex;
    if (m < THR_MUTEX_DESTROYED) {
        return 0;
    }
    if (m == THR_MUTEX_DESTROYED) {
        return POSIX_EINVAL;
    }
    if (m->m_owner != nullptr) {
        return EBUSY;
    }
    *mutex = THR_MUTEX_DESTROYED;
    free(m);
    return 0;
}

static int mutex_self_trylock(PthreadMutex* m) {
    switch (m->Type()) {
    case PthreadMutexType::ErrorCheck:
    case PthreadMutexType::Normal:
        return POSIX_EBUSY;
    case PthreadMutexType::Recursive: {
        /* Increment the lock count: */
        if (m->m_count + 1 > 0) {
            m->m_count++;
            return 0;
        }
        return POSIX_EAGAIN;
    }
    default:
        return POSIX_EINVAL;
    }
}

static int mutex_self_lock(PthreadMutex* m, const timespec *abstime) {
    struct timespec	ts1, ts2;
    int	ret;

    switch (m->Type()) {
    case PthreadMutexType::ErrorCheck:
    case PthreadMutexType::AdaptiveNp: {
        if (abstime) {
            if (abstime->tv_sec < 0 || abstime->tv_nsec < 0 ||
                abstime->tv_nsec >= 1000000000) {
                return POSIX_EINVAL;
            } else {
                clock_gettime(CLOCK_REALTIME, &ts1);
                TIMESPEC_SUB(&ts2, abstime, &ts1);
                __sys_nanosleep(&ts2, NULL);
                return POSIX_ETIMEDOUT;
            }
        }
        /*
         * POSIX specifies that mutexes should return
         * EDEADLK if a recursive lock is detected.
         */
        return POSIX_EDEADLK;
    }
    case PthreadMutexType::Normal: {
        /*
         * What SS2 define as a 'normal' mutex.  Intentionally
         * deadlock on attempts to get a lock you already own.
         */
        if (abstime) {
            if (abstime->tv_sec < 0 || abstime->tv_nsec < 0 ||
                abstime->tv_nsec >= 1000000000) {
                return POSIX_EINVAL;
            } else {
                clock_gettime(CLOCK_REALTIME, &ts1);
                TIMESPEC_SUB(&ts2, abstime, &ts1);
                __sys_nanosleep(&ts2, NULL);
                return POSIX_ETIMEDOUT;
            }
        }
        ts1.tv_sec = 30;
        ts1.tv_nsec = 0;
        for (;;)
            __sys_nanosleep(&ts1, NULL);
        return 0;
    }
    case PthreadMutexType::Recursive: {
        /* Increment the lock count: */
        if (m->m_count + 1 > 0) {
            m->m_count++;
            return 0;
        }
        return POSIX_EAGAIN;
    }
    default:
        return POSIX_EINVAL;
    }
}

static int mutex_trylock_common(PthreadMutexT* mutex)
{
    Pthread* curthread = _get_curthread();
    PthreadMutex* m = *mutex;

    const u32 id = u32(curthread->tid);
    if (True(m->m_flags & PthreadMutexFlags::Private)) {
        curthread->CriticalEnter();
    }

    s32 ret = m->m_lock.try_lock() ? 0 : POSIX_EBUSY;
    if (ret == 0) [[likely]] {
        curthread->Enqueue(m);
    } else if (m->m_owner == curthread) {
        ret = mutex_self_trylock(m);
    }
    if (ret && True(m->m_flags & PthreadMutexFlags::Private)) {
        curthread->CriticalLeave();
    }
    return (ret);
}

int PS4_SYSV_ABI posix_pthread_mutex_trylock(PthreadMutexT* mutex) {
    CHECK_AND_INIT_MUTEX
    return (mutex_trylock_common(mutex));
}

static int mutex_lock_sleep(Pthread* curthread, PthreadMutex* m, const timespec* abstime) {
    uint32_t owner;
    int	count;
    int	ret;

    if (m->m_owner == curthread) {
        return mutex_self_lock(m, abstime);
    }

    const u32 id = u32(curthread->tid);
    /*
     * For adaptive mutexes, spin for a bit in the expectation
     * that if the application requests this mutex type then
     * the lock is likely to be released quickly and it is
     * faster than entering the kernel
     */
    if (m->m_protocol != PthreadMutexProt::None) [[unlikely]] {
        goto sleep_in_kernel;
    }

    count = m->m_spinloops;
    while (count--) {
        owner = m->m_lock.m_owner;
        if ((owner & ~UMUTEX_CONTESTED) == 0) {
            if (atomic_cmpset_acq_32(&m->m_lock.m_owner, owner, id|owner)) {
                ret = 0;
                goto done;
            }
        }
        CPU_SPINWAIT;
    }

yield_loop:
    count = m->m_yieldloops;
    while (count--) {
        std::this_thread::yield();
        owner = m->m_lock.m_owner;
        if ((owner & ~UMUTEX_CONTESTED) == 0) {
            if (atomic_cmpset_acq_32(&m->m_lock.m_owner, owner, id|owner)) {
                ret = 0;
                goto done;
            }
        }
    }

sleep_in_kernel:
    if (abstime == NULL) {
        m->m_lock.lock();
        ret = 0;
    } else if (abstime->tv_nsec < 0 || abstime->tv_nsec >= 1000000000) [[unlikely]] {
        ret = POSIX_EINVAL;
    } else {
        ret = m->m_lock.try_lock_until() ? 0 :
        ret = __thr_umutex_timedlock(&m->m_lock, id, abstime);
    }
done:
    if (ret == 0) {
        curthread->Enqueue(m);
    }

    return (ret);
}

static inline int mutex_lock_common(PthreadMutex* m, const timespec *abstime, int cvattach) {
    Pthread *curthread  = _get_curthread();
    int ret;

    if (!cvattach && True(m->m_flags & PthreadMutexFlags::Private)) {
        curthread->CriticalEnter();
    }
    if (_thr_umutex_trylock2(&m->m_lock, TID(curthread)) == 0) {
        curthread->Enqueue(m);
        ret = 0;
    } else {
        ret = mutex_lock_sleep(curthread, m, abstime);
    }
    if (ret && True(m->m_flags & PthreadMutexFlags::Private) && !cvattach) {
        curthread->CriticalLeave();
    }
    return ret;
}

int PS4_SYSV_ABI posix_pthread_mutex_lock(PthreadMutexT* mutex) {
    _thr_check_init();
    CHECK_AND_INIT_MUTEX
    return (mutex_lock_common(m, NULL, 0));
}

int PS4_SYSV_ABI posix_pthread_mutex_timedlock(PthreadMutexT* mutex, const timespec* abstime) {
    _thr_check_init();
    CHECK_AND_INIT_MUTEX
    return (mutex_lock_common(m, abstime, 0));
}

static int mutex_unlock_common(PthreadMutex* m, int cv) {
    Pthread *curthread = _get_curthread();
    int defered;

    if (m <= THR_MUTEX_DESTROYED) [[unlikely]] {
        if (m == THR_MUTEX_DESTROYED) {
            return POSIX_EINVAL;
        }
        return POSIX_EPERM;
    }

    /*
     * Check if the running thread is not the owner of the mutex.
     */
    if (m->m_owner != curthread) [[unlikely]] {
        return POSIX_EPERM;
    }

    const u32 id = u32(curthread->tid);
    if (m->Type() == PthreadMutexType::Recursive && m->m_count > 0) [[unlikely]] {
        m->m_count--;
    } else {
        if (True(m->m_flags & PthreadMutexFlags::Defered)) {
            defered = 1;
            m->m_flags &= ~PthreadMutexFlags::Defered;
        } else {
            defered = 0;
        }

        curthread->Dequeue(m);
        m->m_lock.unlock();

        if (curthread->will_sleep == 0 && defered)  {
            _thr_wake_all(curthread->defer_waiters,
                          curthread->nwaiter_defer);
            curthread->nwaiter_defer = 0;
        }
    }
    if (!cv && True(m->m_flags & PthreadMutexFlags::Private)) {
        curthread->CriticalLeave();
    }
    return (0);
}

int PS4_SYSV_ABI posix_pthread_mutex_unlock(PthreadMutexT* mutex) {
    PthreadMutex* mp = *mutex;
    return (mutex_unlock_common(mp, 0));
}

int _mutex_cv_lock(PthreadMutex* m, int count) {
    const s32 error = mutex_lock_common(m, NULL, 1);
    if (error == 0) {
        m->m_count = count;
    }
    return error;
}

int _mutex_cv_unlock(PthreadMutex* m, int* count) {
    /*
     * Clear the count in case this is a recursive mutex.
     */
    *count = m->m_count;
    m->m_count = 0;
    (void)mutex_unlock_common(m, 1);
    return (0);
}

int _mutex_cv_attach(PthreadMutex* m, int count)
{
    Pthread* curthread = _get_curthread();
    curthread->Enqueue(m);
    m->m_count = count;
    return 0;
}

int _mutex_cv_detach(PthreadMutex* mp, int* recurse)
{
    Pthread *curthread = _get_curthread();
    int     defered;
    int     error;

    if ((error = _mutex_owned(curthread, mp)) != 0)
        return (error);

    /*
     * Clear the count in case this is a recursive mutex.
     */
    *recurse = mp->m_count;
    mp->m_count = 0;
    curthread->Dequeue(mp);

    /* Will this happen in real-world ? */
    if (True(mp->m_flags & PthreadMutexFlags::Defered)) {
        defered = 1;
        mp->m_flags &= ~PthreadMutexFlags::Defered;
    } else {
        defered = 0;
    }

    if (defered)  {
        _thr_wake_all(curthread->defer_waiters,
                      curthread->nwaiter_defer);
        curthread->nwaiter_defer = 0;
    }
    return 0;
}

int PS4_SYSV_ABI posix_pthread_mutex_getprioceiling(PthreadMutexT* mutex, int* prioceiling) {
    PthreadMutex* m = *mutex;
    if ((m <= THR_MUTEX_DESTROYED) ||
        (m->m_lock.m_flags & UMUTEX_PRIO_PROTECT) == 0) {
        return POSIX_EINVAL;
    }

    *prioceiling = m->m_lock.m_ceilings[0];
    return 0;
}

int PS4_SYSV_ABI posix_pthread_mutex_setprioceiling(PthreadMutexT* mutex, int ceiling, int *old_ceiling)
{
    Pthread *curthread = _get_curthread();
    PthreadMutex *m, *m1, *m2;
    int ret;

    m = *mutex;
    if ((m <= THR_MUTEX_DESTROYED) ||
        (m->m_lock.m_flags & UMUTEX_PRIO_PROTECT) == 0)
        return (EINVAL);

    ret = __thr_umutex_set_ceiling(&m->m_lock, ceiling, old_ceiling);
    if (ret != 0)
        return (ret);

    if (m->m_owner == curthread) {
        MUTEX_ASSERT_IS_OWNED(m);
        m1 = TAILQ_PREV(m, mutex_queue, m_qe);
        m2 = TAILQ_NEXT(m, m_qe);
        if ((m1 != NULL && m1->m_lock.m_ceilings[0] > (u_int)ceiling) ||
            (m2 != NULL && m2->m_lock.m_ceilings[0] < (u_int)ceiling)) {
            TAILQ_REMOVE(&curthread->pp_mutexq, m, m_qe);
            TAILQ_FOREACH(m2, &curthread->pp_mutexq, m_qe) {
                if (m2->m_lock.m_ceilings[0] > (u_int)ceiling) {
                    TAILQ_INSERT_BEFORE(m2, m, m_qe);
                    return (0);
                }
            }
            TAILQ_INSERT_TAIL(&curthread->pp_mutexq, m, m_qe);
        }
    }
    return (0);
}

int PS4_SYSV_ABI posix_pthread_mutex_getspinloops_np(PthreadMutexT* mutex, int* count) {
    CHECK_AND_INIT_MUTEX
    *count = (*mutex)->m_spinloops;
    return 0;
}

int PS4_SYSV_ABI posix_pthread_mutex_setspinloops_np(PthreadMutexT* mutex, int count) {
    CHECK_AND_INIT_MUTEX
    (*mutex)->m_spinloops = count;
    return 0;
}

int PS4_SYSV_ABI posix_pthread_mutex_getyieldloops_np(PthreadMutexT* mutex, int* count) {
    CHECK_AND_INIT_MUTEX
    *count = (*mutex)->m_yieldloops;
    return 0;
}

int PS4_SYSV_ABI posix_pthread_mutex_setyieldloops_np(PthreadMutexT* mutex, int count) {
    CHECK_AND_INIT_MUTEX
    (*mutex)->m_yieldloops = count;
    return 0;
}

int PS4_SYSV_ABI posix_pthread_mutex_isowned_np(PthreadMutexT* mutex) {
    PthreadMutex* m = *mutex;
    if (m <= THR_MUTEX_DESTROYED) {
        return 0;
    }
    return (m->m_owner == _get_curthread());
}

int _mutex_owned(Pthread* curthread, const PthreadMutex* mp) {
    if (mp <= THR_MUTEX_DESTROYED) [[unlikely]] {
        if (mp == THR_MUTEX_DESTROYED) {
            return POSIX_EINVAL;
        }
        return POSIX_EPERM;
    }
    if (mp->m_owner != curthread) {
        return POSIX_EPERM;
    }
    return 0;
}

} // namespace Libraries::Kernel
