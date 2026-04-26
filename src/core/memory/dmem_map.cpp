// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma GCC push_options
#pragma GCC optimize ("O0")
#include <algorithm>
#include "common/alignment.h"
#include "common/assert.h"
#include "core/libraries/kernel/orbis_error.h"
#include "core/memory/address_space.h"
#include "core/memory/dmem_map.h"
#include "core/memory/vm_map.h"

namespace Core {

void DmemManager::Init(u64 dmem_size) {
    m_total_size = dmem_size;
    m_sentinel.start = dmem_size;
    m_sentinel.end = 0x1000000000;
    m_sentinel.is_free = true;
    m_sentinel.mtype = DmemMemoryType::Invalid;
    m_tree.Init(&m_header, 0, dmem_size);
    m_tree.Link(m_tree.end(), &m_sentinel);
    m_entry_count = 1;
}

s32 DmemManager::Allocate(PAddr search_start, PAddr search_end, u64 size, u64 alignment,
                          s32 mtype, PAddr* out_addr) {
    if (s64(search_start) < 0 || s64(search_end) < 0 || s64(size) < 1 || 10 < mtype || 1 < std::popcount(alignment)) {
        return ORBIS_KERNEL_ERROR_EINVAL;
    }
    if (s64(alignment) < 0 || !Common::Is16KBAligned(alignment) || !Common::Is16KBAligned(size)) {
        return ORBIS_KERNEL_ERROR_EINVAL;
    }
    if (alignment == 0) {
        alignment = 1;
    }
    search_end = std::min(search_end, DMEM_MAX_ADDRESS);
    if (search_end <= search_start || search_end < size || search_end < (search_start + size)) {
        return ORBIS_KERNEL_ERROR_EAGAIN;
    }

    std::scoped_lock lock{m_mutex};

    auto [prev, addr] = m_tree.FindSpaceAligned(search_start, search_end, size, alignment);
    if (addr == -1) {
        return ORBIS_KERNEL_ERROR_EAGAIN;
    }

    auto* entry = new DmemEntry{};
    entry->start = addr;
    entry->end = addr + size;
    entry->mtype = static_cast<DmemMemoryType>(mtype);
    entry->is_free = false;
    entry->budget_id = 0xff;
    m_tree.Link(prev, entry);
    m_entry_count++;
    *out_addr = addr;
    return ORBIS_OK;
}

s32 DmemManager::Free(PAddr start, u64 size, bool is_checked) {
    if (start < 0 || size < 0 || !Common::Is16KBAligned(start) || !Common::Is16KBAligned(size)) {
        return ORBIS_KERNEL_ERROR_EINVAL;
    }
    if (start >= DMEM_MAX_ADDRESS) {
        return ORBIS_OK;
    }
    size = std::min(DMEM_MAX_ADDRESS - start, size);
    const PAddr end = start + size;
    if (size == 0) {
        return ORBIS_OK;
    }

    std::scoped_lock lock{m_mutex};

    auto [entry, found] = m_tree.LookupEntry(start);

    if (is_checked) {
        for (; entry != m_tree.end() && entry->end < end; ++entry) {
            if (!entry->IsAllocated()) {
                return ORBIS_KERNEL_ERROR_EACCES;
            }
        }
    }

    if (entry->IsAllocated() && start > entry->start && entry->end > start) {
        Split(entry, start);
        ++entry;
    } else if (entry->end <= start) {
        ++entry;
    }

    while (entry->start < end) {
        if (!entry->IsAllocated()) {
            if (entry == m_tree.end()) {
                break;
            }
            ++entry;
        } else {
            if (end < entry->end) {
                Split(entry, end);
            }
            /* Free entry */
            auto next = std::next(entry);
            FreeEntry(entry);
            entry = next;
        }
    }

    return ORBIS_OK;
}

s32 DmemManager::MapDirectMemory(VmMap& map, VAddr* out_addr, u64 length, DmemMemoryType mtype_override,
                                 MemoryProt prot, MemoryMapFlags flags,
                                 PAddr phys_addr, u32 alignment_shift) {
    const PAddr phys_end = phys_addr + length;
    constexpr MemoryMapFlags MapWritableWbGarlic = static_cast<MemoryMapFlags>(0x800000);

    if ((phys_addr >> 36) > 4 || phys_end < phys_addr) {
        return ORBIS_KERNEL_ERROR_EINVAL;
    }

    // If the physical range already has virtual mappings and aliasing mode
    // requires it, check for GPU write conflicts.
    if (True(prot & MemoryProt::GpuReadWrite)) {
        std::scoped_lock lock{m_rmap_mutex};

        const auto prot_mask = True(prot & MemoryProt::GpuWrite) ? MemoryProt::GpuReadWrite : MemoryProt::GpuWrite;
        const s32 start_page = phys_addr >> PAGE_SHIFT;
        const s32 end_page = phys_end >> PAGE_SHIFT;

        for (DmemRmap* rmap = m_rmap_head; rmap; rmap = rmap->next_rmap) {
            if (!rmap->vmspace || rmap->tree.IsEmpty()) continue;

            auto* hit = rmap->tree.FindOverlapping(start_page, end_page);
            for (auto* cur = hit; cur;
                 cur = RmapSplayTree<DmemRmapTraits>::NextOverlap(cur)) {
                // Compute overlapping page range.
                s32 os = std::max(start_page, cur->p_start_idx);
                s32 oe = std::min(end_page, cur->p_end_idx);

                // Check if the existing mapping has GPU write on the overlapping range.
                VAddr alias_vaddr = cur->vaddr + u64(os - cur->p_start_idx) * PAGE_SIZE;
                u64 alias_size = u64(oe - os) * PAGE_SIZE;

                // Look up the vm_map entries at the aliased virtual range.
                auto [other, found] = map.LookupEntry(alias_vaddr);
                if (!found) {
                    ++other;
                }
                while (other != map.GetTree().end() && other->start < alias_vaddr + alias_size) {
                    if (other->IsDmem2() && True(other->protection & prot_mask)) {
                        return ORBIS_KERNEL_ERROR_EBUSY;
                    }
                    ++other;
                }
            }
        }
    }

    MemoryProt max_prot = MemoryProt::All;
    {
        std::scoped_lock lock{m_mutex};

        auto [dentry, found] = m_tree.LookupEntry(phys_addr);
        PAddr cursor = phys_addr;

        while (cursor < phys_end) {
            if (dentry->start > cursor || dentry->end <= cursor || dentry->is_free || !dentry->IsAllocated()) {
                return ORBIS_KERNEL_ERROR_EINVAL;
            }

            const bool wants_write = True(prot & (MemoryProt::CpuWrite | MemoryProt::GpuWrite));
            const auto effective_mtype = (mtype_override != DmemMemoryType::Invalid) ? mtype_override : dentry->mtype;
            if (wants_write && effective_mtype == DmemMemoryType::WbGarlic && False(flags & MapWritableWbGarlic)) {
                return ORBIS_KERNEL_ERROR_EINVAL;
            }

            // TODO: accumulate max_prot from ACL permissions.

            cursor = dentry->end;
            ++dentry;
        }
    }

    VAddr vaddr = *out_addr;
    VAddr end = vaddr + length;
    VmMap::Tree::iterator entry;

    {
        std::scoped_lock lk{map.lock};

        if (True(flags & MemoryMapFlags::Fixed)) {
            if (vaddr == 0 || end < vaddr || end > map.MaxOffset()) {
                return ORBIS_KERNEL_ERROR_EINVAL;
            }
            if (False(flags & MemoryMapFlags::NoOverwrite)) {
                map.Delete(vaddr, end);
            }
        } else {
            u64 align = PAGE_SIZE;
            if (alignment_shift > 13) {
                align = 1ULL << alignment_shift;
            }

            auto [prev, found_addr] = map.FindSpace(vaddr ? vaddr : map.MinOffset(), length);
            if (!found_addr) {
                return ORBIS_KERNEL_ERROR_ENOMEM;
            }

            vaddr = Common::AlignUpPow2(found_addr, align);
            end = vaddr + length;

            auto [check, found] = map.LookupEntry(vaddr);
            if (found && check->start < end) {
                auto [prev2, found_addr2] = map.FindSpace(check->end, length);
                if (!found_addr2) {
                    return ORBIS_KERNEL_ERROR_ENOMEM;
                }
                vaddr = Common::AlignUpPow2(found_addr2, align);
                end = vaddr + length;
            }
        }

        auto [prev, found] = map.LookupEntry(vaddr);

        VmEntryFlags eflags = VmEntryFlags::Dmem2;
        if (True(flags & MemoryMapFlags::NoCoalesce)) {
            eflags |= VmEntryFlags::NoCoalesce;
        }

        entry = map.Insert(prev, vaddr, end, m_dmem_object, phys_addr, prot, max_prot, eflags, "");

        RmapInsert(&map, vaddr, length, phys_addr);
    }

    {
        std::scoped_lock lock{m_mutex};

        auto [dentry, found] = m_tree.LookupEntry(phys_addr);
        PAddr cursor = phys_addr;

        while (cursor < phys_end) {
            const PAddr entry_end = std::min(dentry->end, phys_end);
            if (mtype_override != DmemMemoryType::Invalid && dentry->mtype != mtype_override) {
                if (cursor != dentry->start) {
                    Split(dentry, cursor);
                    ++dentry;
                }
                if (entry_end < dentry->end) {
                    Split(dentry, entry_end);
                }
                dentry->mtype = mtype_override;
            }

            impl.Map(vaddr + (cursor - phys_addr), entry_end - cursor, cursor);

            // GPU mapping.
            if (True(prot & MemoryProt::GpuReadWrite)) {
                // rasterizer->MapMemory();
            }

            cursor = entry_end;
            ++dentry;
        }
    }

    // Simplify on success.
    {
        std::scoped_lock lk{map.lock};
        map.SimplifyEntry(entry);
    }

    *out_addr = vaddr;
    return ORBIS_OK;

}

void DmemManager::RmapInsert(void* vmspace, VAddr vaddr, u64 vsize, PAddr phys_offset) {
    std::scoped_lock lock{m_rmap_mutex};
    DmemRmap* rmap = FindOrCreateRmap(vmspace);

    auto* entry = new DmemRmapEntry{};
    entry->p_start_idx = phys_offset >> PAGE_SHIFT;
    entry->p_end_idx = (phys_offset + vsize) >> PAGE_SHIFT;
    entry->vaddr = vaddr;

    auto* nearest = rmap->tree.Find(entry->p_start_idx, vaddr);
    rmap->tree.Splay(nearest);
    rmap->tree.Insert(entry);
    rmap->Dereference();
}

void DmemManager::RmapRemove(void* vmspace, VAddr vaddr, u64 vsize, PAddr phys_offset) {
    std::scoped_lock lock{m_rmap_mutex};
    DmemRmap* rmap = FindRmap(vmspace);
    if (!rmap) {
        return;
    }

    const s32 page = static_cast<s32>(phys_offset >> PAGE_SHIFT);
    auto* const found = rmap->tree.Find(page, vaddr);
    if (found && found->p_start_idx == page && found->vaddr == vaddr) {
        rmap->tree.Splay(found);
        rmap->tree.Remove(found);
        delete found;
    }
    if (rmap->tree.IsEmpty() && rmap->ref_count.load() <= 1) {
        UnlinkRmap(rmap);
        delete rmap;
    }
}

s32 DmemManager::Query(PAddr addr, bool find_next, PAddr* start_out, PAddr* end_out, s32* mtype_out) {
    std::scoped_lock lock{m_mutex};
    auto [entry, found] = m_tree.LookupEntry(addr);
    if (found && entry->IsAllocated()) {
        *start_out = entry->start;
        *end_out = entry->end;
        *mtype_out = static_cast<s32>(entry->mtype);
        return ORBIS_OK;
    }
    if (!find_next) {
        return ORBIS_KERNEL_ERROR_EACCES;
    }
    auto next = found ? std::next(entry) : entry;
    if (next == m_tree.end() && found) {
        next = std::next(entry);
    }
    while (next != m_tree.end()) {
        if (next->IsAllocated()) {
            *start_out = next->start;
            *end_out = next->end;
            *mtype_out = static_cast<s32>(next->mtype);
            return ORBIS_OK;
        }
        ++next;
    }
    return ORBIS_KERNEL_ERROR_EACCES;
}

s32 DmemManager::QueryAvailable(PAddr search_start, PAddr search_end, u64 alignment,
                                PAddr* phys_addr_out, u64* size_out) {
    /*if (alignment & (alignment - 1)) {
        return ORBIS_KERNEL_ERROR_EINVAL;
    }
    alignment = std::max(alignment, PAGE_SIZE);

    std::scoped_lock lock{m_mutex};

    PAddr astart = std::clamp<s64>(search_start, 0, DMEM_MAX_ADDRESS);
    astart = Common::AlignUpPow2(astart, alignment);
    const PAddr limit = std::clamp<s64>(search_end, 0, DMEM_MAX_ADDRESS);

    PAddr best_addr = 0;
    u64 best_size = 0;

    const auto update_best = [&](PAddr gap_start, PAddr gap_end) {
        PAddr aligned = Common::AlignUpPow2(gap_start, alignment);
        aligned = std::max(aligned, astart);
        gap_end = std::min(gap_end, limit);
        const u64 gap_size = gap_end - aligned;
        if (gap_end > aligned && gap_size > best_size) {
            best_size = gap_size;
            best_addr = aligned;
        }
    };

    // Splay to the search end. Check the gap between entry->end and the end its free space.
    auto [entry, found] = m_tree.LookupEntry(limit);

    if (limit >= entry->end) {
        update_best(entry->end, entry->end + entry->adj_free);
    }

    DmemEntry* left_root = m_tree.Left(entry);
    if (left_root) {
        left_root = m_tree.SubtreeSplay(left_root, astart);
        Tree::SetLeft(entry, left_root);

        if (astart < left_root->start) {
            DmemEntry* prev_of_left = m_tree.Prev(left_root);
            update_best(prev_of_left ? prev_of_left->end : 0, left_root->start);
        }
        if (Tree::AdjFree(left_root) > best_size) {
            update_best(left_root->end, left_root->end + Tree::AdjFree(left_root));
        }

        DmemEntry* stk = nullptr;
        DmemEntry* cursor = left_root;

        while (true) {
            DmemEntry* right_child;
            while ((right_child = m_tree.Right(cursor)) != nullptr &&
                   best_size < Tree::MaxFree(right_child)) {

                // SplayMaxFree the right child.
                right_child = m_tree.SubtreeSplayMaxFree(right_child);
                Tree::SetRight(cursor, right_child);

                // Now walk from right_child downward.
                while (true) {
                    cursor = right_child;

                    // Check this node's gap.
                    if (best_size < Tree::AdjFree(cursor)) {
                        update_best(cursor->end, cursor->end + Tree::AdjFree(cursor));
                    }

                    // Try left child.
                    DmemEntry* lc = m_tree.Left(cursor);
                    if (lc == nullptr || lc->max_free <= best_size) {
                        break;
                    }

                    // Push right child onto intrusive stack if viable.
                    Tree::SetTemp(cursor, stk);
                    stk = cursor;

                    // SplayMaxFree the left child, fix up pointer.
                    lc = m_tree.SubtreeSplayMaxFree(lc);
                    Tree::SetLeft(cursor, lc);
                    right_child = lc;
                }
            }

            // Pop from intrusive stack.
            if (stk == nullptr) {
                break;
            }
            DmemEntry* popped = stk;
            stk = Tree::Temp(stk);
            Tree::SetTemp(popped, nullptr);
            cursor = popped;
        }
    }

    if (best_size == 0) {
        return ORBIS_KERNEL_ERROR_EAGAIN;
    }
    *phys_addr_out = best_addr;
    *size_out = best_size;*/
    return ORBIS_OK;
}

s32 DmemManager::GetDirectMemoryType(PAddr addr, s32* mtype_out, PAddr* start_out, PAddr* end_out) {
    if (s64(addr) < 0) {
        return ORBIS_KERNEL_ERROR_EINVAL;
    }
    std::scoped_lock lock{m_mutex};
    auto [entry, found] = m_tree.LookupEntry(addr);
    if (!found || !entry->IsAllocated()) {
        return ORBIS_KERNEL_ERROR_ENOENT;
    }
    *mtype_out = static_cast<s32>(entry->mtype);
    if (start_out) {
        *start_out = entry->start;
    }
    if (end_out) {
        *end_out = entry->end;
    }
    return ORBIS_OK;
}

s32 DmemManager::SetDirectMemoryType(PAddr addr, u64 size, s32 mtype) {
    std::scoped_lock lock{m_mutex};
    auto [entry, found] = m_tree.LookupEntry(addr);
    if (!found || !entry->IsAllocated()) {
        return ORBIS_KERNEL_ERROR_EINVAL;
    }
    const PAddr end = addr + size;
    if (entry->start < addr) {
        Split(entry, addr);
        ++entry;
    }
    while (entry != m_tree.end() && entry->start < end) {
        if (entry->end > end) {
            Split(entry, end);
        }
        entry->mtype = static_cast<DmemMemoryType>(mtype);
        ++entry;
    }
    return ORBIS_OK;
}

bool DmemManager::IncludesWbGarlicMemory(PAddr addr, u64 size) {
    std::scoped_lock lock{m_mutex};

    const PAddr end = addr + size;
    auto [entry, found] = m_tree.LookupEntry(addr);
    if (!found) {
        ++entry;
    }

    // Walk forward through entries that overlap [addr, end).
    while (entry->start < end) {
        if (entry->mtype == DmemMemoryType::WbGarlic) {
            return true;
        }
        ++entry;
    }
    return false;
}

bool DmemManager::CheckGpuWriteAlias(VmMap& map, VmMapEntry& entry, VAddr protect_end) {
    // Only check if aliasing mode is strict.
    //if (process->dmem_aliasing_mode != 2) {
    //    return false;
    //}

    auto* rmap = FindRmap(&map);
    if (!rmap) {
        return false;
    }

    const PAddr phys_start = entry.offset;
    const PAddr phys_end = entry.offset + std::min(protect_end, entry.end) - entry.start;
    const s32 start_page = static_cast<s32>(phys_start >> PAGE_SHIFT);
    const s32 end_page = static_cast<s32>(phys_end >> PAGE_SHIFT);

    auto* hit = rmap->tree.FindOverlapping(start_page, end_page);

    for (auto* cur = hit; cur; cur = DmemRmap::Tree::NextOverlap(cur)) {
        const s64 our_offset = start_page - (entry.start >> PAGE_SHIFT);
        const s64 their_offset = cur->p_start_idx - (cur->vaddr >> PAGE_SHIFT);
        if (our_offset == their_offset) {
            continue;
        }

        // Compute the overlapping virtual range in the alias.
        const s32 overlap_start = std::max(start_page, cur->p_start_idx);
        const s32 overlap_end = std::min(end_page, cur->p_end_idx);
        const VAddr alias_vaddr = cur->vaddr + (overlap_start - cur->p_start_idx) * PAGE_SIZE;
        const u64 alias_size = (overlap_end - overlap_start) * PAGE_SIZE;

        auto [other, found] = map.LookupEntry(alias_vaddr);
        if (!found) {
            ++other;
        }
        while (other->start < alias_vaddr + alias_size) {
            if (other->IsDmem() && True(other->protection & MemoryProt::GpuWrite)) {
                return true;
            }
            ++other;
        }
    }

    return false;
}

void DmemManager::Split(Tree::iterator entry, PAddr addr) {
    ASSERT(addr > entry->start && addr < entry->end);
    auto* new_entry = new DmemEntry{};
    new_entry->start = addr;
    new_entry->end = entry->end;
    new_entry->mtype = entry->mtype;
    new_entry->budget_id = entry->budget_id;
    new_entry->control_count = entry->control_count;
    new_entry->is_free = entry->is_free;
    entry->end = addr;
    m_tree.Link(entry, new_entry);
    m_entry_count++;
}

void DmemManager::FreeEntry(Tree::iterator entry) {
    UnmapVirtualMappings(entry);

    m_tree.Unlink(entry);
    delete std::addressof(*entry);
    m_entry_count--;
}

void DmemManager::UnmapVirtualMappings(Tree::iterator entry) {
    if (!m_unmap_callback) {
        return;
    }
    std::scoped_lock lock{m_rmap_mutex};

    const s32 start_page = entry->start >> PAGE_SHIFT;
    const s32 end_page = entry->end >> PAGE_SHIFT;

    for (DmemRmap* rmap = m_rmap_head; rmap;) {
        DmemRmap* next_rmap = rmap->next_rmap;
        if (!rmap->vmspace || rmap->tree.IsEmpty()) {
            rmap = next_rmap;
            continue;
        }

        rmap->Reference();
        auto* hit = rmap->tree.FindOverlapping(start_page, end_page);

        // Collect hits before mutating the tree.
        struct Hit {
            VAddr addr;
            u64 size;
            s32 ps;
            s32 pe;
            DmemRmapEntry* entry;
        };
        std::vector<Hit> hits;
        for (auto* c = hit; c; c = DmemRmap::Tree::NextOverlap(c)) {
            s32 os = std::max(start_page, c->p_start_idx);
            s32 oe = std::min(end_page, c->p_end_idx);
            s32 off = os - c->p_start_idx;
            hits.emplace_back(c->vaddr + u64(off) * PAGE_SIZE,
                              u64(oe - os) * PAGE_SIZE,
                              c->p_start_idx, c->p_end_idx, c);
        }

        for (const auto& hit : hits) {
            m_unmap_callback(rmap->vmspace, hit.addr, hit.size);

            if (start_page <= hit.ps && end_page >= hit.pe) {
                rmap->tree.Remove(hit.entry);
                delete hit.entry;
            } else if (start_page <= hit.ps) {
                hit.entry->vaddr += u64(end_page - hit.ps) * PAGE_SIZE;
                hit.entry->p_start_idx = end_page;
            } else if (end_page >= hit.pe) {
                hit.entry->p_end_idx = start_page;
            } else {
                auto* rmap_entry = new DmemRmapEntry{};
                rmap_entry->p_start_idx = end_page;
                rmap_entry->p_end_idx = hit.pe;
                rmap_entry->vaddr = hit.entry->vaddr + u64(end_page - hit.ps) * PAGE_SIZE;

                hit.entry->p_end_idx = start_page;

                auto* near = rmap->tree.Find(rmap_entry->p_start_idx, rmap_entry->vaddr);
                rmap->tree.Splay(near);
                rmap->tree.Insert(rmap_entry);
            }
        }

        if (rmap->Dereference()) {
            UnlinkRmap(rmap);
            delete rmap;
        }
        rmap = next_rmap;
    }
}

DmemRmap* DmemManager::FindRmap(void* vmspace) {
    for (auto* rmap = m_rmap_head; rmap; rmap = rmap->next_rmap) {
        if (rmap->vmspace == vmspace) {
            return rmap;
        }
    }
    return nullptr;
}

DmemRmap* DmemManager::FindOrCreateRmap(void* vmspace) {
    if (auto* rmap = FindRmap(vmspace)) {
        rmap->Reference();
        return rmap;
    }
    auto* rmap = new DmemRmap{};
    rmap->vmspace = vmspace;
    rmap->ref_count.store(2);
    rmap->next_rmap = m_rmap_head;

    m_rmap_head = rmap;
    return rmap;
}

void DmemManager::UnlinkRmap(DmemRmap* rmap) {
    if (m_rmap_head == rmap) {
        m_rmap_head = rmap->next_rmap;
        return;
    }
    for (auto* rmap = m_rmap_head; rmap; rmap = rmap->next_rmap) {
        if (rmap->next_rmap == rmap) {
            rmap->next_rmap = rmap->next_rmap;
            return;
        }
    }
}


} // namespace Core