// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/assert.h"
#include "common/elf_info.h"
#include "core/libraries/kernel/orbis_error.h"
#include "core/memory/address_space.h"
#include "core/memory/blockpool.h"
#include "core/memory/dmem_map.h"
#include "core/memory/vm_map.h"
#include "video_core/renderer_vulkan/vk_rasterizer.h"

namespace Core {

static bool CanMerge(VmMap::Tree::iterator a, VmMap::Tree::iterator b) {
    if (a->end != b->start) {
        return false;
    }
    if (a->object != b->object) {
        return false;
    }
    if (a->object && a->offset + a->Size() != b->offset) {
        return false;
    }
    constexpr auto mask =
        ~(VmEntryFlags::InTransition | VmEntryFlags::InTransition2 | VmEntryFlags::NeedsWakeup);
    if ((a->eflags & mask) != (b->eflags & mask)) {
        return false;
    }
    if (a->protection != b->protection || a->max_protection != b->max_protection) {
        return false;
    }
    if (a->inheritance != b->inheritance) {
        return false;
    }
    if (a->wired_count != b->wired_count) {
        return false;
    }
    if (a->ext_flags != b->ext_flags) {
        return false;
    }
    if (std::strncmp(a->name, b->name, sizeof(a->name)) != 0) {
        return false;
    }
    if (True(a->eflags & VmEntryFlags::NoCoalesce) && a->obj_entry_id != b->obj_entry_id) {
        return false;
    }
    return true;
}

void VmMap::Init(VAddr min_offset, VAddr max_offset, u32 sdk_version) {
    m_min_offset = min_offset;
    m_max_offset = max_offset;
    m_sdk_version = sdk_version;
    m_header.start = min_offset;
    m_header.end = max_offset;
    m_tree.Init(&m_header, min_offset, max_offset);
}

VmMap::Tree::iterator VmMap::Insert(Tree::iterator prev, VAddr start, VAddr end,
                                    std::shared_ptr<VmObject> object, u64 offset, MemoryProt prot,
                                    MemoryProt max_prot, VmEntryFlags eflags,
                                    std::string_view name) {
    auto* entry = new VmMapEntry{};
    entry->start = start;
    entry->end = end;
    entry->object = object;
    entry->offset = offset;
    entry->protection = prot;
    entry->max_protection = max_prot;
    entry->eflags = eflags;
    entry->inheritance = VmInherit::Copy;
    entry->obj_entry_id = m_obj_entry_count++;

    if (!name.empty()) {
        const auto len = std::min(name.size(), sizeof(entry->name) - 1);
        std::memcpy(entry->name, name.data(), len);
        entry->name[len] = '\0';
    }

    m_size += entry->Size();
    auto it = m_tree.Link(prev, entry);
    m_tree.VerifyIntegrity();
    return it;
}

VmMap::Tree::iterator VmMap::ClipStart(Tree::iterator entry, VAddr addr) {
    ASSERT(addr > entry->start && addr < entry->end);
    SimplifyEntry(entry);

    auto* new_entry = new VmMapEntry{};
    *new_entry = *entry;
    static_cast<SplayTreeNode&>(*new_entry) = SplayTreeNode{};
    new_entry->end = addr;
    entry->offset += (addr - entry->start);
    entry->start = addr;

    return m_tree.Link(std::prev(entry), new_entry);
}

VmMap::Tree::iterator VmMap::ClipEnd(Tree::iterator entry, VAddr addr) {
    ASSERT(addr > entry->start && addr < entry->end);

    auto* new_entry = new VmMapEntry{};
    *new_entry = *entry;
    static_cast<SplayTreeNode&>(*new_entry) = SplayTreeNode{};

    new_entry->start = addr;
    new_entry->offset = entry->offset + (addr - entry->start);
    entry->end = addr;
    m_tree.ResizeFree(entry);
    return m_tree.Link(entry, new_entry);
}

s32 VmMap::MapMemory(VAddr* out_addr, u64 size, MemoryProt prot, MemoryProt max_prot,
                     MemoryMapFlags flags, std::shared_ptr<VmObject> object, u64 offset,
                     std::string_view name) {
    VAddr addr = *out_addr;

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
        std::scoped_lock lk{lock};

        if (True(flags & MemoryMapFlags::Fixed)) {
            if (False(flags & MemoryMapFlags::NoOverwrite)) {
                Delete(addr, addr + size);
            } else {
                auto [existing, found] = LookupEntry(addr);
                if (found && existing->start < addr + size && existing->end > addr) {
                    return ORBIS_KERNEL_ERROR_ENOMEM;
                }
            }
        } else {
            VmMap::Tree::iterator it;
            VAddr found_addr;
            std::tie(it, found_addr) = FindSpace(addr, size);
            if (!found_addr) {
                std::tie(it, found_addr) = FindSpace(m_min_offset, size);
                if (!found_addr) {
                    return ORBIS_KERNEL_ERROR_ENOMEM;
                }
            }
            addr = found_addr;
        }

        auto [prev, found] = LookupEntry(addr);
        entry = Insert(prev, addr, addr + size, object, offset, prot, max_prot, eflags, name);

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
            case VmObjectType::Self:
                impl.Map(addr, size, -1, True(prot & MemoryProt::CpuExec));
                break;
            default:
                UNREACHABLE_MSG("Unknown vm object type {}", u32(object->type));
            }

