#include "core/hle/libraries/libs.h"
#include "LibC.h"
#include "core/hle/libraries/libkernel/libkernel.h"
#include "core/hle/libraries/libscegnmdriver/libscegnmdriver.h"
#include "core/hle/libraries/libscevideoout/video_out.h"
#include "core/hle/libraries/libuserservice/user_service.h"
#include "core/hle/libraries/libpad/pad.h"
#include "core/hle/libraries/libsystemservice/system_service.h"

namespace Core::Libraries {

void Init_HLE_Libs(Loader::SymbolsResolver *sym) {
    LibC::LibC_Register(sym);
    LibKernel::LibKernel_Register(sym);
    Graphics::VideoOut::videoOutRegisterLib(sym);
    LibSceGnmDriver::LibSceGnmDriver_Register(sym);
    Emulator::HLE::Libraries::LibUserService::libUserService_Register(sym);
    Emulator::HLE::Libraries::LibPad::libPad_Register(sym);
    Emulator::HLE::Libraries::LibSystemService::libSystemService_Register(sym);
}
}  // namespace Core::Libraries
