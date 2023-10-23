#include "common/debug.h"
#include "common/log.h"
#include "core/emulator.h"
#include "core/loader/elf.h"
#include "core/hle/libraries/libs.h"
#include "core/hle/libraries/libscevideoout/gpu_memory.h"
#include "core/hle/libraries/libscegnmdriver/libscegnmdriver.h"

namespace Core::Libraries {

s32 sceGnmSubmitDone() {
    PRINT_DUMMY_FUNCTION_NAME();
    return 0;
}

void sceGnmFlushGarlic() {
    PRINT_FUNCTION_NAME();
    GPU::flushGarlic(Emu::getGraphicCtx());
}

void LibSceGnmDriver_Register(Loader::SymbolsResolver* sym) {
    LIB_FUNCTION("yvZ73uQUqrk", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1, sceGnmSubmitDone);
    LIB_FUNCTION("iBt3Oe00Kvc", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1, sceGnmFlushGarlic);
}
    
};