            if (object->IsDmem()) {
                dmem.RmapInsert(this, addr, size, offset);
            }
        }

        if (False(eflags & VmEntryFlags::NoCoalesce) && False(flags & MemoryMapFlags::Stack)) {
            SimplifyEntry(entry);
        }
    }

    if (entry->IsGpuMapping()) {
        rasterizer->MapMemory(addr, size);
    }

    *out_addr = addr;
    return ORBIS_OK;
}

s32 VmMap::Delete(VAddr start, VAddr end) {
    m_tree.VerifyIntegrity();
    auto [entry, found] = m_tree.LookupEntry(start);
    if (!found) {
        entry++;
    } else if (entry->start < start) {
        if (entry->IsBlockpool()) {
            return ORBIS_KERNEL_ERROR_EINTR;
        }
        ClipStart(entry, start);
    }

    // Make sure no partial blockpool entries are unmapped
    for (auto temp = entry; temp != m_tree.end() && temp->start < end; ++temp) {
        if (end < temp->end && temp->IsBlockpool()) {
            return ORBIS_KERNEL_ERROR_EINTR;
        }
    }

    while (entry != m_tree.end() && entry->start < end) {
        if (entry->IsBlockpool()) {
            blockpool.NameSplit(entry->start, entry->end);
            blockpool.Unmap(*entry, entry->start, entry->Size());
        } else {
            if (entry->end > end) {
                ClipEnd(entry, end);
            }
            if (entry->IsGpuMapping()) {
                rasterizer->UnmapMemory(entry->start, entry->Size());
            }
            impl.Unmap(entry->start, entry->Size());
        }

        m_size -= entry->Size();
        ASSERT_MSG(entry->prev != nullptr, "unlinking entry with null prev");
        ASSERT_MSG(entry->next != nullptr, "unlinking entry with null next");
        auto next = m_tree.Unlink(entry);
        delete std::addressof(*entry);

        entry = next;
    }

    m_tree.VerifyIntegrity();

    return ORBIS_OK;
}

