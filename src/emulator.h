// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <filesystem>
#include <thread>

#include "common/discord.h"
#include "core/linker.h"
#include "input/controller.h"
#include "sdl_window.h"

namespace Core {

class Emulator {
public:
    Emulator();
    ~Emulator();

    void Run(const std::filesystem::path& file);

private:
    Discord::RPC discord_rpc;
    Input::GameController controller;
    Frontend::WindowSDL window;
    Core::Linker linker;
    std::jthread mainthread;
};

} // namespace Core
