// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <condition_variable>
#include <mutex>
#include <semaphore>
#include <boost/intrusive/list.hpp>
#include "common/assert.h"
#include "common/logging/log.h"
#include "core/libraries/kernel/time_management.h"
#include "core/libraries/error_codes.h"
#include "core/libraries/libs.h"

namespace Libraries::Kernel {

using ListBaseHook =
    boost::intrusive::list_base_hook<boost::intrusive::link_mode<boost::intrusive::safe_link>>;

class Semaphore {
public:
    Semaphore(s32 init_count, s32 max_count, std::string_view name, bool is_fifo)
        : name{name}, token_count{init_count}, max_count{max_count}, init_count{init_count},
          is_fifo{is_fifo} {}
    ~Semaphore() {
        ASSERT(wait_list.empty());
    }

    int Wait(bool can_block, s32 need_count, u32* timeout) {
        std::unique_lock lk{mutex};
        if (token_count >= need_count) {
            token_count -= need_count;
            return ORBIS_OK;
        }
        if (!can_block) {
            return ORBIS_KERNEL_ERROR_EBUSY;
        }

        // Create waiting thread object and add it into the list of waiters.
        WaitingThread waiter{need_count, is_fifo};
        AddWaiter(waiter);

        // Perform the wait.
        return waiter.Wait(lk, timeout);
    }

    bool Signal(s32 signal_count) {
        std::scoped_lock lk{mutex};
        if (token_count + signal_count > max_count) {
            return false;
        }
        token_count += signal_count;

        // Wake up threads in order of priority.
        for (auto it = wait_list.begin(); it != wait_list.end();) {
            auto& waiter = *it;
            if (waiter.need_count > token_count) {
                it++;
                continue;
            }
            it = wait_list.erase(it);
            token_count -= waiter.need_count;
            waiter.cv.notify_one();
        }

        return true;
    }

    int Cancel(s32 set_count, s32* num_waiters) {
        std::scoped_lock lk{mutex};
        if (num_waiters) {
            *num_waiters = wait_list.size();
        }
        for (auto& waiter : wait_list) {
            waiter.was_cancled = true;
            waiter.cv.notify_one();
        }
        wait_list.clear();
        token_count = set_count < 0 ? init_count : set_count;
        return ORBIS_OK;
    }

public:
    struct WaitingThread : public ListBaseHook {
        std::condition_variable cv;
        u32 priority;
        s32 need_count;
        bool was_deleted{};
        bool was_cancled{};

        explicit WaitingThread(s32 need_count, bool is_fifo) : need_count{need_count} {
            if (is_fifo) {
                return;
            }
            // Retrieve calling thread priority for sorting into waiting threads list.
            s32 policy;
            sched_param param;
            pthread_getschedparam(pthread_self(), &policy, &param);
            priority = param.sched_priority;
        }

        int GetResult(bool timed_out) {
            if (timed_out) {
                return SCE_KERNEL_ERROR_ETIMEDOUT;
            }
            if (was_deleted) {
                return SCE_KERNEL_ERROR_EACCES;
            }
            if (was_cancled) {
                return SCE_KERNEL_ERROR_ECANCELED;
            }
            return SCE_OK;
        }

        int Wait(std::unique_lock<std::mutex>& lk, u32* timeout) {
            if (!timeout) {
                // Wait indefinitely until we are woken up.
                cv.wait(lk);
                return GetResult(false);
            }
            // Wait until timeout runs out, recording how much remaining time there was.
            const auto start = std::chrono::high_resolution_clock::now();
            const auto status = cv.wait_for(lk, std::chrono::microseconds(*timeout));
            const auto end = std::chrono::high_resolution_clock::now();
            const auto time =
                std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
            if (status == std::cv_status::timeout) {
                *timeout = 0;
            } else {
                *timeout -= time;
            }
            return GetResult(status == std::cv_status::timeout);
        }
    };

    void AddWaiter(WaitingThread& waiter) {
        // Insert at the end of the list for FIFO order.
        if (is_fifo) {
            wait_list.push_back(waiter);
            return;
        }
        // Find the first with priority less then us and insert right before it.
        auto it = wait_list.begin();
        while (it != wait_list.end() && it->priority > waiter.priority) {
            it++;
        }
        wait_list.insert(it, waiter);
    }

    using WaitingThreads =
        boost::intrusive::list<WaitingThread, boost::intrusive::base_hook<ListBaseHook>,
                               boost::intrusive::constant_time_size<false>>;
    WaitingThreads wait_list;
    std::string name;
    std::atomic<s32> token_count;
    std::mutex mutex;
    s32 max_count;
    s32 init_count;
    bool is_fifo;
};

using OrbisKernelSema = Semaphore*;

struct PthreadSemaphore {
    std::string name;
    std::counting_semaphore<std::numeric_limits<s32>::max()> sema;
    std::atomic_int32_t value;

