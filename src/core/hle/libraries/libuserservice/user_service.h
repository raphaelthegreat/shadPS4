#pragma once

#include "core/loader/symbols_resolver.h"

namespace Core::Libraries {

using SceUserServiceUserId = s32;

struct SceUserServiceInitializeParams {
    s32 priority;
};

struct SceUserServiceLoginUserIdList {
    int user_id[4];
};

s32 PS4_SYSV_ABI sceUserServiceInitialize(const SceUserServiceInitializeParams* initParams);
s32 PS4_SYSV_ABI sceUserServiceGetLoginUserIdList(SceUserServiceLoginUserIdList* userIdList);

void libUserService_Register(Loader::SymbolsResolver* sym);

};  // Core::Libraries