s32 VmMap::Protect(DmemManager& dmem, VAddr start, VAddr end, MemoryProt new_prot, bool set_max) {
    start = std::max(start, m_min_offset);
    end = std::min(end, m_max_offset);

    if (True(new_prot & MemoryProt::CpuWrite)) {
        new_prot |= MemoryProt::CpuRead;
    }

    auto [entry, found] = m_tree.LookupEntry(start);
    if (!found) {
        entry++;
    } else if (!entry->IsBlockpool() && entry->start < start) {
        ClipStart(entry, start);
    }

    // Validation pass.
    for (auto cur = entry; cur != m_tree.end() && cur->start < end; ++cur) {
        if (set_max && cur->IsBlockpool()) {
            return ORBIS_KERNEL_ERROR_EINTR;
        }
        MemoryProt eff = cur->max_protection;
        if (True(cur->ext_flags & VmExtFlags::GpuOnly)) {
            eff &= MemoryProt::GpuReadWrite;
        }
        if (True(cur->ext_flags & VmExtFlags::Hide)) {
            eff = MemoryProt::NoAccess;
        }
        if ((eff & new_prot) != new_prot) {
            LOG_WARNING(Kernel_Vmm, "Protection request exceeds max_protection, this succeeds on console for some reason");
            // return ORBIS_KERNEL_ERROR_ENOENT;
        }
        if (cur->IsDmem()) {
            if (True(new_prot & (MemoryProt::GpuWrite | MemoryProt::CpuWrite))) {
                const PAddr entry_start = std::max(start, cur->start);
                const PAddr entry_end = std::min(end, cur->end);
                if (entry_start <= entry_end && entry_end - entry_start != 0) {
                    if (dmem.IncludesWbGarlicMemory(entry_start - cur->start + cur->offset,
                                                    entry_end - entry_start)) {
                        return ORBIS_KERNEL_ERROR_EACCES;
                    }
                }
            }
            VmMap& map = *this;
            if (True(new_prot & MemoryProt::GpuReadWrite) &&
                !dmem.CheckGpuWriteAlias(map, *cur, end)) {
                return ORBIS_KERNEL_ERROR_EACCES;
            }
        }
    }

    // Apply pass.
    while (entry != m_tree.end() && entry->start < end) {
        if (entry->IsBlockpool()) {
            PAddr entry_start = std::max(start, entry->start);
            entry_start = Common::AlignUpPow2(entry_start, Blockpool::BLOCK_SIZE);
            PAddr entry_end = std::min(end, entry->end);
            entry_end = Common::AlignDownPow2(entry_end, Blockpool::BLOCK_SIZE);
            if (entry_start < entry_end) {
                const u64 vm_start = entry->start - entry->offset;
                /*ret1 = vm_map_protect_blockpool
                    (map2,obj,vm_start,(uint)(b_start - vm_start >> 0x10),
                     (int)((b___end & 0xffffffffffff0000) - vm_start >> 0x10),
                     (uint)local_68);
                if (ret1 != 0) {
                    if (ret1 == 13) {
                        ret1 = 2;
                    } else {
                        ret1 = 4;
                    }
                    goto _exit_unlock;
                }*/
            }
            ++entry;
            continue;
        }
        if (entry->end > end) {
            ClipEnd(entry, end);
        }
        MemoryProt old = entry->protection;
        if (set_max) {
            entry->max_protection = new_prot;
            entry->protection = static_cast<MemoryProt>(static_cast<u32>(entry->protection) &
                                                        static_cast<u32>(new_prot));
        } else {
            entry->protection = new_prot;
        }

        if (auto prot = entry->protection; prot != old) {
            Core::MemoryPermission perms{};
            if (True(prot & (MemoryProt::CpuRead | MemoryProt::GpuRead))) {
                perms |= Core::MemoryPermission::Read;
            }
            if (True(prot & MemoryProt::CpuWrite | MemoryProt::GpuWrite)) {
                perms |= Core::MemoryPermission::Write;
            }
            if (True(prot & MemoryProt::CpuExec)) {
                perms |= Core::MemoryPermission::Execute;
            }
            impl.Protect(entry->start, entry->Size(), perms);
        }

        SimplifyEntry(entry);
        ++entry;
    }
    m_tree.VerifyIntegrity();
    return 0;
}

