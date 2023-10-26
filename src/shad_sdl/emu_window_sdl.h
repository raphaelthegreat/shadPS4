#pragma once

#include "core/frontend/emu_window.h"

namespace Core::Input {
class GameController;
}

union SDL_Event;
struct SDL_Window;

class EmuWindowSDL : public Core::Frontend::EmuWindow {
  public:
    explicit EmuWindowSDL(Core::Input::GameController& controller, s32 width, s32 height);
    ~EmuWindowSDL();

    void pollEvents() override;

  private:
    void keyboardEvent(SDL_Event* event);
    void resizeEvent();

  private:
    Core::Input::GameController& controller;
    SDL_Window* sdl_window{};
};
