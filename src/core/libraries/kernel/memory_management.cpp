// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <bit>
#include "common/alignment.h"
#include "common/assert.h"
#include "common/logging/log.h"
#include "common/singleton.h"
#include "core/libraries/error_codes.h"
#include "core/libraries/kernel/memory_management.h"
#include "core/virtual_memory.h"
#include "video_core/renderer_vulkan/renderer_vulkan.h"

extern std::unique_ptr<Vulkan::RendererVulkan> renderer;

namespace Libraries::Kernel {

u64 PS4_SYSV_ABI sceKernelGetDirectMemorySize() {
    LOG_WARNING(Kernel_Vmm, "called");
    return SCE_KERNEL_MAIN_DMEM_SIZE;
}

int PS4_SYSV_ABI sceKernelAllocateDirectMemory(s64 searchStart, s64 searchEnd, u64 len,
                                               u64 alignment, int memoryType, s64* physAddrOut) {
    LOG_INFO(Kernel_Vmm,
             "searchStart = {:#x}, searchEnd = {:#x}, len = {:#x}, alignment = {:#x}, memoryType = "
             "{:#x}",
             searchStart, searchEnd, len, alignment, memoryType);

    if (searchStart < 0 || searchEnd <= searchStart) {
        LOG_ERROR(Kernel_Vmm, "Provided address range is invalid!");
        return SCE_KERNEL_ERROR_EINVAL;
    }
    const bool is_in_range = (searchStart < len && searchEnd > len);
    if (len <= 0 || !Common::is16KBAligned(len) || !is_in_range) {
        LOG_ERROR(Kernel_Vmm, "Provided address range is invalid!");
        return SCE_KERNEL_ERROR_EINVAL;
    }
    if ((alignment != 0 || Common::is16KBAligned(alignment)) && !std::has_single_bit(alignment)) {
        LOG_ERROR(Kernel_Vmm, "Alignment value is invalid!");
        return SCE_KERNEL_ERROR_EINVAL;
    }
    if (physAddrOut == nullptr) {
        LOG_ERROR(Kernel_Vmm, "Result physical address pointer is null!");
        return SCE_KERNEL_ERROR_EINVAL;
    }

    u64 physical_addr = 0;
    auto& memory_manager = renderer->MemoryManager();
    if (!memory_manager.Alloc(searchStart, searchEnd, len, alignment, &physical_addr,
                              memoryType)) {
        LOG_CRITICAL(Kernel_Vmm, "Unable to allocate physical memory");
        return SCE_KERNEL_ERROR_EAGAIN;
    }
    *physAddrOut = static_cast<s64>(physical_addr);
    LOG_INFO(Kernel_Vmm, "physAddrOut = {:#x}", physical_addr);
    return SCE_OK;
}

int PS4_SYSV_ABI sceKernelMapDirectMemory(void** addr, u64 len, int prot, int flags,
                                          s64 directMemoryStart, u64 alignment) {
    LOG_INFO(
        Kernel_Vmm,
        "len = {:#x}, prot = {:#x}, flags = {:#x}, directMemoryStart = {:#x}, alignment = {:#x}",
        len, prot, flags, directMemoryStart, alignment);

    if (len == 0 || !Common::is16KBAligned(len)) {
        LOG_ERROR(Kernel_Vmm, "Map size is either zero or not 16KB aligned!");
        return SCE_KERNEL_ERROR_EINVAL;
    }
    if (!Common::is16KBAligned(directMemoryStart)) {
        LOG_ERROR(Kernel_Vmm, "Start address is not 16KB aligned!");
        return SCE_KERNEL_ERROR_EINVAL;
    }
    if (alignment != 0) {
        if ((!std::has_single_bit(alignment) && !Common::is16KBAligned(alignment))) {
            LOG_ERROR(Kernel_Vmm, "Alignment value is invalid!");
            return SCE_KERNEL_ERROR_EINVAL;
        }
    }

    VirtualMemory::MemoryMode cpu_mode = VirtualMemory::MemoryMode::NoAccess;

    switch (prot) {
    case 0x03:
        cpu_mode = VirtualMemory::MemoryMode::ReadWrite;
        break;
    case 0x32:
    case 0x33: // SCE_KERNEL_PROT_CPU_READ|SCE_KERNEL_PROT_CPU_WRITE|SCE_KERNEL_PROT_GPU_READ|SCE_KERNEL_PROT_GPU_ALL
        cpu_mode = VirtualMemory::MemoryMode::ReadWrite;
        break;
    default:
        UNREACHABLE();
    }

    auto& memory_manager = renderer->MemoryManager();

    auto in_addr = reinterpret_cast<u64>(*addr);
    void* out_addr = memory_manager.Map(in_addr, directMemoryStart, len, alignment, prot, cpu_mode);
    ASSERT(out_addr != nullptr);

    LOG_INFO(Kernel_Vmm, "in_addr = {:#x}, out_addr = {}", in_addr, fmt::ptr(out_addr));
    *addr = reinterpret_cast<void*>(out_addr); // return out_addr to first functions parameter

    if (out_addr == 0) {
        return SCE_KERNEL_ERROR_ENOMEM;
    }

    return SCE_OK;
}

int PS4_SYSV_ABI sceKernelQueryMemoryProtection(void* addr, void** start, void** end, u32* prot) {
    auto& memory_manager = renderer->MemoryManager();
    auto* block = memory_manager.FindBlock(reinterpret_cast<VAddr>(addr));
    *prot = SCE_KERNEL_PROT_GPU_READ | SCE_KERNEL_PROT_GPU_WRITE;
    *start = reinterpret_cast<void*>(block->map_virtual_addr);
    *end = reinterpret_cast<void*>(block->map_virtual_addr + block->map_size);
    return SCE_OK;
    //*prot = SCE_KERNEL_PROT_GPU_READ | SCE_KERNEL_PROT_GPU_WRITE;
    //*start = addr;
    //*end = (void*)(uintptr_t(addr) + 0x1000);
    //return SCE_OK;
}

} // namespace Libraries::Kernel
