// SPDX-FileCopyrightText: Copyright 2025-2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma GCC push_options
#pragma GCC optimize ("O0")
#include "common/alignment.h"
#include "common/assert.h"
#include "common/elf_info.h"
#include "core/emulator_settings.h"
#include "core/file_sys/fs.h"
#include "core/libraries/kernel/memory.h"
#include "core/libraries/kernel/orbis_error.h"
#include "core/libraries/kernel/process.h"
#include "core/memory/kernel.h"
#include "video_core/renderer_vulkan/vk_rasterizer.h"

namespace Core {

MemoryManager::MemoryManager() : blockpool{impl}, dmem{impl}, vm_map{impl, blockpool} {
    ASSERT_MSG(Libraries::Kernel::sceKernelGetCompiledSdkVersion(&sdk_version) == 0,
               "Failed to get compiled SDK version");

    // Construct vma_map using the regions reserved by the address space
    const auto regions = impl.GetUsableRegions();
    const VAddr min_offset = regions.begin()->lower();
    const VAddr max_offset = regions.rbegin()->upper();
    vm_map.Init(min_offset, max_offset, sdk_version);
    flex_pool.Init(ORBIS_KERNEL_TOTAL_MEM, ORBIS_KERNEL_FLEXIBLE_MEMORY_SIZE);
}

MemoryManager::~MemoryManager() = default;

void MemoryManager::SetupMemoryRegions(u64 flexible_size, bool use_extended_mem1,
                                       bool use_extended_mem2) {
    // Calculate actual direct and flexible memory sizes
    const bool is_neo = ::Libraries::Kernel::sceKernelIsNeoMode();
    auto total_size = is_neo ? ORBIS_KERNEL_TOTAL_MEM_PRO : ORBIS_KERNEL_TOTAL_MEM;
    if (EmulatorSettings.IsDevKit()) {
        total_size = is_neo ? ORBIS_KERNEL_TOTAL_MEM_DEV_PRO : ORBIS_KERNEL_TOTAL_MEM_DEV;
    }
    s32 extra_dmem = EmulatorSettings.GetExtraDmemInMBytes();
    if (extra_dmem != 0) {
        LOG_WARNING(Kernel_Vmm,
                    "extraDmemInMbytes is {} MB! Old Direct Size: {:#x} -> New Direct Size: {:#x}",
                    extra_dmem, total_size, total_size + extra_dmem * 1_MB);
        total_size += extra_dmem * 1_MB;
    }
    if (!use_extended_mem1 && is_neo) {
        total_size -= 256_MB;
    }
    if (!use_extended_mem2 && !is_neo) {
        total_size -= 128_MB;
    }

    // Update stored totals
    total_flexible_size = flexible_size - ORBIS_KERNEL_FLEXIBLE_MEMORY_BASE;
    total_direct_size = total_size - flexible_size;

    // Initialize dmem and flexible allocators to match limits
    dmem.Init(total_direct_size);

    LOG_INFO(Kernel_Vmm, "Configured memory regions: flexible size = {:#x}, direct size = {:#x}",
             total_flexible_size, total_direct_size);
}

u64 MemoryManager::ClampRangeSize(VAddr virtual_addr, u64 size) {
    static constexpr u64 MinSizeToClamp = 1_GB;
    // Dont bother with clamping if the size is small so we dont pay a map lookup on every buffer.
    if (size < MinSizeToClamp) {
        return size;
    }

    /*std::shared_lock lk{vm_map.lock};

    VmMapEntry* entry;
    if (!vm_map.LookupEntry(virtual_addr, &entry)) {
        return size;
    }

    u64 clamped_size = vma->second.base + vma->second.size - virtual_addr;
    ++vma;

    // Keep adding to the size while there is contigious virtual address space.
    while (vma != vma_map.end() && vma->second.IsMapped() && clamped_size < size) {
        clamped_size += vma->second.size;
        ++vma;
    }
    clamped_size = std::min(clamped_size, size);

    if (size != clamped_size) {
        LOG_WARNING(Kernel_Vmm, "Clamped requested buffer range addr={:#x}, size={:#x} to {:#x}",
                    virtual_addr, size, clamped_size);
    }
    return clamped_size;*/
    return size;
}

void MemoryManager::SetPrtArea(u32 id, VAddr address, u64 size) {
    PrtArea& area = prt_areas[id];
    if (area.mapped) {
        rasterizer->UnmapMemory(area.start, area.end - area.start);
    }

    area.start = address;
    area.end = address + size;
    area.mapped = true;

    // Pretend the entire PRT area is mapped to avoid GPU tracking errors.
    // The caches will use CopySparseMemory to fetch data which avoids unmapped areas.
    rasterizer->MapMemory(address, size);
}

void MemoryManager::CopySparseMemory(VAddr virtual_addr, u8* dest, u64 size) {
    std::shared_lock lk{vm_map.lock};
    std::memcpy(dest, (void*)virtual_addr, size);
    /*auto [entry, found] = vm_map.LookupEntry(virtual_addr);

    while (entry && size) {
        const u64 copy_size = std::min<u64>(entry->end - virtual_addr, size);
        if (entry->object) {
            std::memcpy(dest, std::bit_cast<const u8*>(virtual_addr), copy_size);
        } else {
            std::memset(dest, 0, copy_size);
        }
        size -= copy_size;
        virtual_addr += copy_size;
        dest += copy_size;
        entry = vm_map.Next(entry);
    }*/
}

s32 MemoryManager::MapDirectMemory(VAddr* out_addr, u64 size, DmemMemoryType mtype,
                                   MemoryProt prot, MemoryMapFlags flags,
                                   PAddr phys_addr, u32 alignment) {
    return dmem.MapDirectMemory(vm_map, out_addr, size, mtype, prot, flags, phys_addr, alignment);
}

s32 MemoryManager::PoolMap(VAddr virtual_addr, u64 size, MemoryProt prot, s32 mtype) {
    std::shared_lock lk{vm_map.lock};

    auto [entry, found] = vm_map.LookupEntry(virtual_addr);
    if (!found || !entry->IsBlockpool() || virtual_addr + size > entry->end) {
        LOG_ERROR(Kernel_Vmm, "Attempting to commit non-pooled memory at {:#x}", virtual_addr);
        return ORBIS_KERNEL_ERROR_EINVAL;
    }

    if (size && !blockpool.Map(*entry, virtual_addr, size, prot, mtype)) {
        return ORBIS_KERNEL_ERROR_ENOMEM;
    }

    return ORBIS_OK;
}

s32 MemoryManager::PoolUnmap(VAddr virtual_addr, u64 size) {
    std::shared_lock lk{vm_map.lock};

    auto [entry, found] = vm_map.LookupEntry(virtual_addr);
    if (!found || !entry->IsBlockpool() || virtual_addr + size > entry->end) {
        LOG_ERROR(Kernel_Vmm, "Attempting to decommit non-pooled memory at {:#x}", virtual_addr);
        return ORBIS_KERNEL_ERROR_EINVAL;
    }

    if (size) {
        blockpool.Unmap(*entry, virtual_addr, size);
    }

    return ORBIS_OK;
}

s32 MemoryManager::MapMemory(VAddr* out_addr, u64 size, MemoryProt prot, MemoryMapFlags flags,
                             s32 fd, u64 offset, std::string_view name) {
    if (size == 0) {
        return ORBIS_OK;
    }

    if (True(prot & MemoryProt::CpuWrite)) {
        // On PS4, read is appended to write mappings.
        prot |= MemoryProt::CpuRead;
    }

    VAddr addr = *out_addr;
    const u64 page_offset = offset & PAGE_MASK;
    size = (size + PAGE_MASK + page_offset) & ~PAGE_MASK;

    // Resolve the address hint.
    const bool is_fixed = True(flags & MemoryMapFlags::Fixed);
    if (is_fixed) {
        addr -= page_offset;
        if ((addr & PAGE_MASK) != 0) {
            return ORBIS_KERNEL_ERROR_EINVAL;
        }
        if (addr < vm_map.MinOffset() || addr + size > vm_map.MaxOffset()) {
            return ORBIS_KERNEL_ERROR_EINVAL;
        }
    } else {
        addr = (addr + PAGE_MASK) & ~PAGE_MASK;
        if (addr == 0) {
            addr = DEFAULT_MAPPING_BASE;
        }
    }

    offset &= ~PAGE_MASK;

    MemoryProt max_prot = MemoryProt::NoAccess;
    std::shared_ptr<VmObject> object = nullptr;
    bool is_anon = True(flags & MemoryMapFlags::Anon);

    if (True(flags & MemoryMapFlags::Void)) {
        prot = MemoryProt::NoAccess;
        max_prot = MemoryProt::NoAccess;
    } else if (True(flags & MemoryMapFlags::Anon)) {
        max_prot = MemoryProt::All;
        object = std::make_shared<VmObject>();
        object->type = VmObjectType::Default;
        if (!flex_pool.Allocate(size, object->anon.backing)) {
            LOG_ERROR(Kernel_Vmm,
                      "Out of flexible memory, available flexible memory = {:#x} requested size = {:#x}",
                      flex_pool.GetAvailableSize(), size);
            return ORBIS_KERNEL_ERROR_EINVAL;
        }
    } else {
        ASSERT(fd != -1);
        auto* table = Common::Singleton<Core::FileSys::HandleTable>::Instance();
        auto* file = table->GetFile(fd);
        object = std::make_shared<VmObject>();
        switch (file->type) {
        case Core::FileSys::FileType::Device:
            max_prot = MemoryProt::All;
            object->type = VmObjectType::Device;
            object->flags = VmObjectFlags::DmemExt;
            break;
        case Core::FileSys::FileType::Regular:
            max_prot = MemoryProt::CpuReadWrite;
            if (False(file->f.GetAccessMode() & Common::FS::FileAccessMode::Write) ||
                False(file->f.GetAccessMode() & Common::FS::FileAccessMode::Append)) {
                // If the file does not have write access, ensure prot does not contain write
                // permissions. On real hardware, these mappings succeed, but the memory cannot be
                // written to.
                max_prot &= ~MemoryProt::CpuWrite;
            }
            object->type = VmObjectType::Vnode;
            object->vnode.host_fd = file->f.GetFileMapping();
            break;
        case Core::FileSys::FileType::Blockpool:
            max_prot = MemoryProt::All;
            object->type = VmObjectType::Blockpool;
            object->blockpool.blocks.resize(Blockpool::ToBlocks(size));
            break;
        default:
            max_prot = MemoryProt::All;
            break;
        }
    }

    VmEntryFlags eflags = VmEntryFlags::None;
    if (True(flags & MemoryMapFlags::Private) && object) {
        eflags |= VmEntryFlags::CopyOnWrite | VmEntryFlags::NeedsCopy;
    }
    if (True(flags & MemoryMapFlags::NoSync)) {
        eflags |= VmEntryFlags::NoSync;
    }
    if (True(flags & MemoryMapFlags::NoCore)) {
        eflags |= VmEntryFlags::NoCoredump;
    }
    if (True(flags & MemoryMapFlags::NoCoalesce)) {
        eflags |= VmEntryFlags::NoCoalesce;
    }
    if (True(flags & MemoryMapFlags::Void)) {
        eflags |= VmEntryFlags::NoFault;
    }

    VmMap::Tree::iterator entry;
    {
        std::scoped_lock lk{vm_map.lock};

        if (is_fixed) {
            if (False(flags & MemoryMapFlags::NoOverwrite)) {
                vm_map.Delete(addr, addr + size);
            } else {
                auto [existing, found] = vm_map.LookupEntry(addr);
                if (found && existing->start < addr + size && existing->end > addr) {
                    return ORBIS_KERNEL_ERROR_ENOMEM;
                }
            }
        } else {
            VmMap::Tree::iterator it;
            VAddr found_addr;
            std::tie(it, found_addr) = vm_map.FindSpace(addr, size);
            if (!found_addr) {
                std::tie(it, found_addr) = vm_map.FindSpace(vm_map.MinOffset(), size);
                if (!found_addr) {
                    return ORBIS_KERNEL_ERROR_ENOMEM;
                }
            }
            addr = found_addr;
        }

        auto [prev, found] = vm_map.LookupEntry(addr);
        entry = vm_map.Insert(prev, addr, addr + size, object, offset, prot, max_prot, eflags, name);

        if (object) {
            switch (object->type) {
            case VmObjectType::Device:
                impl.Map(addr, size, offset, True(prot & MemoryProt::CpuExec));
                break;
            case VmObjectType::Vnode:
                impl.MapFile(addr, size, offset, static_cast<u32>(prot), object->vnode.host_fd);
                break;
            case VmObjectType::Default: {
                u64 map_offset = 0;
                object->ForEachBacking(entry->offset, size, [&](u64 phys_addr, u64 size) {
                    impl.Map(addr + map_offset, size, phys_addr, True(prot & MemoryProt::CpuExec));
                    map_offset += size;
                });
                break;
            }
            default:
                break;
            }

            if (object->IsDmem()) {
                dmem.RmapInsert(&vm_map, addr, size, offset);
            }
        }

        if (False(eflags & VmEntryFlags::NoCoalesce) && False(flags & MemoryMapFlags::Stack)) {
            vm_map.SimplifyEntry(entry);
        }
    }

    if (entry->IsGpuMapping()) {
        rasterizer->MapMemory(addr, size);
    }

    *out_addr = addr;
    return ORBIS_OK;
}

s32 MemoryManager::UnmapMemory(VAddr virtual_addr, u64 size) {
    std::scoped_lock lk{vm_map.lock};
    return vm_map.Delete(virtual_addr, virtual_addr + size);
}

s32 MemoryManager::QueryProtection(VAddr addr, VAddr* out_start, VAddr* out_end, u32* out_prot) {
    addr = Common::AlignDownPow2(addr, PAGE_SIZE);
    if (addr > vm_map.MaxOffset()) {
        return ORBIS_KERNEL_ERROR_EINVAL;
    }

    std::shared_lock lk{vm_map.lock};

    auto [entry, found] = vm_map.LookupEntry(addr);
    if (!found) {
        return ORBIS_KERNEL_ERROR_EACCES;
    }

    VAddr start, end;
    u32 prot;

    if (!entry->IsBlockpool() || sdk_version < Common::ElfInfo::FW_10) {
        start = entry->start;
        end = entry->end;
        prot = static_cast<u32>(entry->max_protection & entry->protection);
    } else {
        ::Libraries::Kernel::OrbisVirtualQueryInfo info;
        blockpool.Query(addr, entry->start, entry->Size(), entry->object->blockpool.blocks.data(), &info);
        start = info.start;
        end = info.end;
        prot = info.protection;
    }

    if (out_start) {
        *out_start = start;
    }
    if (out_end) {
        *out_end = end;
    }
    if (out_prot) {
        *out_prot = prot;
    }

    return ORBIS_OK;
}

s32 MemoryManager::Protect(VAddr start, u64 size, MemoryProt new_prot) {
    start = Common::AlignDownPow2(start, PAGE_SIZE);
    size = Common::AlignUpPow2(size, PAGE_SIZE);
    return vm_map.Protect(dmem, start, start + size, new_prot, 0);
}

s32 MemoryManager::ProtectType(VAddr start, VAddr end, s32 new_mtype, MemoryProt new_prot) {
    if (new_mtype >= 11) {
        return ORBIS_KERNEL_ERROR_EINVAL;
    }
    start = Common::AlignDownPow2(start, PAGE_SIZE);
    end = Common::AlignUpPow2(end, PAGE_SIZE);
    //return vm_map.ProtectType(dmem, start, end, new_mtype, new_prot);
    return ORBIS_OK;
}
#pragma GCC push_options
#pragma GCC optimize ("O0")

s32 MemoryManager::VirtualQuery(VAddr addr, s32 flags,
                                ::Libraries::Kernel::OrbisVirtualQueryInfo* info) {
    std::shared_lock lk{vm_map.lock};

    auto [entry, found] = vm_map.LookupEntry(addr);
    if (!found) {
        if ((flags & 1) == 0 || ++entry == vm_map.GetTree().end()) {
            return ORBIS_KERNEL_ERROR_EACCES;
        }
        addr = entry->start;
    }

    memset(info, 0, sizeof(*info));
    strncpy(info->name, entry->name, sizeof(entry->name));

    if (entry->IsBlockpool()) {
        blockpool.Query(addr, entry->start, entry->Size(), entry->object->blockpool.blocks.data(), info);
        return ORBIS_OK;
    }

    info->start = entry->start;
    info->end = entry->end;
    info->protection = static_cast<s32>(entry->max_protection & entry->protection);
    info->memory_type = ::Libraries::Kernel::ORBIS_KERNEL_WB_ONION;

    if (entry->object) {
        if (entry->IsDmem()) {
            info->is_direct = 1;
            info->is_committed = 1;

            const u64 relofs = entry->offset - entry->start;
            addr = std::max(addr, entry->start);

            s32 memory_type;
            PAddr dmem_start, dmem_end;
            ASSERT(dmem.GetDirectMemoryType(addr + relofs, &memory_type, &dmem_start, &dmem_end) == 0);

            info->start = std::max(dmem_start - relofs, entry->start);
            info->end = std::min(dmem_end - relofs, entry->end);
            info->memory_type = memory_type;
            info->offset = info->start + relofs;
            return ORBIS_OK;
        }
        if (!IsPageableObject(entry->object->type)) {
            ASSERT(entry->object->type != VmObjectType::PhysShm);
            return ORBIS_OK;
        }
    }

    if (entry->max_protection != MemoryProt::NoAccess) {
        info->is_flexible = 1;
        info->is_committed = entry->wired_count > 0;
        info->is_stack = /* True(entry->eflags & (VmEntryFlags::GrowsUp | VmEntryFlags::GrowsDown)); */ 0;
    }

    return ORBIS_OK;
}

void MemoryManager::NameVirtualRange(VAddr virtual_addr, u64 size, std::string_view name) {
    return vm_map.NameRange(virtual_addr, virtual_addr + size, name);
}

void MemoryManager::InvalidateMemory(const VAddr addr, const u64 size) const {
    if (rasterizer) {
        rasterizer->InvalidateMemory(addr, size);
    }
}

} // namespace Core
