// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <filesystem>
#include "common/types.h"

namespace Config {

void Load(const std::filesystem::path& path);
void Save(const std::filesystem::path& path);

bool isNeoMode();
std::string getLogFilter();

u32 GetScreenWidth();
u32 GetScreenHeight();

}; // namespace Config
