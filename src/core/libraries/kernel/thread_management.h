// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <vector>
#include <pthread.h>
#include <sched.h>
#include "common/types.h"

namespace Core::Loader {
class SymbolsResolver;
}

namespace Libraries::Kernel {

struct PthreadInternal;
struct PthreadAttrInternal;
struct PthreadMutexInternal;
struct PthreadMutexattrInternal;
struct PthreadCondInternal;
struct PthreadCondAttrInternal;

using SceKernelSchedParam = ::sched_param;
using ScePthread = PthreadInternal*;
using ScePthreadAttr = PthreadAttrInternal*;
using ScePthreadMutex = PthreadMutexInternal*;
using ScePthreadMutexattr = PthreadMutexattrInternal*;
using ScePthreadCond = PthreadCondInternal*;
using ScePthreadCondattr = PthreadCondAttrInternal*;

using pthreadEntryFunc = PS4_SYSV_ABI void* (*)(void*);

struct SceKernelTimespec {
    int64_t tv_sec;
    int64_t tv_nsec;
};

struct PthreadInternal {
    u8 reserved[4096];
    std::string name;
    pthread_t pth;
    ScePthreadAttr attr;
    pthreadEntryFunc entry;
    void* arg;
    std::atomic_bool is_started;
    std::atomic_bool is_detached;
    std::atomic_bool is_almost_done;
    std::atomic_bool is_free;
};

struct PthreadAttrInternal {
    u8 reserved[64];
    u64 affinity;
    size_t guard_size;
    int policy;
    bool detached;
    pthread_attr_t pth_attr;
};

struct PthreadMutexInternal {
    u8 reserved[256];
    std::string name;
    pthread_mutex_t pth_mutex;
};

struct PthreadMutexattrInternal {
    u8 reserved[64];
    pthread_mutexattr_t pth_mutex_attr;
    int pprotocol;
};

struct PthreadCondInternal {
    u8 reserved[256];
    std::string name;
    pthread_cond_t cond;
};

struct PthreadCondAttrInternal {
    u8 reserved[64];
    pthread_condattr_t cond_attr;
};

class PThreadPool {
public:
    ScePthread Create();

private:
    std::vector<ScePthread> m_threads;
    std::mutex m_mutex;
};

class PThreadCxt {
public:
    ScePthreadMutexattr* getDefaultMutexattr() {
        return &m_default_mutexattr;
    }
    void setDefaultMutexattr(ScePthreadMutexattr attr) {
        m_default_mutexattr = attr;
    }
    ScePthreadCondattr* getDefaultCondattr() {
        return &m_default_condattr;
    }
    void setDefaultCondattr(ScePthreadCondattr attr) {
        m_default_condattr = attr;
    }
    ScePthreadAttr* GetDefaultAttr() {
        return &m_default_attr;
    }
    void SetDefaultAttr(ScePthreadAttr attr) {
        m_default_attr = attr;
    }
    PThreadPool* GetPthreadPool() {
        return m_pthread_pool;
    }
    void SetPthreadPool(PThreadPool* pool) {
        m_pthread_pool = pool;
    }

private:
    ScePthreadMutexattr m_default_mutexattr = nullptr;
    ScePthreadCondattr m_default_condattr = nullptr;
    ScePthreadAttr m_default_attr = nullptr;
    PThreadPool* m_pthread_pool = nullptr;
};

void init_pthreads();
void pthreadInitSelfMainThread();

int PS4_SYSV_ABI scePthreadAttrInit(ScePthreadAttr* attr);
int PS4_SYSV_ABI scePthreadAttrSetdetachstate(ScePthreadAttr* attr, int detachstate);
int PS4_SYSV_ABI scePthreadAttrSetinheritsched(ScePthreadAttr* attr, int inheritSched);
int PS4_SYSV_ABI scePthreadAttrSetschedparam(ScePthreadAttr* attr,
                                             const SceKernelSchedParam* param);
int PS4_SYSV_ABI scePthreadAttrSetschedpolicy(ScePthreadAttr* attr, int policy);
ScePthread PS4_SYSV_ABI scePthreadSelf();
int PS4_SYSV_ABI scePthreadAttrSetaffinity(ScePthreadAttr* pattr,
                                           const /*SceKernelCpumask*/ u64 mask);
int PS4_SYSV_ABI scePthreadSetaffinity(ScePthread thread, const /*SceKernelCpumask*/ u64 mask);
int PS4_SYSV_ABI scePthreadCreate(ScePthread* thread, const ScePthreadAttr* attr,
                                  pthreadEntryFunc start_routine, void* arg, const char* name);

/***
 * Mutex calls
 */
int PS4_SYSV_ABI scePthreadMutexInit(ScePthreadMutex* mutex, const ScePthreadMutexattr* attr,
                                     const char* name);
int PS4_SYSV_ABI scePthreadMutexattrInit(ScePthreadMutexattr* attr);
int PS4_SYSV_ABI scePthreadMutexattrSettype(ScePthreadMutexattr* attr, int type);
int PS4_SYSV_ABI scePthreadMutexattrSetprotocol(ScePthreadMutexattr* attr, int protocol);
int PS4_SYSV_ABI scePthreadMutexLock(ScePthreadMutex* mutex);
int PS4_SYSV_ABI scePthreadMutexUnlock(ScePthreadMutex* mutex);
/****
 * Cond calls
 */
int PS4_SYSV_ABI scePthreadCondInit(ScePthreadCond* cond, const ScePthreadCondattr* attr,
                                    const char* name);
int PS4_SYSV_ABI scePthreadCondattrInit(ScePthreadCondattr* attr);
int PS4_SYSV_ABI scePthreadCondBroadcast(ScePthreadCond* cond);
/****
 * Posix calls
 */
int PS4_SYSV_ABI posix_pthread_mutex_init(ScePthreadMutex* mutex, const ScePthreadMutexattr* attr);
int PS4_SYSV_ABI posix_pthread_mutex_lock(ScePthreadMutex* mutex);
int PS4_SYSV_ABI posix_pthread_mutex_unlock(ScePthreadMutex* mutex);
int PS4_SYSV_ABI posix_pthread_cond_broadcast(ScePthreadCond* cond);

void pthreadSymbolsRegister(Core::Loader::SymbolsResolver* sym);
} // namespace Libraries::Kernel
