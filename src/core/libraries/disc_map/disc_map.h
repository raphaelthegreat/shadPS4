// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/types.h"

namespace Core::Loader {
class SymbolsResolver;
}
namespace Libraries::DiscMap {

int PS4_SYSV_ABI sceDiscMapGetPackageSize();
int PS4_SYSV_ABI sceDiscMapIsRequestOnHDD();
int PS4_SYSV_ABI Func_7C980FFB0AA27E7A(const char* path, s64 param2, s64 param3,
                                       s64* pflags, s64* pret1, s64* pret2);
int PS4_SYSV_ABI Func_8A828CAEE7EDD5E9();
int PS4_SYSV_ABI Func_E7EBCE96E92F91F8();

void RegisterlibSceDiscMap(Core::Loader::SymbolsResolver* sym);
} // namespace Libraries::DiscMap
