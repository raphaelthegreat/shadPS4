// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/assert.h"
#include "common/config.h"
#include "common/logging/log.h"
#include "core/libraries/error_codes.h"
#include "core/libraries/libs.h"
#include "core/libraries/system/userservice.h"
#include "core/libraries/videoout/driver.h"
#include "core/libraries/videoout/video_out.h"
#include "core/loader/symbols_resolver.h"

namespace Libraries::VideoOut {

static std::unique_ptr<VideoOutDriver> driver;

void PS4_SYSV_ABI sceVideoOutSetBufferAttribute(BufferAttribute* attribute, PixelFormat pixelFormat,
                                                u32 tilingMode, u32 aspectRatio, u32 width,
                                                u32 height, u32 pitchInPixel) {
    LOG_INFO(Lib_VideoOut,
             "pixelFormat = {}, tilingMode = {}, aspectRatio = {}, width = {}, height = {}, "
             "pitchInPixel = {}",
             GetPixelFormatString(pixelFormat), tilingMode, aspectRatio, width, height,
             pitchInPixel);

    std::memset(attribute, 0, sizeof(BufferAttribute));
    attribute->pixel_format = static_cast<PixelFormat>(pixelFormat);
    attribute->tiling_mode = static_cast<TilingMode>(tilingMode);
    attribute->aspect_ratio = aspectRatio;
    attribute->width = width;
    attribute->height = height;
    attribute->pitch_in_pixel = pitchInPixel;
    attribute->option = SCE_VIDEO_OUT_BUFFER_ATTRIBUTE_OPTION_NONE;
}

s32 PS4_SYSV_ABI sceVideoOutAddFlipEvent(Kernel::SceKernelEqueue eq, s32 handle, void* udata) {
    LOG_INFO(Lib_VideoOut, "handle = {}", handle);

    auto* port = driver->GetPort(handle);
    if (port == nullptr) {
        return ORBIS_VIDEO_OUT_ERROR_INVALID_HANDLE;
    }

    if (eq == nullptr) {
        return ORBIS_VIDEO_OUT_ERROR_INVALID_EVENT_QUEUE;
    }

    Kernel::EqueueEvent event{};
    event.isTriggered = false;
    event.event.ident = SCE_VIDEO_OUT_EVENT_FLIP;
    event.event.filter = Kernel::EVFILT_VIDEO_OUT;
    event.event.udata = udata;
    event.event.fflags = 0;
    event.event.data = 0;
    event.filter.data = port;

    port->flip_events.push_back(eq);
    return eq->addEvent(event);
}

s32 PS4_SYSV_ABI sceVideoOutRegisterBuffers(s32 handle, s32 startIndex, void* const* addresses,
                                            s32 bufferNum, const BufferAttribute* attribute) {
    if (!addresses || !attribute) {
        LOG_ERROR(Lib_VideoOut, "Addresses are null");
        return ORBIS_VIDEO_OUT_ERROR_INVALID_ADDRESS;
    }

    auto* port = driver->GetPort(handle);
    if (!port || !port->is_open) {
        LOG_ERROR(Lib_VideoOut, "Invalid handle = {}", handle);
        return ORBIS_VIDEO_OUT_ERROR_INVALID_HANDLE;
    }

    return driver->RegisterBuffers(port, startIndex, addresses, bufferNum, attribute);
}

s32 PS4_SYSV_ABI sceVideoOutSetFlipRate(s32 handle, s32 rate) {
    LOG_INFO(Lib_VideoOut, "called");
    driver->GetPort(handle)->flip_rate = rate;
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceVideoOutIsFlipPending(s32 handle) {
    LOG_INFO(Lib_VideoOut, "called");
    s32 pending = driver->GetPort(handle)->flip_status.flipPendingNum;
    return pending;
}

s32 PS4_SYSV_ABI sceVideoOutSubmitFlip(s32 handle, s32 bufferIndex, s32 flipMode, s64 flipArg) {
    auto* port = driver->GetPort(handle);
    if (!port) {
        LOG_ERROR(Lib_VideoOut, "Invalid handle = {}", handle);
        return ORBIS_VIDEO_OUT_ERROR_INVALID_HANDLE;
    }

    if (flipMode != 1) {
        LOG_WARNING(Lib_VideoOut, "flipmode = {}", flipMode);
    }

    ASSERT_MSG(bufferIndex != -1, "Blank output not supported");

    if (bufferIndex < -1 || bufferIndex > 15) {
        LOG_ERROR(Lib_VideoOut, "Invalid bufferIndex = {}", bufferIndex);
        return ORBIS_VIDEO_OUT_ERROR_INVALID_INDEX;
    }

    if (port->buffer_slots[bufferIndex].group_index < 0) {
        LOG_ERROR(Lib_VideoOut, "Slot in bufferIndex = {} is not registered", bufferIndex);
        return ORBIS_VIDEO_OUT_ERROR_INVALID_INDEX;
    }

    LOG_INFO(Lib_VideoOut, "bufferIndex = {}, flipMode = {}, flipArg = {}", bufferIndex, flipMode,
             flipArg);

    if (!driver->SubmitFlip(port, bufferIndex, flipArg)) {
        LOG_ERROR(Lib_VideoOut, "Flip queue is full");
        return ORBIS_VIDEO_OUT_ERROR_FLIP_QUEUE_FULL;
    }

    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceVideoOutGetFlipStatus(s32 handle, FlipStatus* status) {
    if (!status) {
        LOG_ERROR(Lib_VideoOut, "Flip status is null");
        return ORBIS_VIDEO_OUT_ERROR_INVALID_ADDRESS;
    }

    auto* port = driver->GetPort(handle);
    if (!port) {
        LOG_ERROR(Lib_VideoOut, "Invalid port handle = {}", handle);
        return ORBIS_VIDEO_OUT_ERROR_INVALID_HANDLE;
    }

    *status = port->flip_status;

    LOG_INFO(Lib_VideoOut,
             "count = {}, processTime = {}, tsc = {}, submitTsc = {}, flipArg = {}, gcQueueNum = "
             "{}, flipPendingNum = {}, currentBuffer = {}",
             status->count, status->processTime, status->tsc, status->submitTsc, status->flipArg,
             status->gcQueueNum, status->flipPendingNum, status->currentBuffer);

    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceVideoOutGetVblankStatus(int handle, SceVideoOutVblankStatus* status) {
    if (status == nullptr) {
        return SCE_VIDEO_OUT_ERROR_INVALID_ADDRESS;
    }

    auto* port = driver->GetPort(handle);
    if (!port) {
        LOG_ERROR(Lib_VideoOut, "Invalid port handle = {}", handle);
        return ORBIS_VIDEO_OUT_ERROR_INVALID_HANDLE;
    }

    *status = port->vblank_status;
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceVideoOutGetResolutionStatus(s32 handle, SceVideoOutResolutionStatus* status) {
    LOG_INFO(Lib_VideoOut, "called");
    *status = driver->GetPort(handle)->resolution;
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceVideoOutOpen(SceUserServiceUserId userId, s32 busType, s32 index,
                                 const void* param) {
    LOG_INFO(Lib_VideoOut, "called");
    ASSERT(userId == UserService::ORBIS_USER_SERVICE_USER_ID_SYSTEM || userId == 0);
    ASSERT(busType == SCE_VIDEO_OUT_BUS_TYPE_MAIN);
    ASSERT(param == nullptr);

    if (index != 0) {
        LOG_ERROR(Lib_VideoOut, "Index != 0");
        return ORBIS_VIDEO_OUT_ERROR_INVALID_VALUE;
    }

    auto* params = reinterpret_cast<const ServiceThreadParams*>(param);
    int handle = driver->Open(params);

    if (handle < 0) {
        LOG_ERROR(Lib_VideoOut, "All available handles are open");
        return ORBIS_VIDEO_OUT_ERROR_RESOURCE_BUSY;
    }

    return handle;
}

s32 PS4_SYSV_ABI sceVideoOutClose(s32 handle) {
    driver->Close(handle);
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceVideoOutUnregisterBuffers(s32 handle, s32 attributeIndex) {
    auto* port = driver->GetPort(handle);
    if (!port || !port->is_open) {
        return ORBIS_VIDEO_OUT_ERROR_INVALID_HANDLE;
    }

    return driver->UnregisterBuffers(port, attributeIndex);
}

void Flip(std::chrono::microseconds micros) {
    return driver->Flip(micros);
}

void Vblank() {
    return driver->Vblank();
}

void RegisterLib(Core::Loader::SymbolsResolver* sym) {
    driver = std::make_unique<VideoOutDriver>(Config::getScreenWidth(), Config::getScreenHeight());

    LIB_FUNCTION("SbU3dwp80lQ", "libSceVideoOut", 1, "libSceVideoOut", 0, 0,
                 sceVideoOutGetFlipStatus);
    LIB_FUNCTION("U46NwOiJpys", "libSceVideoOut", 1, "libSceVideoOut", 0, 0, sceVideoOutSubmitFlip);
    LIB_FUNCTION("w3BY+tAEiQY", "libSceVideoOut", 1, "libSceVideoOut", 0, 0,
                 sceVideoOutRegisterBuffers);
    LIB_FUNCTION("HXzjK9yI30k", "libSceVideoOut", 1, "libSceVideoOut", 0, 0,
                 sceVideoOutAddFlipEvent);
    LIB_FUNCTION("CBiu4mCE1DA", "libSceVideoOut", 1, "libSceVideoOut", 0, 0,
                 sceVideoOutSetFlipRate);
    LIB_FUNCTION("i6-sR91Wt-4", "libSceVideoOut", 1, "libSceVideoOut", 0, 0,
                 sceVideoOutSetBufferAttribute);
    LIB_FUNCTION("6kPnj51T62Y", "libSceVideoOut", 1, "libSceVideoOut", 0, 0,
                 sceVideoOutGetResolutionStatus);
    LIB_FUNCTION("Up36PTk687E", "libSceVideoOut", 1, "libSceVideoOut", 0, 0, sceVideoOutOpen);
    LIB_FUNCTION("zgXifHT9ErY", "libSceVideoOut", 1, "libSceVideoOut", 0, 0,
                 sceVideoOutIsFlipPending);
    LIB_FUNCTION("N5KDtkIjjJ4", "libSceVideoOut", 1, "libSceVideoOut", 0, 0,
                 sceVideoOutUnregisterBuffers);
    LIB_FUNCTION("uquVH4-Du78", "libSceVideoOut", 1, "libSceVideoOut", 0, 0, sceVideoOutClose);
    LIB_FUNCTION("1FZBKy8HeNU", "libSceVideoOut", 1, "libSceVideoOut", 0, 0,
                 sceVideoOutGetVblankStatus);

    // openOrbis appears to have libSceVideoOut_v1 module libSceVideoOut_v1.1
    LIB_FUNCTION("Up36PTk687E", "libSceVideoOut", 1, "libSceVideoOut", 1, 1, sceVideoOutOpen);
    LIB_FUNCTION("CBiu4mCE1DA", "libSceVideoOut", 1, "libSceVideoOut", 1, 1,
                 sceVideoOutSetFlipRate);
    LIB_FUNCTION("HXzjK9yI30k", "libSceVideoOut", 1, "libSceVideoOut", 1, 1,
                 sceVideoOutAddFlipEvent);
    LIB_FUNCTION("i6-sR91Wt-4", "libSceVideoOut", 1, "libSceVideoOut", 1, 1,
                 sceVideoOutSetBufferAttribute);
    LIB_FUNCTION("w3BY+tAEiQY", "libSceVideoOut", 1, "libSceVideoOut", 1, 1,
                 sceVideoOutRegisterBuffers);
    LIB_FUNCTION("U46NwOiJpys", "libSceVideoOut", 1, "libSceVideoOut", 1, 1, sceVideoOutSubmitFlip);
    LIB_FUNCTION("SbU3dwp80lQ", "libSceVideoOut", 1, "libSceVideoOut", 1, 1,
                 sceVideoOutGetFlipStatus);
}

} // namespace Libraries::VideoOut
