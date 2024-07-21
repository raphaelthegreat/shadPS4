// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstddef>
#include <utility>
#include "common/enum.h"
#include "common/types.h"
#include "video_core/renderer_vulkan/vk_common.h"

namespace Vulkan {
class Instance;
class Scheduler;
} // namespace Vulkan

VK_DEFINE_HANDLE(VmaAllocation)
VK_DEFINE_HANDLE(VmaAllocator)

namespace VideoCore {

struct UniqueBuffer {
    explicit UniqueBuffer(vk::Device device, VmaAllocator allocator);
    ~UniqueBuffer();

    UniqueBuffer(const UniqueBuffer&) = delete;
    UniqueBuffer& operator=(const UniqueBuffer&) = delete;

    UniqueBuffer(UniqueBuffer&& other)
        : buffer{std::exchange(other.buffer, VK_NULL_HANDLE)},
          allocator{std::exchange(other.allocator, VK_NULL_HANDLE)},
          allocation{std::exchange(other.allocation, VK_NULL_HANDLE)} {}
    UniqueBuffer& operator=(UniqueBuffer&& other) {
        buffer = std::exchange(other.buffer, VK_NULL_HANDLE);
        allocator = std::exchange(other.allocator, VK_NULL_HANDLE);
        allocation = std::exchange(other.allocation, VK_NULL_HANDLE);
        return *this;
    }

    void Create(const vk::BufferCreateInfo& image_ci);

    operator vk::Buffer() const {
        return buffer;
    }

private:
    vk::Device device;
    VmaAllocator allocator;
    VmaAllocation allocation;
    vk::Buffer buffer{};
};

class Buffer {
public:
    explicit Buffer(const Vulkan::Instance& instance, VAddr cpu_addr_, u64 size_bytes_);

    Buffer& operator=(const Buffer&) = delete;
    Buffer(const Buffer&) = delete;

    Buffer& operator=(Buffer&&) = default;
    Buffer(Buffer&&) = default;

    /// Increases the likeliness of this being a stream buffer
    void IncreaseStreamScore(int score) noexcept {
        stream_score += score;
    }

    /// Returns the likeliness of this being a stream buffer
    [[nodiscard]] int StreamScore() const noexcept {
        return stream_score;
    }

    /// Returns true when vaddr -> vaddr+size is fully contained in the buffer
    [[nodiscard]] bool IsInBounds(VAddr addr, u64 size) const noexcept {
        return addr >= cpu_addr && addr + size <= cpu_addr + SizeBytes();
    }

    /// Returns the base CPU address of the buffer
    [[nodiscard]] VAddr CpuAddr() const noexcept {
        return cpu_addr;
    }

    /// Returns the offset relative to the given CPU address
    [[nodiscard]] u32 Offset(VAddr other_cpu_addr) const noexcept {
        return static_cast<u32>(other_cpu_addr - cpu_addr);
    }

    size_t SizeBytes() const {
        return size_bytes;
    }

public:
    VAddr cpu_addr = 0;
    bool is_picked{};
    int stream_score = 0;
    size_t lru_id = SIZE_MAX;
    size_t size_bytes = 0;

    const Vulkan::Instance* instance{};
    UniqueBuffer buffer;
};

} // namespace VideoCore
