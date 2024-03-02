// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <fmt/core.h>
#include "common/config.h"
#include "common/logging/backend.h"
#include "common/path_util.h"
#include "common/singleton.h"
#include "core/file_sys/fs.h"
#include "core/libraries/kernel/thread_management.h"
#include "core/libraries/libs.h"
#include "core/linker.h"
#include "core/tls.h"
#include "emulator.h"

Frontend::WindowSDL* g_window = nullptr;

namespace Core {

static constexpr s32 WindowWidth = 1280;
static constexpr s32 WindowHeight = 720;

Emulator::Emulator() : window{WindowWidth, WindowHeight, &controller} {
    g_window = &window;
    // Read configuration file.
    const auto config_dir = Common::FS::GetUserPath(Common::FS::PathType::UserDir);
    Config::Load(config_dir / "config.toml");

    // Start logger.
    Common::Log::Initialize();
    Common::Log::Start();

    // Start discord integration
    discord_rpc.init();
    discord_rpc.update(Discord::RPCStatus::Idling, "");

    // Initialize kernel and library facilities.
    Libraries::LibKernel::init_pthreads();
    Libraries::InitHLELibs(&linker.getHLESymbols());
    InstallTlsHandler();
}

Emulator::~Emulator() {
    const auto config_dir = Common::FS::GetUserPath(Common::FS::PathType::UserDir);
    Config::Save(config_dir / "config.toml");
    discord_rpc.stop();
}

void Emulator::Run(const std::filesystem::path& file) {
    // Applications expect to be run from /app0 so mount the file's parent path as app0.
    auto* mnt = Common::Singleton<Core::FileSys::MntPoints>::Instance();
    mnt->Mount(file.parent_path(), "/app0");

    // Load the module with the linker and start its execution
    linker.LoadModule(file);
    mainthread = std::jthread([this](std::stop_token stop_token) { linker.Execute(); });

    // Begin main window loop until the application exits
    while (window.IsOpen()) {
        window.WaitEvent();
    }

    std::exit(0);
}

} // namespace Core
