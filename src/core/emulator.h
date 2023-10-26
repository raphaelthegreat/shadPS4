#pragma once

#include <thread>

#include "common/types.h"

class Config;

namespace Core {

namespace Frontend {
class EmuWindow;
}

namespace Input {
class GameController;
}

class Emulator {
public:
    explicit Emulator(const Config& config);
    ~Emulator();

    void run();

private:
    const Config& config;
    std::unique_ptr<Input::GameController> controller;
    std::unique_ptr<Frontend::EmuWindow> window;
    std::jthread main_thread;
};

} // namespace Core
