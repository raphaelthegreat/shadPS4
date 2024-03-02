// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <pthread.h>
#include "common/assert.h"
#include "common/thread.h"
#include "core/libraries/error_codes.h"
#include "core/libraries/kernel/time_management.h"
#include "core/libraries/videoout/driver.h"

#include "video_core/renderer_vulkan/renderer_vulkan.h"

extern Frontend::WindowSDL* g_window;

namespace Core::Libraries::VideoOut {

constexpr static bool Is32BppPixelFormat(PixelFormat format) {
    switch (format) {
    case PixelFormat::A8R8G8B8Srgb:
    case PixelFormat::A8B8G8R8Srgb:
    case PixelFormat::A2R10G10B10:
    case PixelFormat::A2R10G10B10Srgb:
    case PixelFormat::A2R10G10B10Bt2020Pq:
        return true;
    default:
        return false;
    }
}

constexpr u32 PixelFormatBpp(PixelFormat pixel_format) {
    switch (pixel_format) {
    case PixelFormat::A16R16G16B16Float:
        return 8;
    default:
        return 4;
    }
}

VideoOutDriver::VideoOutDriver(u32 width, u32 height) {
    main_port.resolution.fullWidth = width;
    main_port.resolution.fullHeight = height;
    main_port.resolution.paneWidth = width;
    main_port.resolution.paneHeight = height;

    renderer = std::make_unique<Vulkan::RendererVulkan>(*g_window);
}

VideoOutDriver::~VideoOutDriver() = default;

int VideoOutDriver::Open(const ServiceThreadParams* params) {
    std::scoped_lock lock{mutex};

    if (main_port.is_open) {
        return SCE_VIDEO_OUT_ERROR_RESOURCE_BUSY;
    }

    int handle = 1;
    main_port.is_open = true;

    present_thread =
        std::jthread([this, params](std::stop_token token) { PresentThread(token, params); });

    return handle;
}

void VideoOutDriver::Close(s32 handle) {
    std::scoped_lock lock{mutex};

    main_port.is_open = false;
    main_port.flip_rate = 0;
    ASSERT(main_port.flip_events.empty());
}

VideoOutPort* VideoOutDriver::GetPort(int handle) {
    if (handle != 1) [[unlikely]] {
        return nullptr;
    }
    return &main_port;
}

int VideoOutDriver::RegisterBuffers(VideoOutPort* port, s32 startIndex, void* const* addresses,
                                    s32 bufferNum, const BufferAttribute* attribute) {
    const s32 group_index = port->FindFreeGroup();
    if (group_index >= MaxDisplayBufferGroups) {
        return SCE_VIDEO_OUT_ERROR_NO_EMPTY_SLOT;
    }

    if (startIndex + bufferNum > MaxDisplayBuffers || startIndex > MaxDisplayBuffers ||
        bufferNum > MaxDisplayBuffers) {
        LOG_ERROR(Lib_VideoOut,
                  "Attempted to register too many buffers startIndex = {}, bufferNum = {}",
                  startIndex, bufferNum);
        return SCE_VIDEO_OUT_ERROR_INVALID_VALUE;
    }

    const s32 end_index = startIndex + bufferNum;
    if (bufferNum > 0 &&
        std::any_of(port->buffer_slots.begin() + startIndex, port->buffer_slots.begin() + end_index,
                    [](auto& buffer) { return buffer.group_index != -1; })) {
        return SCE_VIDEO_OUT_ERROR_SLOT_OCCUPIED;
    }

    if (attribute->reserved0 != 0 || attribute->reserved1 != 0) {
        LOG_ERROR(Lib_VideoOut, "Invalid reserved memebers");
        return SCE_VIDEO_OUT_ERROR_INVALID_VALUE;
    }
    if (attribute->aspect_ratio != 0) {
        LOG_ERROR(Lib_VideoOut, "Invalid aspect ratio = {}", attribute->aspect_ratio);
        return SCE_VIDEO_OUT_ERROR_INVALID_ASPECT_RATIO;
    }
    if (attribute->width > attribute->pitch_in_pixel) {
        LOG_ERROR(Lib_VideoOut, "Buffer width {} is larger than pitch {}", attribute->width,
                  attribute->pitch_in_pixel);
        return SCE_VIDEO_OUT_ERROR_INVALID_PITCH;
    }
    if (attribute->tiling_mode < TilingMode::Tile || attribute->tiling_mode > TilingMode::Linear) {
        LOG_ERROR(Lib_VideoOut, "Invalid tilingMode = {}",
                  static_cast<u32>(attribute->tiling_mode));
        return SCE_VIDEO_OUT_ERROR_INVALID_TILING_MODE;
    }

    LOG_INFO(Lib_VideoOut,
             "startIndex = {}, bufferNum = {}, pixelFormat = {}, aspectRatio = {}, "
             "tilingMode = {}, width = {}, height = {}, pitchInPixel = {}, option = {:#x}",
             startIndex, bufferNum, GetPixelFormatString(attribute->pixel_format),
             attribute->aspect_ratio, static_cast<u32>(attribute->tiling_mode), attribute->width,
             attribute->height, attribute->pitch_in_pixel, attribute->option);

    auto& group = port->groups[group_index];
    std::memcpy(&group.attrib, attribute, sizeof(BufferAttribute));
    group.size_in_bytes =
        attribute->height * attribute->pitch_in_pixel * PixelFormatBpp(attribute->pixel_format);
    group.is_occupied = true;

    for (u32 i = 0; i < bufferNum; i++) {
        const uintptr_t address = reinterpret_cast<uintptr_t>(addresses[i]);
        port->buffer_slots[startIndex + i] = VideoOutBuffer{
            .group_index = group_index,
            .address_left = address,
            .address_right = 0,
        };

        LOG_INFO(Lib_VideoOut, "buffers[{}] = {:#x}", i + startIndex, address);
    }

    return group_index;
}

int VideoOutDriver::UnregisterBuffers(VideoOutPort* port, s32 attributeIndex) {
    if (attributeIndex >= MaxDisplayBufferGroups || !port->groups[attributeIndex].is_occupied) {
        LOG_ERROR(Lib_VideoOut, "Invalid attribute index {}", attributeIndex);
        return SCE_VIDEO_OUT_ERROR_INVALID_VALUE;
    }

    auto& group = port->groups[attributeIndex];
    group.is_occupied = false;

    for (auto& buffer : port->buffer_slots) {
        if (buffer.group_index != attributeIndex) {
            continue;
        }
        buffer.group_index = -1;
    }

    return ORBIS_OK;
}

bool VideoOutDriver::SubmitFlip(VideoOutPort* port, s32 index, s64 flip_arg) {
    std::scoped_lock lock{mutex};

    const auto& buffer = port->buffer_slots[index];
    renderer->Present(port->groups[buffer.group_index], buffer.address_left);

    for (auto& event : port->flip_events) {
        if (event != nullptr) {
            event->TriggerEvent(SCE_VIDEO_OUT_EVENT_FLIP, Core::Kernel::EVFILT_VIDEO_OUT,
                                reinterpret_cast<void*>(flip_arg));
        }
    }

    auto& flip_status = port->flip_status;
    flip_status.count++;
    flip_status.processTime = Core::Libraries::LibKernel::sceKernelGetProcessTime();
    flip_status.tsc = Core::Libraries::LibKernel::sceKernelReadTsc();
    flip_status.submitTsc = Core::Libraries::LibKernel::sceKernelReadTsc();
    flip_status.flipArg = flip_arg;
    flip_status.currentBuffer = index;
    flip_status.flipPendingNum = static_cast<int>(requests.size());
    return true;

    if (requests.size() >= 2) {
        return false;
    }

    requests.push({
        .port = port,
        .index = index,
        .flip_arg = flip_arg,
        .submit_tsc = Core::Libraries::LibKernel::sceKernelReadTsc(),
    });

    port->flip_status.flipPendingNum = static_cast<int>(requests.size());
    port->flip_status.gcQueueNum = 0;
    submit_cond.notify_one();

    return true;
}

void VideoOutDriver::PresentThread(std::stop_token token, const ServiceThreadParams* params) {
    if (params && params->set_priority) {
        LOG_WARNING(Lib_VideoOut, "Application requested thread priority {}", params->priority);
    }
    if (params && params->set_affinity) {
        LOG_WARNING(Lib_VideoOut, "Application requested thread affinity {}", params->affinity);
    }

    Common::SetCurrentThreadName("SceVideoOutServiceThread");

    while (!token.stop_requested()) {
        Request* request;
        {
            std::unique_lock lock{mutex};
            submit_cond.wait(lock, token, [&] { return !requests.empty(); });
            if (token.stop_requested()) {
                return;
            }
            request = &requests.front();
            requests.pop();
        }

        // const auto buffer = request->port->buffers[request->index].buffer_render;
        // Emu::DrawBuffer(buffer);

        std::scoped_lock lock{mutex};
        for (auto& event : request->port->flip_events) {
            if (event != nullptr) {
                event->TriggerEvent(SCE_VIDEO_OUT_EVENT_FLIP, Core::Kernel::EVFILT_VIDEO_OUT,
                                    reinterpret_cast<void*>(request->flip_arg));
            }
        }

        done_cond.notify_one();

        auto& flip_status = request->port->flip_status;
        flip_status.count++;
        flip_status.processTime = Core::Libraries::LibKernel::sceKernelGetProcessTime();
        flip_status.tsc = Core::Libraries::LibKernel::sceKernelReadTsc();
        flip_status.submitTsc = request->submit_tsc;
        flip_status.flipArg = request->flip_arg;
        flip_status.currentBuffer = request->index;
        flip_status.flipPendingNum = static_cast<int>(requests.size());
    }
}

} // namespace Core::Libraries::VideoOut
