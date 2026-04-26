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
    constexpr auto mask = ~(VmEntryFlags::InTransition | VmEntryFlags::InTransition2 |
                            VmEntryFlags::NeedsWakeup);
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

VmMap::Tree::iterator VmMap::Insert(Tree::iterator prev, VAddr start, VAddr end, std::shared_ptr<VmObject> object,
                                    u64 offset, MemoryProt prot, MemoryProt max_prot,
                                    VmEntryFlags eflags, std::string_view name) {
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
    if (addr <= entry->start || addr >= entry->end) return entry;

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
    SimplifyEntry(entry);
    if (addr <= entry->start || addr >= entry->end) return entry;

    auto* new_entry = new VmMapEntry{};
    *new_entry = *entry;
    static_cast<SplayTreeNode&>(*new_entry) = SplayTreeNode{};

    new_entry->start = addr;
    new_entry->offset = entry->offset + (addr - entry->start);
    entry->end = addr;
    m_tree.ResizeFree(entry);
    return m_tree.Link(entry, new_entry);
}

s32 VmMap::Delete(VAddr start, VAddr end) {
    m_tree.VerifyIntegrity();
    auto [entry, found] = m_tree.LookupEntry(start);
    if (!found) {
        auto& ent = *entry;
        entry++;
    } else if (entry->start < start) {
        if (entry->IsBlockpool()) {
            return 4;
        }
        ClipStart(entry, start);
    }

    auto& ent = *entry;

    // Make sure no partial blockpool entries are unmapped
    for (auto temp = entry; temp != m_tree.end() && temp->start < end; ++temp) {
        if (end < temp->end && temp->IsBlockpool()) {
            return 4;
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

    auto [entry, found] = m_tree.LookupEntry(start);
    if (!found) {
        entry++;
    } else if (!entry->IsBlockpool() && entry->start < start) {
        ClipStart(entry, start);
    }

    // Validation pass.
    for (auto cur = entry; cur != m_tree.end() && cur->start < end; ++cur) {
        if (set_max && cur->IsBlockpool()) {
            return ORBIS_KERNEL_ERROR_EINVAL;
        }
        MemoryProt eff = cur->max_protection;
        if (True(cur->ext_flags & VmExtFlags::GpuOnly)) {
            eff = static_cast<MemoryProt>(static_cast<u32>(eff) & 0x30);
        }
        if (True(cur->ext_flags & VmExtFlags::Hide)) {
            eff = MemoryProt::NoAccess;
        }
        if ((static_cast<u32>(new_prot) & ~static_cast<u32>(eff)) != 0) {
            return ORBIS_KERNEL_ERROR_EACCES;
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
            if (True(new_prot & MemoryProt::GpuReadWrite) && !dmem.CheckGpuWriteAlias(map, *cur, end)) {
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
            entry->protection = static_cast<MemoryProt>(
                static_cast<u32>(entry->protection) & static_cast<u32>(new_prot));
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

s32 VmMap::ProtectType(DmemManager& dmem, VAddr start, VAddr end, s32 new_mtype, MemoryProt new_prot) {
    if (new_mtype == 10 && True(new_prot & ~(MemoryProt::GpuRead|MemoryProt::CpuRead))) {
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
            return ORBIS_KERNEL_ERROR_EINTR;
        }
        // vm_map_type_protect_blockpool
        return ORBIS_OK;
    }

    while (entry != m_tree.end() && entry->start < end) {
        ++entry;
    }
}

void VmMap::SimplifyEntry(Tree::iterator entry) {
    if (entry->object && entry->object->IsDmem() && m_sdk_version < Common::ElfInfo::FW_20) {
        return;
    }
    if (True(entry->eflags & (VmEntryFlags::InTransition | VmEntryFlags::InTransition2 |
                              VmEntryFlags::IsSubMap))) {
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
            blockpool.SetName(std::max(entry->start, start), std::min(entry->end, end), name.data());
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