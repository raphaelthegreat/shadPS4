#include <cstring>

#include "common/log.h"
#include "common/types.h"
#include "core/aerolib/aerolib.h"

namespace Core::AeroLib {

// Use a direct table here + binary search as contents are static
static constexpr NidEntry NID_TABLE[] = {
#define STUB(nid, name) \
    { nid, #name },
#include "aerolib.inl"
#undef STUB
};

NidEntry* findByNid(std::string_view nid) {
    s64 l = 0;
    s64 r = sizeof(NID_TABLE) / sizeof(NID_TABLE[0]) - 1;

    while (l <= r) {
        const size_t m = l + (r - l) / 2;
        const int cmp = std::strcmp(NID_TABLE[m].nid, nid.data());
        if (cmp == 0) {
            return &NID_TABLE[m];
        } else if (cmp < 0) {
            l = m + 1;
        } else {
            r = m - 1;
        }
    }

    return nullptr;
}

} // namespace Core::AeroLib
