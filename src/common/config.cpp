// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <fstream>
#include <string>
#include <fmt/core.h>
#include <toml.hpp>
#include "config.h"

namespace Config {

bool isNeo = false;
u32 screenWidth = 1280;
u32 screenHeight = 720;
std::string logFilter;

bool isNeoMode() {
    return isNeo;
}

u32 GetScreenWidth() {
    return screenWidth;
}

u32 GetScreenHeight() {
    return screenHeight;
}

std::string getLogFilter() {
    return logFilter;
}

void Load(const std::filesystem::path& path) {
    // If the configuration file does not exist, create it and return
    std::error_code error;
    if (!std::filesystem::exists(path, error)) {
        Save(path);
        return;
    }

    toml::value data;

    try {
        data = toml::parse(path);
    } catch (std::exception& ex) {
        fmt::print("Got exception trying to load config file. Exception: {}\n", ex.what());
        return;
    }

    if (data.contains("General")) {
        auto generalResult = toml::expect<toml::value>(data.at("General"));
        if (generalResult.is_ok()) {
            auto general = generalResult.unwrap();

            isNeo = toml::find_or<toml::boolean>(general, "isPS4Pro", false);
            logFilter = toml::find_or<toml::string>(general, "logFilter", "");
        }
    }
    if (data.contains("GPU")) {
        auto generalResult = toml::expect<toml::value>(data.at("GPU"));
        if (generalResult.is_ok()) {
            auto general = generalResult.unwrap();

            screenWidth = toml::find_or<toml::integer>(general, "screenWidth", false);
            screenHeight = toml::find_or<toml::integer>(general, "screenHeight", false);
        }
    }
}
void Save(const std::filesystem::path& path) {
    toml::basic_value<toml::preserve_comments> data;

    std::error_code error;
    if (std::filesystem::exists(path, error)) {
        try {
            data = toml::parse<toml::preserve_comments>(path);
        } catch (const std::exception& ex) {
            fmt::print("Exception trying to parse config file. Exception: {}\n", ex.what());
            return;
        }
    } else {
        if (error) {
            fmt::print("Filesystem error accessing {} (error: {})\n", path.string(),
                       error.message().c_str());
        }
        fmt::print("Saving new configuration file {}\n", path.string());
    }

    data["General"]["isPS4Pro"] = isNeo;
    data["General"]["logFilter"] = logFilter;
    data["GPU"]["screenWidth"] = screenWidth;
    data["GPU"]["screenHeight"] = screenHeight;

    std::ofstream file(path, std::ios::out);
    file << data;
    file.close();
}
} // namespace Config
