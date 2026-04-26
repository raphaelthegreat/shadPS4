// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>

#include "common/types.h"
#include "core/memory/splay_tree.h"
#include "core/memory/rmap_splay_tree.h"

namespace Core {

class MemoryManager;

enum class DmemMemoryType : s32 {
    Invalid = -1,
    WbOnion = 0,
    WbOnionNonVolatile = 1,
    WcGarlicVolatile = 2,
    WcGarlic = 3,
    WtOnionVolatile = 4,
    WtOnionNonVolatile = 5,
    WpOnionVolatile = 6,
    WpOnionNonVolatile = 7,
    UcGarlicVolatile = 8,
    UcGarlicNonVolatile = 9,
    WbGarlic = 10,
};

constexpr bool IsWriteBackType(DmemMemoryType mtype) {
    return mtype == DmemMemoryType::WbOnion || mtype == DmemMemoryType::WbOnionNonVolatile ||
           mtype == DmemMemoryType::WtOnionVolatile || mtype == DmemMemoryType::WtOnionNonVolatile ||
           mtype == DmemMemoryType::WpOnionVolatile || mtype == DmemMemoryType::WpOnionNonVolatile ||
           mtype == DmemMemoryType::WbGarlic;
}

struct DmemRmapEntry : RmapNode {};

struct DmemRmapTraits {
    using EntryType = DmemRmapEntry;

    static RmapNode* GetNode(DmemRmapEntry* entry) {
        return static_cast<RmapNode*>(entry);
    }
    static DmemRmapEntry* FromNode(RmapNode* node) {
        return static_cast<DmemRmapEntry*>(node);
    }
};

struct DmemRmap {
    using Tree = RmapSplayTree<DmemRmapTraits>;

    DmemRmap* next_rmap{};
    void* vmspace{};
    Tree tree;
    std::atomic<s32> ref_count{1};
    std::atomic<s32> wait_count{0};

    void Reference() {
        ref_count.fetch_add(1, std::memory_order_relaxed);
    }
    bool Dereference() {
        return ref_count.fetch_sub(1, std::memory_order_acq_rel) == 1;
    }
};

struct DmemEntry : SplayTreeNode {
    PAddr start{};
    PAddr end{};
    DmemMemoryType mtype{-1};
    u8 budget_id{0xFF};
    u8 control_count{};
    bool is_free{true};

    u64 Size() const {
        return end - start;
    }
    bool IsAllocated() const {
        return mtype >= DmemMemoryType::WbOnion;
    }
};

struct DmemSplayTraits {
    using EntryType = DmemEntry;
    static SplayTreeNode* GetNode(DmemEntry* entry) {
        return static_cast<SplayTreeNode*>(entry);
    }
    static DmemEntry* FromNode(SplayTreeNode* node) {
        return static_cast<DmemEntry*>(node);
    }
    static u64 GetStart(const DmemEntry* entry) {
        return entry->start;
    }
    static u64 GetEnd(const DmemEntry* entry) {
        return entry->end;
    }
};

struct VmMapEntry;
struct VmObject;
class VmMap;

enum class MemoryProt : u32;
enum class MemoryMapFlags : u32;

class AddressSpace;

using VirtualUnmapCallback = std::function<void(void* vmspace, VAddr vaddr, u64 size)>;

class DmemManager {
public:
    static constexpr PAddr DMEM_MAX_ADDRESS = 0x5000000000ULL;
    static constexpr u64 PAGE_SIZE = 0x4000;
    static constexpr u32 PAGE_SHIFT = 14;

    using Tree = SplayTree<DmemSplayTraits>;

    DmemManager(AddressSpace& impl_) : impl{impl_} {}
    ~DmemManager() = default;

    DmemManager(const DmemManager&) = delete;
    DmemManager& operator=(const DmemManager&) = delete;

    void SetUnmapCallback(VirtualUnmapCallback cb) {
        m_unmap_callback = std::move(cb);
    }

    u64 GetTotalSize() const {
        return m_total_size;
    }

    void Init(u64 dmem_size);

    s32 Allocate(PAddr search_start, PAddr search_end, u64 size, u64 alignment,
                 s32 mtype, PAddr* out_addr);

    s32 Free(PAddr start, u64 size, bool is_checked);

    s32 MapDirectMemory(VmMap& map, VAddr* out_addr, u64 size, DmemMemoryType mtype,
                        MemoryProt prot, MemoryMapFlags flags,
                        PAddr phys_addr, u32 alignment_shift);

    void RmapInsert(void* vmspace, VAddr vaddr, u64 vsize, PAddr phys_offset);

    void RmapRemove(void* vmspace, VAddr vaddr, u64 vsize, PAddr phys_offset);

    s32 Query(PAddr addr, bool find_next, PAddr* start_out, PAddr* end_out, s32* mtype_out);

    s32 QueryAvailable(PAddr search_start, PAddr search_end, u64 alignment,
                       PAddr* phys_addr_out, u64* size_out);

    s32 GetDirectMemoryType(PAddr addr, s32* mtype_out, PAddr* start_out, PAddr* end_out);

    s32 SetDirectMemoryType(PAddr addr, u64 size, s32 mtype);

    bool IncludesWbGarlicMemory(PAddr addr, u64 size);

    bool CheckGpuWriteAlias(VmMap& map, VmMapEntry& entry, VAddr protect_end);

private:
    void Split(Tree::iterator entry, PAddr addr);

    void FreeEntry(Tree::iterator entry);

    void UnmapVirtualMappings(Tree::iterator entry);

    DmemRmap* FindRmap(void* vmspace);

    DmemRmap* FindOrCreateRmap(void* vmspace);

    void UnlinkRmap(DmemRmap* rmap);

private:
    AddressSpace& impl;

    Tree m_tree;
    DmemEntry m_header;
    DmemEntry m_sentinel;

    std::mutex m_mutex;
    std::mutex m_rmap_mutex;

    DmemRmap* m_rmap_head{};
    std::shared_ptr<VmObject> m_dmem_object;
    VirtualUnmapCallback m_unmap_callback;

    u64 m_total_size{};
    u32 m_entry_count{};
};

} // namespace Core