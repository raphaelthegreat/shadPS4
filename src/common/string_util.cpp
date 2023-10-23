#include <algorithm>
#include <sstream>

#include "common/string_util.h"

namespace Common {

std::vector<std::string> splitString(const std::string& str, char delim) {
    std::istringstream iss(str);
    std::vector<std::string> output(1);

    while (std::getline(iss, *output.rbegin(), delim)) {
        output.emplace_back();
    }

    output.pop_back();
    return output;
}

} // namespace Common
