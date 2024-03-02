// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/types.h"

namespace Core::Kernel {

enum class MemoryTypes : u32 {
    WriteBackOnion = 0,
    WriteCombOnion = 3,
    WriteBackGarlic = 10,
};

enum class MemoryFlags : u32 {
    MapFixed = 0x0010,
    MapNoOverwrite = 0x0080,
    MapNoCoalesce = 0x400000,
};

enum class MemoryProtection : u32 {
    CpuRead = 0x01,
    CpuReadWrite = 0x02,
    CpuWrite = 0x02,
    GpuRead = 0x10,
    GpuWrite = 0x20,
    GpuReadWRite = 0x30,
};

struct VirtualQueryInfo {
    uintptr_t start;
    uintptr_t end;
    uintptr_t offset;
    int protection;
    int memory_type;
    u32 is_flexible_memory : 1;
    u32 is_direct_memory : 1;
    u32 is_stack : 1;
    u32 is_pooled_memory : 1;
    u32 is_committed : 1;
    char name[32];
};
static_assert(sizeof(VirtualQueryInfo) == 0x48);

u64 PS4_SYSV_ABI sceKernelGetDirectMemorySize();
int PS4_SYSV_ABI sceKernelAllocateDirectMemory(s64 searchStart, s64 searchEnd, u64 len,
                                               u64 alignment, int memoryType, s64* physAddrOut);
int PS4_SYSV_ABI sceKernelMapDirectMemory(void** addr, u64 len, int prot, int flags,
                                          s64 directMemoryStart, u64 alignment);
int PS4_SYSV_ABI sceKernelVirtualQuery(const void* addr, int flags, VirtualQueryInfo* info,
                                       std::size_t infoSize);

} // namespace Core::Kernel
