// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/logging/log.h"
#include "core/memory/kernel.h"
#include "core/libraries/kernel/memory.h"
#include "core/file_sys/devices/dmem_device.h"

namespace Core::Devices {

std::shared_ptr<BaseDevice> DmemDevice::Create(u32 handle, const char*, s32, u16) {
    return std::make_shared<DmemDevice>(handle);
}

s32 DmemDevice::ioctl(u32 cmd, void* args) {
    LOG_ERROR(Kernel_Fs, "called, cmd = {:#x}", cmd);
    auto* memory = Core::Memory::Instance();
    auto& dmem = memory->GetDmemManager();

    switch (cmd) {
    case GetMemorySize: {
        *reinterpret_cast<u64*>(args) = memory->GetTotalDirectSize();
        break;
    }
    case GetMemoryType: {
        auto* data = reinterpret_cast<GetMemoryTypeArgs*>(args);
        return dmem.GetDirectMemoryType(data->addr, &data->mtype, &data->out_start, &data->out_end);
    }
    case AllocateMemory: {
        auto* data = reinterpret_cast<AllocateMemoryArgs*>(args);
        return dmem.Allocate(data->search_start, data->search_end, data->size, data->alignment,
                             data->memory_type, &data->search_start);
    }
    case ReleaseMemory: {
        auto* data = reinterpret_cast<ReleaseMemoryArgs*>(args);
        return dmem.Free(data->addr, data->size, false);
    }
    case CheckedReleaseMemory: {
        auto* data = reinterpret_cast<ReleaseMemoryArgs*>(args);
        return dmem.Free(data->addr, data->size, true);
    }
    case AvailableMemorySize: {
        auto* data = reinterpret_cast<AvailableMemoryArgs*>(args);
        return dmem.QueryAvailable(data->search_start, data->search_end, data->alignment,
                                   &data->search_start, &data->out_size);
    }
    case MemoryQuery: {
        auto* data = reinterpret_cast<MemoryQueryArgs*>(args);
        auto* info = reinterpret_cast<::Libraries::Kernel::OrbisVirtualQueryInfo*>(data->query_info);
        return dmem.Query(data->addr, data->flags & 1, &info->start, &info->end, &info->memory_type);
    }
    default:
        LOG_WARNING(Kernel_Fs, "Unknown ioctl");
    }

    return 0;
}

} // namespace Core::Devices
