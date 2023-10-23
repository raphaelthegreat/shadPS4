#include "discord.h"

#include <cstring>
#include <ctime>

namespace Common {

DiscordRPC::DiscordRPC() {
	DiscordEventHandlers handlers{};
	Discord_Initialize("1139939140494971051", &handlers, 1, nullptr);
    start_timestamp = time(nullptr);
}

DiscordRPC::~DiscordRPC() {
    Discord_ClearPresence();
    Discord_Shutdown();
}

void DiscordRPC::update(RPCStatus status, std::string_view game) {
	DiscordRichPresence rpc{};

    if (status == RPCStatus::Playing) {
		rpc.details = "Playing a game";
        rpc.state = game.data();
	} else {
		rpc.details = "Idle";
	}

	rpc.largeImageKey = "shadps4";
	rpc.largeImageText = "ShadPS4 is a PS4 emulator";
    rpc.startTimestamp = start_timestamp;

	Discord_UpdatePresence(&rpc);
}

} // namespace Common