s32 VmMap::ProtectType(DmemManager& dmem, VAddr start, VAddr end, DmemMemoryType new_mtype,
                       MemoryProt new_prot) {
    if (new_mtype == DmemMemoryType::WbGarlic && True(new_prot & ~(MemoryProt::GpuRead | MemoryProt::CpuRead))) {
        return ORBIS_KERNEL_ERROR_EACCES;
    }

    std::scoped_lock lk{lock};

    start = std::max(start, m_min_offset);
    end = std::min(end, m_max_offset);

    auto [entry, found] = m_tree.LookupEntry(start);
    if (!found) {
        ++entry;
    }

    if (entry != m_tree.end() && entry->IsBlockpool()) {
        // _sx_downgrade
        if (!Common::Is64KBAligned(start) || !Common::Is64KBAligned(end) || end > entry->end) {
            return ORBIS_KERNEL_ERROR_EINVAL;
        }
        // vm_map_type_protect_blockpool
        return ORBIS_OK;
    }

    auto& ent = *entry;

    // Validation pass.
    auto last_entry = m_tree.end();
    for (auto cur = entry; cur != m_tree.end() && cur->start < end; ++cur) {
        if (True(cur->eflags & VmEntryFlags::IsSubMap)) {
            return ORBIS_KERNEL_ERROR_EINVAL;
        }
        if (True(cur->ext_flags & (VmExtFlags::GpuOnly | VmExtFlags::Hide))) {
            return ORBIS_KERNEL_ERROR_EACCES;
        }
        if ((static_cast<u32>(new_prot) & ~static_cast<u32>(cur->max_protection)) != 0) {
            return ORBIS_KERNEL_ERROR_EACCES;
        }
        if (!cur->object || !cur->IsDmem()) {
            return ORBIS_KERNEL_ERROR_ENOTSUP;
        }

        // Rmap alias check.
        const VAddr vs = std::max(start, cur->start);
        const VAddr ve = std::min(end, cur->end);
        const PAddr ps = cur->offset + (vs - cur->start);
        const PAddr pe = cur->offset + (ve - cur->start);
        if (dmem.CheckRmapAlias(ps, pe, vs, ve)) {
            return ORBIS_KERNEL_ERROR_EBUSY;
        }

        last_entry = cur;
    }

    if (entry == m_tree.end()) {
        return ORBIS_OK;
    }

    if (entry->start < start) {
        ClipStart(entry, start);
    }
    if (last_entry != m_tree.end() && last_entry->end > end) {
        ClipEnd(last_entry, end);
    }

    for (auto cur = entry; cur != m_tree.end() && cur->start < end; ) {
        const MemoryProt old_prot = cur->protection;
        cur->protection = new_prot;

        const bool had_gpu = True(old_prot & MemoryProt::GpuReadWrite);
        const bool wants_gpu = True(new_prot & MemoryProt::GpuReadWrite);
        if (rasterizer && had_gpu != wants_gpu) {
            if (wants_gpu) {
                rasterizer->MapMemory(cur->start, cur->Size());
            } else {
                rasterizer->UnmapMemory(cur->start, cur->Size());
            }
        }

        if (old_prot != new_prot) {
            //impl.Protect(cur->start, cur->Size(), ToHostPerm(new_prot));
        }
        dmem.UpdateMtype(cur->offset, cur->offset + cur->Size(), new_mtype);

        SimplifyEntry(cur);
        ++cur;
    }

    return ORBIS_OK;
}

s32 VmMap::Wire(VAddr start, VAddr end, VmMapWireFlags flags) {
    std::scoped_lock lk{lock};

    start = std::max(start, m_min_offset);
    end = std::min(end, m_max_offset);
    if (end <= start) {
        return ORBIS_OK;
    }

    const auto prot =
        True(flags & VmMapWireFlags::Write) ? MemoryProt::CpuWrite : MemoryProt::NoAccess;
    auto [entry, found] = m_tree.LookupEntry(start);
    if (!found) {
        if (False(flags & VmMapWireFlags::HolesOk)) {
            return ORBIS_KERNEL_ERROR_EINVAL;
        }
        ++entry;
    }

    auto first_entry = entry;

    while (entry != m_tree.end() && entry->start < end) {
        if (entry->start < start) {
            ClipStart(entry, start);
        }
        if (entry->end > end) {
            ClipEnd(entry, end);
        }

        if (entry->protection == MemoryProt::NoAccess || (entry->protection & prot) != prot) {
            if (False(flags & VmMapWireFlags::HolesOk)) {
                auto next = std::next(entry);
                if (next != m_tree.end() && next->start < end && next->start > entry->end) {
                    return ORBIS_KERNEL_ERROR_EINVAL;
                }
            }
            ++entry;
            continue;
        }

        if (entry->wired_count == 0) {
            const bool needs_budget =
                !entry->object || (False(entry->object->flags & VmObjectFlags::WireBudget) &&
                                   entry->object->IsPageable());
            if (needs_budget && entry->budget_ptype > BudgetPtype::Invalid) {
                if (budget.ReserveWire(entry->budget_ptype, entry->Size())) {
                    entry->wired_count = -1;
                    UnwireRange(first_entry, entry->end, flags);
                    return ORBIS_KERNEL_ERROR_ENOMEM;
                }
                entry->eflags |= VmEntryFlags::InBudget;
            }
            entry->wired_count = 1;
        } else {
            entry->wired_count++;
        }

        if (False(flags & VmMapWireFlags::HolesOk)) {
            auto next = std::next(entry);
            if (next != m_tree.end() && entry->end < end && next->start > entry->end) {
                UnwireRange(first_entry, entry->end, flags);
                return ORBIS_KERNEL_ERROR_EINVAL;
            }
        }

        ++entry;
    }

    for (auto it = first_entry; it != m_tree.end() && it->start < end;) {
        SimplifyEntry(it);
        ++it;
    }

    return ORBIS_OK;
}

