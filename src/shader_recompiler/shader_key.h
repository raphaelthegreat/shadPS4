#pragma once

#include <string>
#include <fmt/format.h>
#include "common/types.h"

namespace Shader::Gcn {

[[nodiscard]] inline u64 HashCombine(const u64 seed, const u64 hash) {
    return seed ^ (hash + 0x9e3779b9 + (seed << 6) + (seed >> 2));
}

class ShaderKey {
public:
    ShaderKey(u32 hash, u32 crc) {
        m_key = HashCombine(hash, crc);
    }

    ~ShaderKey() = default;

    u64 key() const {
        return m_key;
    }

    std::string name() const {
        return fmt::format("SHDR_{:X}", m_key);
    }

    bool operator==(const ShaderKey& other) const {
        return m_key == other.m_key;
    }

private:
    u64 m_key;
};

} // namespace Shader::Gcn

namespace std {
template <>
struct hash<Shader::Gcn::ShaderKey> {
    std::size_t operator()(const Shader::Gcn::ShaderKey& k) const noexcept {
        return k.key();
    }
};
} // namespace std
