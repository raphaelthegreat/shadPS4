#pragma once

#include <cstdint>
#include <string_view>

namespace Core::AeroLib {

struct NidEntry {
    std::string_view nid;
    std::string_view name;
};

NidEntry* findByNid(std::string_view nid);

} // namespace Core::AeroLib;