void VmMap::UnwireRange(Tree::iterator first_entry, VAddr upto_addr, VmMapWireFlags flags) {
    for (auto it = first_entry; it != m_tree.end() && it->start < upto_addr; ++it) {
        if (it->wired_count == -1) {
            it->wired_count = 0;
        } else if (it->wired_count == 1) {
            it->wired_count = 0;
            if (True(it->eflags & VmEntryFlags::InBudget)) {
                budget.ReleaseWire(it->budget_ptype, it->Size());
                it->eflags &= ~VmEntryFlags::InBudget;
            }
        } else if (it->wired_count > 1) {
            it->wired_count--;
        }
    }
}

void VmMap::SimplifyEntry(Tree::iterator entry) {
    if (entry->object && entry->object->IsDmem() && m_sdk_version < Common::ElfInfo::FW_20) {
        return;
    }
    if (True(entry->eflags &
             (VmEntryFlags::InTransition | VmEntryFlags::InTransition2 | VmEntryFlags::IsSubMap))) {
        return;
    }
    if (entry->IsBlockpool()) {
        return;
    }

    auto prev = std::prev(entry);
    if (prev != m_tree.end() && CanMerge(prev, entry)) {
        m_tree.Unlink(prev);
        entry->start = prev->start;
        entry->offset = prev->offset;
        if (auto new_prev = std::prev(entry); new_prev != m_tree.end()) {
            m_tree.ResizeFree(new_prev);
        }
        delete std::addressof(*prev);
        m_tree.VerifyIntegrity();
    }
    auto next = std::next(entry);
    if (next != m_tree.end() && CanMerge(entry, next)) {
        m_tree.Unlink(next);
        entry->end = next->end;
        m_tree.ResizeFree(entry);
        delete std::addressof(*next);
        m_tree.VerifyIntegrity();
    }
}

void VmMap::NameRange(VAddr start, VAddr end, std::string_view name) {
    start = std::max(start, m_min_offset);
    end = std::min(end, m_max_offset);

    if (end < start) {
        start = end;
    }

    auto [entry, found] = m_tree.LookupEntry(start);
    auto origin = entry;
    if (!found) {
        ++entry;
        origin = entry;
    } else if (!entry->IsBlockpool() && entry->start < start) {
        ClipStart(entry, start);
    }

    while (entry != m_tree.end() && entry->start < end) {
        if (entry->IsBlockpool()) {
            blockpool.SetName(std::max(entry->start, start), std::min(entry->end, end),
                              name.data());
        } else {
            if (entry->end > end) {
                ClipEnd(entry, end);
            }
            if (name.empty()) {
                entry->name[0] = '\0';
                return;
            }
            auto len = std::min(name.size(), sizeof(entry->name) - 1);
            std::memcpy(entry->name, name.data(), len);
            entry->name[len] = '\0';
            SimplifyEntry(m_sdk_version >= Common::ElfInfo::FW_70 ? entry : origin);
        }
        ++entry;
    }
}

} // namespace Core