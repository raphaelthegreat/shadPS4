// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/libraries/gnmdriver/libscegnmdriver.h"
#include "core/libraries/kernel/libkernel.h"
#include "core/libraries/libc/libc.h"
#include "core/libraries/libs.h"
#include "core/libraries/pad/pad.h"
#include "core/libraries/systemservice/system_service.h"
#include "core/libraries/userservice/libuserservice.h"
#include "core/libraries/videoout/video_out.h"

namespace Core::Libraries {

void InitHLELibs(Loader::SymbolsResolver* sym) {
    LibKernel::LibKernel_Register(sym);
    VideoOut::RegisterLib(sym);
    LibSceGnmDriver::LibSceGnmDriver_Register(sym);
    LibUserService::userServiceSymbolsRegister(sym);
    LibPad::padSymbolsRegister(sym);
    LibSystemService::systemServiceSymbolsRegister(sym);
    LibC::libcSymbolsRegister(sym);
}

} // namespace Core::Libraries
