#pragma once

#include <filesystem>
#include "common/types.h"

class Config {
public:
    explicit Config(const std::filesystem::path& path);
    ~Config();

    bool isNeoMode() const {
        return isNeo;
    }

    u32 getLogLevel() const {
        return logLevel;
    }

    u32 getScreenWidth() const {
        return screenWidth;
    }

    u32 getScreenHeight() const {
        return screenHeight;
    }

private:
    void load();
    void save();

private:
    const std::filesystem::path& path;
    bool isNeo = false;
    u32 screenWidth = 1280;
    u32 screenHeight = 720;
    u32 logLevel = 0;  // TRACE = 0 , DEBUG = 1 , INFO = 2 , WARN = 3 , ERROR = 4 , CRITICAL = 5, OFF = 6
};
