#pragma once

#include <cstdint>
#include <string_view>
#include <discord_rpc.h>

#include "common/types.h"

namespace Common {

enum class RPCStatus : u32 {
    Idling,
    Playing,
};

class DiscordRPC {
public:
    explicit DiscordRPC();
    ~DiscordRPC();

    void init();
    void update(RPCStatus status, std::string_view title);
    void stop();

private:
    std::uint64_t start_timestamp;
};

} // namespace Common
