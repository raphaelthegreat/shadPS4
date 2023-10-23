#pragma once

#include "core/loader/symbols_resolver.h"

namespace Core::Libraries {

//HLE functions
s32 PS4_SYSV_ABI sceSystemServiceHideSplashScreen();

void libSystemService_Register(Loader::SymbolsResolver* sym);

};  // namespace Core::Libraries
