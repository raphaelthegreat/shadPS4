#include "core/hle/libraries/libs.h"
#include "core/hle/libraries/libc/libc.h"
#include "core/hle/libraries/libkernel/libkernel.h"
#include "core/hle/libraries/libscegnmdriver/libscegnmdriver.h"
#include "core/hle/libraries/libscevideoout/video_out.h"
#include "core/hle/libraries/libuserservice/user_service.h"
#include "core/hle/libraries/libpad/pad.h"
#include "core/hle/libraries/libsystemservice/system_service.h"

namespace Core::Libraries {

void Init_HLE_Libs(Loader::SymbolsResolver *sym) {
    LibC::libC_Register(sym);
    libKernel_Register(sym);
    videoOutRegisterLib(sym);
    libSceGnmDriver_Register(sym);
    libUserService_Register(sym);
    libPad_Register(sym);
    libSystemService_Register(sym);
}

}  // namespace Core::Libraries
