// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <mutex>
#include <vector>
#include "common/types.h"
#include "core/virtual_memory.h"
#include "video_core/renderer_vulkan/vk_common.h"

namespace Vulkan {
class Instance;
}

namespace VideoCore {

struct AllocatedBlock {
    PAddr start_addr;
    u64 size;
    int memoryType;
    u64 map_virtual_addr;
    u64 map_size;
    int prot;
    VirtualMemory::MemoryMode cpu_mode;
    vk::UniqueDeviceMemory backing_memory;
    vk::UniqueBuffer buffer;
    int fd;

    bool Contains(PAddr addr, u64 range) const {
        return addr >= start_addr && (addr + range) <= start_addr + size;
    }

    bool ContainsVAddr(VAddr addr) const {
        return addr >= map_virtual_addr && addr < map_virtual_addr + map_size;
    }
};

class MemoryManager {
public:
    explicit MemoryManager(const Vulkan::Instance& instance);
    ~MemoryManager() = default;

    bool Alloc(u64 searchStart, u64 searchEnd, u64 len, u64 alignment, u64* physAddrOut,
               int memoryType);
    void* Map(u64 virtual_addr, u64 phys_addr, u64 len, u64 alignment, int prot,
              VirtualMemory::MemoryMode cpu_mode);

    AllocatedBlock* FindBlock(VAddr addr);

    std::pair<vk::Buffer, u64> GetBufferForRange(VAddr addr);

private:
    const Vulkan::Instance& instance;
    std::vector<AllocatedBlock> m_allocatedBlocks;
    std::mutex m_mutex;
    u64 hint_start{};
};

} // namespace VideoCore
