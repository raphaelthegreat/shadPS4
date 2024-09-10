// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <cstring>
#include "core/libraries/error_codes.h"
#include "core/libraries/kernel/threads/threads.h"

namespace Libraries::Kernel {

static std::mutex CondStaticLock;

#define CV_PSHARED(cvp)	(((cvp)->__flags & USYNC_PROCESS_SHARED) != 0)

#define	THR_COND_INITIALIZER ((PthreadCond*)NULL)
#define	THR_COND_DESTROYED ((PthreadCond *)1)

static constexpr PthreadCondAttr PhreadCondattrDefault = {
    .c_pshared = PTHREAD_PROCESS_PRIVATE,
    .c_clockid = CLOCK_REALTIME
};

static int cond_init(PthreadCondT* cond, const PthreadCondAttrT* cond_attr) {
    PthreadCond* cvp = (PthreadCond*)std::calloc(1, sizeof(PthreadCond));
    if (cvp == nullptr) {
        return POSIX_ENOMEM;
    }
    std::construct_at(cvp);
    if (cond_attr == nullptr || *cond_attr == nullptr) {
        cvp->clock_id = CLOCK_REALTIME;
    } else {
        if ((*cond_attr)->c_pshared) {
            cvp->flags |= USYNC_PROCESS_SHARED;
        }
        cvp->clock_id = (*cond_attr)->c_clockid;
    }
    *cond = cvp;
    return 0;
}

static int init_static(Pthread *thread, PthreadCondT* cond) {
    std::scoped_lock lk{CondStaticLock};
    if (*cond == NULL)
        return cond_init(cond, NULL);
    return 0;
}

#define CHECK_AND_INIT_COND							\
if (PthreadCond* cvp = *cond; cvp <= THR_COND_DESTROYED) [[unlikely]] {		\
        if (cvp == THR_COND_INITIALIZER) {				\
            int ret;						\
            ret = init_static(_get_curthread(), cond);		\
            if (ret)						\
            return (ret);					\
    } else if (cvp == THR_COND_DESTROYED) {				\
            return POSIX_EINVAL;					\
    }								\
}

int PS4_SYSV_ABI posix_pthread_cond_init(PthreadCondT* cond, const PthreadCondAttrT* cond_attr) {
    *cond = nullptr;
    return cond_init(cond, cond_attr);
}

int PS4_SYSV_ABI posix_pthread_cond_destroy(PthreadCondT* cond) {
    PthreadCond* cvp = *cond;
    if (cvp == THR_COND_INITIALIZER) {
        return 0;
    }
    if (cvp == THR_COND_DESTROYED) {
        return POSIX_EINVAL;
    }
    cvp = *cond;
    *cond = THR_COND_DESTROYED;
    free(cvp);
    return 0;
}

/*
 * Cancellation behaivor:
 *   Thread may be canceled at start, if thread is canceled, it means it
 *   did not get a wakeup from pthread_cond_signal(), otherwise, it is
 *   not canceled.
 *   Thread cancellation never cause wakeup from pthread_cond_signal()
 *   to be lost.
 */
static int cond_wait_kernel(PthreadCond* cvp, PthreadMutex* mp,
                            const timespec *abstime, int cancel) {
    Pthread* curthread = _get_curthread();
    int	recurse;
    int	error2 = 0;

    int error = _mutex_cv_detach(mp, &recurse);
    if (error != 0) {
        return error;
    }

    if (cancel) {
        _thr_cancel_enter2(curthread, 0);
        error = _thr_ucond_wait((struct ucond *)&cvp->__has_kern_waiters,
                                (struct umutex *)&mp->m_lock, abstime,
                                CVWAIT_ABSTIME|CVWAIT_CLOCKID);
        _thr_cancel_leave(curthread, 0);
    } else {
        error = _thr_ucond_wait((struct ucond *)&cvp->__has_kern_waiters,
                                (struct umutex *)&mp->m_lock, abstime,
                                CVWAIT_ABSTIME|CVWAIT_CLOCKID);
    }

    /*
     * Note that PP mutex and ROBUST mutex may return
     * interesting error codes.
     */
    if (error == 0) {
        error2 = _mutex_cv_lock(mp, recurse);
    } else if (error == EINTR || error == ETIMEDOUT) {
        error2 = _mutex_cv_lock(mp, recurse);
        if (error2 == 0 && cancel)
            _thr_testcancel(curthread);
        if (error == EINTR)
            error = 0;
    } else {
        /* We know that it didn't unlock the mutex. */
        error2 = _mutex_cv_attach(mp, recurse);
        if (error2 == 0 && cancel)
            _thr_testcancel(curthread);
    }
    return (error2 != 0 ? error2 : error);
}

/*
 * Thread waits in userland queue whenever possible, when thread
 * is signaled or broadcasted, it is removed from the queue, and
 * is saved in curthread's defer_waiters[] buffer, but won't be
 * woken up until mutex is unlocked.
 */
static int cond_wait_user(PthreadCond* cvp, PthreadMutex* mp,
                          const timespec* abstime)
{
    Pthread	*curthread = _get_curthread();
    struct sleepqueue *sq;
    int	recurse;
    int	error;

    if (curthread->wchan != NULL)
        PANIC("thread was already on queue.");

    _thr_testcancel(curthread);

    _sleepq_lock(cvp);
    /*
     * set __has_user_waiters before unlocking mutex, this allows
     * us to check it without locking in pthread_cond_signal().
     */
    cvp->__has_user_waiters = 1;
    curthread->will_sleep = 1;
    _mutex_cv_unlock(mp, &recurse);
    curthread->mutex_obj = mp;
    _sleepq_add(cvp, curthread);
    for(;;) {
        _thr_clear_wake(curthread);
        _sleepq_unlock(cvp);

        _thr_cancel_enter2(curthread, 0);
        error = _thr_sleep(curthread, cvp->__clock_id, abstime);
        _thr_cancel_leave(curthread, 0);

        _sleepq_lock(cvp);
        if (curthread->wchan == NULL) {
            error = 0;
            break;
        } else if (SHOULD_CANCEL(curthread)) {
            sq = _sleepq_lookup(cvp);
            cvp->__has_user_waiters =
                _sleepq_remove(sq, curthread);
            _sleepq_unlock(cvp);
            curthread->mutex_obj = NULL;
            _mutex_cv_lock(mp, recurse);
            if (!curthread->InCritical())
                _pthread_exit(PTHREAD_CANCELED);
            else /* this should not happen */
                return 0;
        } else if (error == ETIMEDOUT) {
            sq = _sleepq_lookup(cvp);
            cvp->__has_user_waiters =
                _sleepq_remove(sq, curthread);
            break;
        }
    }
    _sleepq_unlock(cvp);
    curthread->mutex_obj = NULL;
    _mutex_cv_lock(mp, recurse);
    return (error);
}

static int
cond_wait_common(PthreadCondT *cond, PthreadMutexT *mutex,
                 const struct timespec *abstime)
{
    Pthread	*curthread = _get_curthread();
    PthreadCond *cvp;
    PthreadMutex *mp;
    int	error;

    CHECK_AND_INIT_COND

            mp = *mutex;

    if ((error = _mutex_owned(curthread, mp)) != 0)
        return (error);

    if (curthread->attr.sched_policy != SCHED_OTHER ||
        (mp->m_lock.m_flags & (UMUTEX_PRIO_PROTECT|UMUTEX_PRIO_INHERIT|
                               USYNC_PROCESS_SHARED)) != 0 ||
        (cvp->__flags & USYNC_PROCESS_SHARED) != 0)
        return cond_wait_kernel(cvp, mp, abstime);
    else
        return cond_wait_user(cvp, mp, abstime);
}

int
__pthread_cond_wait(PthreadCondT *cond, PthreadMutexT *mutex)
{

    return (cond_wait_common(cond, mutex, NULL));
}

int PS4_SYSV_ABI posix_pthread_cond_timedwait(PthreadCondT *cond, PthreadMutexT *mutex,
                                              const timespec *abstime) {

    if (abstime == NULL || abstime->tv_sec < 0 || abstime->tv_nsec < 0 ||
        abstime->tv_nsec >= 1000000000)
        return (EINVAL);

    return (cond_wait_common(cond, mutex, abstime));
}

int PS4_SYSV_ABI posix_pthread_cond_signal(PthreadCondT* cond) {
    PthreadCond *cvp;
    CHECK_AND_INIT_COND
    cvp->cond.notify_one();
    return 0;
}

int PS4_SYSV_ABI posix_pthread_cond_broadcast(PthreadCondT* cond) {
    PthreadCond *cvp;
    CHECK_AND_INIT_COND
    cvp->cond.notify_all();
    return 0;
}

int PS4_SYSV_ABI posix_pthread_condattr_init(PthreadCondAttrT* attr) {
    PthreadCondAttr* pattr = (PthreadCondAttr*)std::malloc(sizeof(PthreadCondAttr));
    if (pattr == nullptr) {
        return POSIX_ENOMEM;
    }
    std::memcpy(pattr, &PhreadCondattrDefault, sizeof(PthreadCondAttr));
    *attr = pattr;
    return 0;
}

int PS4_SYSV_ABI posix_pthread_condattr_destroy(PthreadCondAttrT* attr) {
    if (attr == nullptr || *attr == nullptr) {
        return POSIX_EINVAL;
    }
    free(*attr);
    *attr = nullptr;
    return 0;
}

int PS4_SYSV_ABI posix_pthread_condattr_getclock(const PthreadCondAttrT* attr, clockid_t *clock_id) {
    if (attr == nullptr || *attr == nullptr) {
        return POSIX_EINVAL;
    }
    *clock_id = (*attr)->c_clockid;
    return 0;
}

int PS4_SYSV_ABI posix_pthread_condattr_setclock(PthreadCondAttrT* attr, clockid_t clock_id) {
    if (attr == nullptr || *attr == nullptr) {
        return POSIX_EINVAL;
    }
    if (clock_id != CLOCK_REALTIME &&
        clock_id != CLOCK_VIRTUAL &&
        clock_id != CLOCK_PROF &&
        clock_id != CLOCK_MONOTONIC) {
        return  POSIX_EINVAL;
    }
    (*attr)->c_clockid = clock_id;
    return 0;
}

int PS4_SYSV_ABI posix_pthread_condattr_getpshared(const PthreadCondAttrT* attr, int *pshared) {
    if (attr == nullptr || *attr == nullptr) {
        return POSIX_EINVAL;
    }
    *pshared = PTHREAD_PROCESS_PRIVATE;
    return 0;
}

int PS4_SYSV_ABI posix_pthread_condattr_setpshared(PthreadCondAttrT* attr, int pshared) {
    if (attr == nullptr || *attr == nullptr) {
        return POSIX_EINVAL;
    }
    if  (pshared != PTHREAD_PROCESS_PRIVATE) {
        return POSIX_EINVAL;
    }
    return 0;
}

} // namespace Libraries::Kernel
