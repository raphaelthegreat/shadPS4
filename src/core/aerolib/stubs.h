#pragma once

#include <string_view>
#include "common/types.h"

namespace Core::AeroLib {

u64 UnresolvedStub();

u64 GetStub(std::string_view nid);

} // namespace Core::AeroLib
