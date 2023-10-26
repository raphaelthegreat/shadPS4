#include <fmt/core.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_syswm.h>

#include "common/version.h"
#include "core/hle/libraries/libpad/pad.h"
#include "core/input/controller.h"
#include "shad_sdl/emu_window_sdl.h"

EmuWindowSDL::EmuWindowSDL(Core::Input::GameController& controller_, s32 width_, s32 height_)
    : EmuWindow(width_, height_), controller{controller_} {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK) < 0) {
        fmt::print("{}\n", SDL_GetError());
        std::exit(0);
    }

    const std::string title = fmt::format("shadps4 v {}", Common::VERSION);
    sdl_window = SDL_CreateWindowWithPosition(title.c_str(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                              width, height, SDL_WINDOW_HIDDEN | SDL_WINDOW_VULKAN);

    if (!sdl_window) [[unlikely]] {
        fmt::print("{}\n", SDL_GetError());
        std::exit(0);
    }

    SDL_SysWMinfo wm{};
    if (SDL_GetWindowWMInfo(sdl_window, &wm, SDL_SYSWM_CURRENT_VERSION) == SDL_FALSE) {
        fmt::print("Failed to get information from the window manager\n");
        std::exit(EXIT_FAILURE);
    }

    switch (wm.subsystem) {
#ifdef SDL_ENABLE_SYSWM_WINDOWS
        case SDL_SYSWM_TYPE::SDL_SYSWM_WINDOWS:
            window_info.type = Core::Frontend::WindowSystemType::Windows;
            window_info.render_surface = reinterpret_cast<void*>(wm.info.win.window);
            break;
#endif
#ifdef SDL_ENABLE_SYSWM_X11
        case SDL_SYSWM_TYPE::SDL_SYSWM_X11:
            window_info.type = Core::Frontend::WindowSystemType::X11;
            window_info.display_connection = wm.info.x11.display;
            window_info.render_surface = reinterpret_cast<void*>(wm.info.x11.window);
            break;
#endif
#ifdef SDL_ENABLE_SYSWM_WAYLAND
        case SDL_SYSWM_TYPE::SDL_SYSWM_WAYLAND:
            window_info.type = Frontend::WindowSystemType::Wayland;
            window_info.display_connection = wm.info.wl.display;
            window_info.render_surface = wm.info.wl.surface;
            break;
#endif
        default:
            fmt::print("Window manager subsystem {} not implemented", wm.subsystem);
            std::exit(EXIT_FAILURE);
            break;
    }

    SDL_SetWindowResizable(sdl_window, SDL_FALSE);
    is_running = true;
}

EmuWindowSDL::~EmuWindowSDL() {
    SDL_DestroyWindow(sdl_window);
}

void EmuWindowSDL::pollEvents() {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_EVENT_QUIT:
            case SDL_EVENT_TERMINATING:
                is_running = false;
                break;
            case SDL_EVENT_WINDOW_RESIZED:
            case SDL_EVENT_WINDOW_MINIMIZED:
            case SDL_EVENT_WINDOW_MAXIMIZED:
            case SDL_EVENT_WINDOW_RESTORED:
                resizeEvent();
                break;
            case SDL_EVENT_KEY_DOWN:
            case SDL_EVENT_KEY_UP:
                keyboardEvent(&event);
                break;
        }
    }
}

void EmuWindowSDL::keyboardEvent(SDL_Event* event) {
    using Core::Libraries::ScePadButton;

    const ScePadButton button = [event] {
        switch (event->key.keysym.sym) {
            case SDLK_UP:
                return ScePadButton::UP;
            case SDLK_DOWN:
                return ScePadButton::DOWN;
            case SDLK_LEFT:
                return ScePadButton::LEFT;
            case SDLK_RIGHT:
                return ScePadButton::RIGHT;
            case SDLK_KP_8:
                return ScePadButton::TRIANGLE;
            case SDLK_KP_6:
                return ScePadButton::CIRCLE;
            case SDLK_KP_2:
                return ScePadButton::CROSS;
            case SDLK_KP_4:
                return ScePadButton::SQUARE;
            case SDLK_RETURN:
                return ScePadButton::OPTIONS;
            default:
                return ScePadButton::NONE;
        }
    }();

    if (button != ScePadButton::NONE) {
        controller.checKButton(0, static_cast<u32>(button), event->type == SDL_EVENT_KEY_DOWN);
    }
}

void EmuWindowSDL::resizeEvent() {
    SDL_GetWindowSizeInPixels(sdl_window, &width, &height);
}
