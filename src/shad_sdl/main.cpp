#include <thread>
#include <fmt/core.h>
#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl3.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>

#include "common/discord.h"
#include "common/log.h"
#include "core/emulator.h"
#include "core/linker.h"
#include "core/hle/libraries/libscevideoout/video_out.h"
#include "core/hle/libraries/libs.h"
#include "shad_sdl/config.h"
#include "shad_sdl/elf_viewer.h"

int main(int argc, char* argv[]) {
    if (argc == 1) {
        fmt::print("Usage: {} <elf or eboot.bin path>\n", argv[0]);
        return -1;
    }

    // Initialize logging
    Common::Logging::init(true);

    // Read configuration file and intialize subsystems
    Config config{"config.toml"};
    Core::Emulator emulator{config};
    emulator.run();
    //HLE::Libs::Graphics::VideoOut::videoOutInit(width, height);

    const std::string_view path{argv[1]};
    auto* linker = singleton<Linker>::instance();
    HLE::Libs::Init_HLE_Libs(linker->getHLESymbols());
    auto* module = linker->LoadModule(path);  // load main executable
    std::jthread mainthread(
        [](std::stop_token stop_token, void*) {
            auto* linker = singleton<Linker>::instance();
            linker->Execute();
        },
        nullptr);
    Common::DiscordRPC rpc;
    rpc.update(Common::RPCStatus::Idling, "");
    Emu::emuRun();
    return 0;
}
