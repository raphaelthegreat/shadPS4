#pragma once

#include "core/loader/symbols_resolver.h"

namespace Core::Libraries {

u64 PS4_SYSV_ABI sceKernelReadTsc();
int32_t PS4_SYSV_ABI sceKernelReleaseDirectMemory(off_t start, size_t len);
int PS4_SYSV_ABI sceKernelOpen(const char *path, int flags, /* SceKernelMode*/ u16 mode);
int PS4_SYSV_ABI open(const char *path, int flags, /* SceKernelMode*/ u16 mode);

void LibKernel_Register(Loader::SymbolsResolver* sym);

};  // namespace Core::Libraries