    PthreadSemaphore(u32 value) : sema{value} {}
};

using ScePthreadSem = PthreadSemaphore*;

s32 PS4_SYSV_ABI sceKernelCreateSema(OrbisKernelSema* sem, const char* pName, u32 attr,
                                     s32 initCount, s32 maxCount, const void* pOptParam) {
    if (!pName || attr > 2 || initCount < 0 || maxCount <= 0 || initCount > maxCount) {
        LOG_ERROR(Lib_Kernel, "Semaphore creation parameters are invalid!");
        return ORBIS_KERNEL_ERROR_EINVAL;
    }
    *sem = new Semaphore(initCount, maxCount, pName, attr == 1);
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceKernelWaitSema(OrbisKernelSema sem, s32 needCount, u32* pTimeout) {
    if (!sem) {
        return ORBIS_KERNEL_ERROR_ESRCH;
    }
    return sem->Wait(true, needCount, pTimeout);
}

s32 PS4_SYSV_ABI sceKernelSignalSema(OrbisKernelSema sem, s32 signalCount) {
    if (!sem) {
        return ORBIS_KERNEL_ERROR_ESRCH;
    }
    if (!sem->Signal(signalCount)) {
        return ORBIS_KERNEL_ERROR_EINVAL;
    }
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceKernelPollSema(OrbisKernelSema sem, s32 needCount) {
    if (!sem) {
        return ORBIS_KERNEL_ERROR_ESRCH;
    }
    return sem->Wait(false, needCount, nullptr);
}

s32 PS4_SYSV_ABI sceKernelCancelSema(OrbisKernelSema sem, s32 setCount, s32* pNumWaitThreads) {
    if (!sem) {
        return ORBIS_KERNEL_ERROR_ESRCH;
    }
    return sem->Cancel(setCount, pNumWaitThreads);
}

s32 PS4_SYSV_ABI sceKernelDeleteSema(OrbisKernelSema sem) {
    if (!sem) {
        return SCE_KERNEL_ERROR_ESRCH;
    }
    delete sem;
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI posix_sem_init(ScePthreadSem* sem_ptr, int pshared, u32 value) {
    if (!sem_ptr) {
        return POSIX_EINVAL;
    }
    if (value > std::numeric_limits<s32>::max()) {
        return POSIX_EINVAL;
    }
    *sem_ptr = new PthreadSemaphore{value};
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI posix_sem_wait(ScePthreadSem* sem_ptr) {
    if (!sem_ptr || !*sem_ptr) {
        return POSIX_EINVAL;
    }
    ScePthreadSem sema = *sem_ptr;
    sema->sema.acquire();
    sema->value--;
    return ORBIS_OK;;
}

int PS4_SYSV_ABI posix_sem_trywait(ScePthreadSem* sem_ptr) {
    if (!sem_ptr || !*sem_ptr) {
        return POSIX_EINVAL;
    }
    ScePthreadSem sema = *sem_ptr;
    if (!sema->sema.try_acquire()) {
        return POSIX_EAGAIN;
    }
    sema->value--;
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI posix_sem_timedwait(ScePthreadSem* sem_ptr, const OrbisKernelTimespec* spec) {
    if (!sem_ptr || !*sem_ptr) {
        return POSIX_EINVAL;
    }
    ScePthreadSem sema = *sem_ptr;
    const auto nsecs = std::chrono::nanoseconds(spec->tv_nsec) + std::chrono::seconds(spec->tv_sec);
    if (!sema->sema.try_acquire_for(nsecs)) {
        return POSIX_ETIMEDOUT;
    }
    sema->value--;
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI posix_sem_post(ScePthreadSem* sem_ptr) {
    if (!sem_ptr || !*sem_ptr) {
        return POSIX_EINVAL;
    }
    ScePthreadSem sema = *sem_ptr;
    if (sema->value + 1 == sema->sema.max()) {
        return POSIX_EOVERFLOW;
    }
    sema->sema.release();
    sema->value++;
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI posix_sem_destroy(ScePthreadSem* sem_ptr) {
    if (!sem_ptr || !*sem_ptr) {
        return POSIX_EINVAL;
    }
    delete *sem_ptr;
    *sem_ptr = nullptr;
    return ORBIS_OK;
}

int PS4_SYSV_ABI posix_sem_getvalue(ScePthreadSem* sem_ptr, s32* sval) {
    if (!sem_ptr || !*sem_ptr) {
        return POSIX_EINVAL;
    }
    *sval = (*sem_ptr)->value.load();
    return ORBIS_OK;
}

void SemaphoreSymbolsRegister(Core::Loader::SymbolsResolver* sym) {
    LIB_FUNCTION("188x57JYp0g", "libkernel", 1, "libkernel", 1, 1, sceKernelCreateSema);
    LIB_FUNCTION("Zxa0VhQVTsk", "libkernel", 1, "libkernel", 1, 1, sceKernelWaitSema);
    LIB_FUNCTION("4czppHBiriw", "libkernel", 1, "libkernel", 1, 1, sceKernelSignalSema);
    LIB_FUNCTION("12wOHk8ywb0", "libkernel", 1, "libkernel", 1, 1, sceKernelPollSema);
    LIB_FUNCTION("4DM06U2BNEY", "libkernel", 1, "libkernel", 1, 1, sceKernelCancelSema);
    LIB_FUNCTION("R1Jvn8bSCW8", "libkernel", 1, "libkernel", 1, 1, sceKernelDeleteSema);
    LIB_FUNCTION("pDuPEf3m4fI", "libScePosix", 1, "libkernel", 1, 1, posix_sem_init);
    LIB_FUNCTION("YCV5dGGBcCo", "libScePosix", 1, "libkernel", 1, 1, posix_sem_wait);
    LIB_FUNCTION("WBWzsRifCEA", "libScePosix", 1, "libkernel", 1, 1, posix_sem_trywait);
    LIB_FUNCTION("w5IHyvahg-o", "libScePosix", 1, "libkernel", 1, 1, posix_sem_timedwait);
    LIB_FUNCTION("IKP8typ0QUk", "libScePosix", 1, "libkernel", 1, 1, posix_sem_post);
    LIB_FUNCTION("cDW233RAwWo", "libScePosix", 1, "libkernel", 1, 1, posix_sem_destroy);
    LIB_FUNCTION("Bq+LRV-N6Hk", "libScePosix", 1, "libkernel", 1, 1, posix_sem_getvalue);
}

} // namespace Libraries::Kernel
