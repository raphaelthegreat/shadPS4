#pragma once

#include <types.h>

namespace Core::Frontend {

class EmuWindow {
public:
    explicit EmuWindow(s32 width_, s32 height_)
        : width{width_}, height{height_} {}
    virtual ~EmuWindow() = default;

    s32 getWidth() const {
        return width;
    }

    s32 getHeight() const {
        return height;
    }

    bool isRunning() const {
        return is_running;
    }
    
    virtual void pollEvents();

protected:
    s32 width{};
    s32 height{};
    bool is_running{};
};

} // namespace Core::Frontend