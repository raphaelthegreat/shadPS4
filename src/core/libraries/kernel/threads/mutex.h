// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <fmt/format.h>
#include "common/types.h"

namespace Core::Loader {
class SymbolsResolver;
}

namespace Libraries::Kernel {

struct PthreadMutexInternal;
struct PthreadMutexattrInternal;

using ScePthreadMutex = PthreadMutexInternal*;
using ScePthreadMutexattr = PthreadMutexattrInternal*;

enum class MutexType : u32 {
    ErrorCheck = 1,
    Recursive = 2,
    Normal = 3,
    Adaptive = 4,
};

struct PthreadMutexattrInternal {
    MutexType type;
    int protocol;
    int ceiling;
};

struct PthreadMutexInternal {
    std::string name;
    PthreadMutexattrInternal attr;
    std::recursive_timed_mutex mutex;
    std::atomic_int32_t lock_count;
    ScePthread owner;

    PthreadMutexInternal(const char* name, const PthreadMutexattrInternal* mutex_attr) {
        if (name) {
            this->name = name;
        } else {
            this->name = fmt::format("Mutex-{}", fmt::ptr(this));
        }
        std::memcpy(&attr, mutex_attr, sizeof(attr));
    }
};

void RegisterMutex(Core::Loader::SymbolsResolver* sym);

} // namespace Libraries::Kernel
