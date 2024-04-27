// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/alignment.h"
#include "common/assert.h"
#include "common/error.h"
#include "video_core/memory_manager.h"
#include "video_core/renderer_vulkan/vk_instance.h"

#ifdef _WIN64
#include <windows.h>
#else
#include <sys/mman.h>
#endif

namespace VideoCore {

static u32 FindMemoryType(const vk::PhysicalDeviceMemoryProperties& properties,
                          vk::MemoryPropertyFlags wanted) {
    for (uint32_t i = 0; i < properties.memoryTypeCount; ++i) {
        const auto flags = properties.memoryTypes[i].propertyFlags;
        if ((flags & wanted) == wanted) {
            return i;
        }
    }
    UNREACHABLE_MSG("Unable to find suitable memory type!\n");
    return std::numeric_limits<u32>::max();
}

MemoryManager::MemoryManager(const Vulkan::Instance& instance_)
    : instance{instance_}, hint_start{SYSTEM_MANAGED_MIN} {}

bool MemoryManager::Alloc(u64 searchStart, u64 searchEnd, u64 len, u64 alignment, u64* physAddrOut,
                          int memoryType) {
    std::scoped_lock lock{m_mutex};
    u64 find_free_pos = 0;

    // Iterate through allocated blocked and find the next free position
    for (const auto& block : m_allocatedBlocks) {
        const u64 end = block.start_addr + block.size;
        if (end > find_free_pos) {
            find_free_pos = end;
        }
    }

    // Align free position
    find_free_pos = Common::alignUp(find_free_pos, alignment);

    // If the new position is between searchStart - searchEnd , allocate a new block
    if (find_free_pos >= searchStart && find_free_pos + len <= searchEnd) {
        auto& block = m_allocatedBlocks.emplace_back();
        block.size = len;
        block.start_addr = find_free_pos;
        block.memoryType = memoryType;
        block.map_size = 0;
        block.map_virtual_addr = 0;
        block.prot = 0;
        block.cpu_mode = VirtualMemory::MemoryMode::NoAccess;

        // Create buffer
        const vk::ExternalMemoryBufferCreateInfo external_buffer_ci = {
            .handleTypes = vk::ExternalMemoryHandleTypeFlagBits::eOpaqueFd,
        };
        const vk::BufferCreateInfo buffer_ci = {
            .pNext = &external_buffer_ci,
            .size = len,
            .usage = vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eIndexBuffer |
                     vk::BufferUsageFlagBits::eUniformBuffer | vk::BufferUsageFlagBits::eStorageBuffer,
        };
        const vk::Device device = instance.GetDevice();
        block.buffer = device.createBufferUnique(buffer_ci);

        // Allocate backing vulkan memory on the host. We need that memory to be host visible
        // and coherent because the guest will write directly here.
        const auto properties = instance.GetPhysicalDevice().getMemoryProperties();
        const vk::ExportMemoryAllocateInfo export_info = {
            .handleTypes = vk::ExternalMemoryHandleTypeFlagBits::eOpaqueFd,
        };
        const vk::MemoryAllocateInfo allocate_info = {
            .pNext = &export_info,
            .allocationSize = len,
            .memoryTypeIndex =
                FindMemoryType(properties, vk::MemoryPropertyFlagBits::eHostVisible |
                                               vk::MemoryPropertyFlagBits::eHostCoherent |
                                               vk::MemoryPropertyFlagBits::eHostCached),
        };
        block.backing_memory = device.allocateMemoryUnique(allocate_info);

        // Retrieve file descriptor from memory so we can mmap it later
        const vk::MemoryGetFdInfoKHR fd_info = {
            .memory = *block.backing_memory,
            .handleType = vk::ExternalMemoryHandleTypeFlagBits::eOpaqueFd,
        };
        block.fd = device.getMemoryFdKHR(fd_info);

        device.bindBufferMemory(*block.buffer, *block.backing_memory, 0);

        *physAddrOut = find_free_pos;
        return true;
    }

    return false;
}

void* MemoryManager::Map(u64 virtual_addr, u64 phys_addr, u64 len, u64 alignment, int prot,
                         VirtualMemory::MemoryMode cpu_mode) {
    std::scoped_lock lock{m_mutex};

    // Manually specify hint address when it is not given by the guest.
    // Many subsystems depend on the fact that virtual addresses reside on the user area.
    if (virtual_addr == 0) {
        virtual_addr = hint_start;
        hint_start += len;
    }

    virtual_addr = Common::alignUp(virtual_addr, alignment);
    ASSERT(virtual_addr % alignment == 0);

    for (auto& block : m_allocatedBlocks) {
        if (!block.Contains(phys_addr, len)) {
            continue;
        }
        if (block.map_virtual_addr != 0 || block.map_size != 0) {
            return nullptr;
        }

        // Update block attributes
        block.map_virtual_addr = virtual_addr;
        block.map_size = len;
        block.prot = prot;
        block.cpu_mode = cpu_mode;

        // Map the block of memory.
        const off_t offset = phys_addr - block.start_addr;
        const int prot = VirtualMemory::convertMemoryMode(cpu_mode);
        void* hint_address = reinterpret_cast<void*>(virtual_addr);
        void* ptr = mmap(hint_address, len, prot, MAP_SHARED, block.fd, offset);
        fmt::print("hint_address = {} ptr = {}\n", fmt::ptr(hint_address), fmt::ptr(ptr));
        fmt::print("{}\n", Common::GetLastErrorMsg());
        std::fflush(stdout);
        ASSERT(ptr != MAP_FAILED && ptr == hint_address);
        return ptr;
    }

    return nullptr;
}

AllocatedBlock* MemoryManager::FindBlock(VAddr addr) {
    for (auto& block : m_allocatedBlocks) {
        if (block.ContainsVAddr(addr)) {
            return &block;
        }
    }
    UNREACHABLE();
    return nullptr;
}

std::pair<vk::Buffer, u64> MemoryManager::GetBufferForRange(VAddr addr) {
    for (auto& block : m_allocatedBlocks) {
        if (!block.ContainsVAddr(addr)) {
            continue;
        }
        return std::make_pair(*block.buffer, addr - block.map_virtual_addr);
    }

    UNREACHABLE();
    return {};
}

} // namespace VideoCore
