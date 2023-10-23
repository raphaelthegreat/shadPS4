#pragma once

#include "core/loader/symbols_resolver.h"

namespace Core::Libraries {

int32_t sceGnmSubmitDone();
void sceGnmFlushGarlic();

void LibSceGnmDriver_Register(Loader::SymbolsResolver* sym);

};  // namespace Core::Libraries
