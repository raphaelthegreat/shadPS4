// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <condition_variable>
#include <mutex>
#include <shared_mutex>
#include <boost/intrusive/list.hpp>

#include "common/enum.h"
#include "core/libraries/kernel/signal.h"

namespace Core::Loader {
class SymbolsResolver;
}

namespace Libraries::Kernel {

struct Pthread;

using ListBaseHook =
    boost::intrusive::list_base_hook<boost::intrusive::link_mode<boost::intrusive::normal_link>>;

enum class PthreadMutexFlags : u32 {
    TypeMask = 0xff,
    Private = 0x100,
    Defered = 0x200,
};
DECLARE_ENUM_FLAG_OPERATORS(PthreadMutexFlags)

enum class PthreadMutexType : u32 {
    ErrorCheck = 1,
    Recursive = 2,
    Normal = 3,
    AdaptiveNp = 4,
    Max
};

enum class PthreadMutexProt : u32 {
    None = 0,
    Inherit = 1,
    Protect = 2,
};

struct PthreadMutex : public ListBaseHook {
    std::timed_mutex m_lock;
    PthreadMutexFlags m_flags;
    Pthread* m_owner;
    int	m_count;
    int	m_spinloops;
    int	m_yieldloops;
    PthreadMutexProt m_protocol;

    PthreadMutexType Type() const noexcept {
        return static_cast<PthreadMutexType>(m_flags & PthreadMutexFlags::TypeMask);
    }
};
using PthreadMutexT = PthreadMutex*;

struct PthreadMutexAttr {
    PthreadMutexType m_type;
    PthreadMutexProt m_protocol;
    int m_ceiling;
};
using PthreadMutexAttrT = PthreadMutexAttr*;

enum class PthreadCondFlags : u32 {
    Private = 1,
    Inited = 2,
    Busy = 4,
};

struct PthreadCond {
    std::condition_variable_any cond;
    u32 has_user_waiters;
    u32 has_kern_waiters;
    u32 flags;
    u32 clock_id;
};
using PthreadCondT = PthreadCond*;

struct PthreadCondAttr {
    int c_pshared;
    int	c_clockid;
};
using PthreadCondAttrT = PthreadCondAttr*;

struct PthreadCleanup {
    struct pthread_cleanup	*prev;
    void PS4_SYSV_ABI (*routine)(void *);
    void* routine_arg;
    int	onheap;
};

struct PthreadAttr {
    int	sched_policy;
    int	sched_inherit;
    int	prio;
    int	suspend;
    int	flags;
    void* stackaddr_attr;
    size_t stacksize_attr;
    size_t guardsize_attr;
    size_t cpusetsize;
};

static constexpr u32 ThrStackInitial = 2_MB;

struct PthreadRwlockAttr {
    int pshared;
};
using PthreadRwlockAttrT = PthreadRwlockAttr*;

struct PthreadRwlock {
    std::shared_mutex lock;
    Pthread* owner;
};
using PthreadRwlockT = PthreadRwlock*;

enum class PthreadState : u32 {
    Running,
    Dead
};

struct PthreadSpecificElem {
    const void* data;
    int	seqno;
};

struct PthreadKey {
    int allocated;
    int seqno;
    void PS4_SYSV_ABI (*destructor)(void *);
};

enum class ThreadFlags : u32 {
    Private = 1,
    NeedSuspend = 2,
    Suspended = 4,
    Detached = 8,
};

enum class ThreadListFlags : u32 {
    GcSafe = 1,
    InTdList = 2,
    InGcList = 4,
};

struct Pthread {
    static constexpr u32 ThrMagic = 0xd09ba115U;

    long tid;
    std::mutex lock;
    u32	cycle;
    int locklevel;
    int	critical_count;
    int	sigblock;
    int	refcount;
    void PS4_SYSV_ABI *(*start_routine)(void *);
    void* arg;
    PthreadAttr	attr;
    bool cancel_enable;
    bool cancel_pending;
    bool cancel_point;
    bool no_cancel;
    bool cancel_async;
    bool cancelling;
    sigset_t sigmask;
    bool unblock_sigcancel;
    bool in_sigsuspend;
    siginfo_t deferred_siginfo;
    sigset_t deferred_sigmask;
    sigaction deferred_sigact;
    bool force_exit;
    PthreadState state;
    int	error;
    Pthread* joiner;
    ThreadFlags flags;
    ThreadListFlags	tlflags;
    boost::intrusive::list<PthreadMutex> mutexq;
    boost::intrusive::list<PthreadMutex> pp_mutexq;
    void* ret;
    PthreadSpecificElem* specific;
    int	specific_data_count;
    int	rdlock_count;
    int	rtld_bits;
    struct tcb* tcb;
    PthreadCleanup* cleanup;
    u32 pad[27];
    u32	magic;
    int	report_events;
    int	event_mask;

    void CriticalEnter() {
        critical_count++;
    }

    void CriticalLeave() {
        critical_count--;
        // _thr_ast(this);
    }

    bool InCritical() const noexcept {
        return locklevel > 0 || critical_count > 0;
    }

    void Enqueue(PthreadMutex* mutex) {
        mutex->m_owner = this;
        mutexq.push_back(*mutex);
    }

    void Dequeue(PthreadMutex* mutex) {
        mutex->m_owner = nullptr;
        mutexq.erase(decltype(mutexq)::s_iterator_to(*mutex));
    }
};
static_assert(offsetof(Pthread, magic) == 0x21c);

int _mutex_cv_lock(PthreadMutex* m, int count);
int _mutex_cv_unlock(PthreadMutex* m, int* count);
int _mutex_cv_attach(PthreadMutex* m, int count);
int _mutex_cv_detach(PthreadMutex* mp, int* recurse);

} // namespace Libraries::Kernel
