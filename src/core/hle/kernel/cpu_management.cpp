#include "common/log.h"
#include "core/hle/kernel/cpu_management.h"
#include "core/hle/libraries/libs.h"
#include "Util/config.h"

namespace Core::Kernel {

s32 PS4_SYSV_ABI sceKernelIsNeoMode() {
    PRINT_FUNCTION_NAME();
    const bool isNeo = Config::isNeoMode();
    return isNeo ? 1 : 0;
}

}; // namespace Core::Kernel
