#include "common/debug.h"
#include "common/log.h"
#include "core/loader/elf.h"
#include "core/hle/kernel/objects/physical_memory.h"
#include "core/hle/kernel/cpu_management.h"
#include "core/hle/kernel/event_queues.h"
#include "core/hle/kernel/memory_management.h"
#include "core/hle/libraries/libs.h"
#include "core/hle/libraries/libkernel/libkernel.h"
#include "core/hle/libraries/libKernel/file_system/file_system.h"
#include "core/hle/libraries/libKernel/file_system/posix_file_system.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace Core::Libraries {

static u64 g_stack_chk_guard = 0xDEADBEEF54321ABC; // Dummy return
constexpr bool log_file_fs = true;  // Disable it to disable logging

int32_t PS4_SYSV_ABI sceKernelReleaseDirectMemory(off_t start, size_t len) {
    BREAKPOINT();
    return 0;
}

static PS4_SYSV_ABI void stack_chk_fail() {
    BREAKPOINT();
}

u64 PS4_SYSV_ABI sceKernelReadTsc() {
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return c.QuadPart;
}

int PS4_SYSV_ABI sceKernelMunmap(void* addr, size_t len) {
    BREAKPOINT();
}

int PS4_SYSV_ABI sceKernelOpen(const char* path, int flags, u16 mode) {
    LOG_INFO_IF(log_file_fs, "sceKernelOpen path = {} flags = {:#x} mode = {:#x}\n", path, flags, mode);
    return 0;
}

int PS4_SYSV_ABI open(const char* path, int flags, /* SceKernelMode*/ u16 mode) {
    int result = sceKernelOpen(path, flags, mode);
    if (result < 0) {
        BREAKPOINT();  // posix calls different only for their return values
    }
    return result;
}

void libKernel_Register(Loader::SymbolsResolver* sym) {
    // obj
    LIB_OBJ("f7uOxY9mM1U", "libkernel", 1, "libkernel", 1, 1, &g_stack_chk_guard);
    // memory
    LIB_FUNCTION("rTXw65xmLIA", "libkernel", 1, "libkernel", 1, 1, Kernel::sceKernelAllocateDirectMemory);
    LIB_FUNCTION("pO96TwzOm5E", "libkernel", 1, "libkernel", 1, 1, Kernel::sceKernelGetDirectMemorySize);
    LIB_FUNCTION("L-Q3LEjIbgA", "libkernel", 1, "libkernel", 1, 1, Kernel::sceKernelMapDirectMemory);
    LIB_FUNCTION("MBuItvba6z8", "libkernel", 1, "libkernel", 1, 1, sceKernelReleaseDirectMemory);
    LIB_FUNCTION("cQke9UuBQOk", "libkernel", 1, "libkernel", 1, 1, sceKernelMunmap);
    // equeue
    LIB_FUNCTION("D0OdFMjp46I", "libkernel", 1, "libkernel", 1, 1, Kernel::sceKernelCreateEqueue);
    LIB_FUNCTION("fzyMKs9kim0", "libkernel", 1, "libkernel", 1, 1, Kernel::sceKernelWaitEqueue);
    // misc
    LIB_FUNCTION("WslcK1FQcGI", "libkernel", 1, "libkernel", 1, 1, Kernel::sceKernelIsNeoMode);
    LIB_FUNCTION("Ou3iL1abvng", "libkernel", 1, "libkernel", 1, 1, stack_chk_fail);
    // time
    LIB_FUNCTION("-2IRUCO--PM", "libkernel", 1, "libkernel", 1, 1, sceKernelReadTsc);
    // fs
    LIB_FUNCTION("1G3lF1Gg1k8", "libkernel", 1, "libkernel", 1, 1, sceKernelOpen);
    LIB_FUNCTION("wuCroIGjt2g", "libScePosix", 1, "libkernel", 1, 1, open);
}

};  // namespace HLE::Libs::LibKernel
