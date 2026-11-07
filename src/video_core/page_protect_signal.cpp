// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <thread>

#include "common/assert.h"
#include "common/debug.h"
#include "common/signal_context.h"
#include "core/memory.h"
#include "core/signals.h"
#include "video_core/gpu_memory.h"
#include "video_core/renderer_vulkan/vk_rasterizer.h"

namespace VideoCore {

namespace {

struct SignalProtector final : PageProtector {
    explicit SignalProtector(Vulkan::Rasterizer* rasterizer_, GpuMemory& gpu_memory_) {
        rasterizer = rasterizer_;
        gpu_memory = &gpu_memory_;

        // Should be called first.
        constexpr auto priority = std::numeric_limits<u32>::min();
        Core::Signals::Instance()->RegisterAccessViolationHandler(GuestFaultSignalHandler,
                                                                  priority);
    }

    void Protect(VAddr address, size_t size, Core::MemoryPermission perms) override {
        RENDERER_TRACE;
        auto* memory = Core::Memory::Instance();
        auto& impl = memory->GetAddressSpace();
        ASSERT_MSG(perms != Core::MemoryPermission::Write,
                   "Attempted to protect region as write-only which is not a valid permission");
        impl.Protect(address, size, perms);
    }

    static bool GuestFaultSignalHandler(void* context, void* fault_address) {
        const auto addr = reinterpret_cast<VAddr>(fault_address);
        const auto size = std::min<u64>(8, GetNextPageAddr(addr) - addr);
        const auto is_gpu_thread =
            std::this_thread::get_id() == rasterizer->GetGpuCommandProcessorThread();
        if (Common::IsWriteError(context)) {
            return rasterizer->InvalidateMemory(addr, size, is_gpu_thread);
        } else {
            return rasterizer->ReadMemory(addr, size, is_gpu_thread);
        }
        return false;
    }
};

} // Anonymous namespace

std::unique_ptr<PageProtector> CreateSignalPageProtector(Vulkan::Rasterizer* rasterizer,
                                                         GpuMemory& gpu_memory) {
    return std::make_unique<SignalProtector>(rasterizer, gpu_memory);
}

} // namespace VideoCore
