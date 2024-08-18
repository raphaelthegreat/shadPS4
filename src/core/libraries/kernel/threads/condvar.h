// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <condition_variable>
#include <string>
#include <cstring>
#include <fmt/format.h>
#include "common/types.h"

namespace Core::Loader {
class SymbolsResolver;
}

namespace Libraries::Kernel {

struct PthreadCondInternal;
struct PthreadCondAttrInternal;

using ScePthreadCond = PthreadCondInternal*;
using ScePthreadCondattr = PthreadCondAttrInternal*;

struct PthreadCondAttrInternal {
    s32 pshared;
    s32 clockid;
};

struct PthreadCondInternal {
    std::string name;
    PthreadCondAttrInternal attr;
    std::condition_variable_any cond;

    PthreadCondInternal(const char* name, const PthreadCondAttrInternal* cond_attr) {
        if (name) {
            this->name = name;
        } else {
            this->name = fmt::format("Mutex-{}", fmt::ptr(this));
        }
        std::memcpy(&attr, cond_attr, sizeof(attr));
    }
};

void RegisterCondvar(Core::Loader::SymbolsResolver* sym);

} // namespace Libraries::Kernel
