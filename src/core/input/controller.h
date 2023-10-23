#pragma once

#include <array>
#include "common/types.h"

namespace Core::Input {

struct State {
    u32 buttonsState{};
};

constexpr u32 MAX_STATES = 64;

class GameController {
  public:
    GameController();
    virtual ~GameController() = default;

    void readState(State* state, bool* isConnected, int* connectedCount);
    State getLastState() const;
    void checKButton(int id, u32 button, bool isPressed);
    void addState(const State& state);


  private:
    bool m_connected{};
    State m_last_state;
    int m_connected_count{};
    u32 m_states_num{};
    u32 m_first_state{};
    std::array<State, MAX_STATES> m_states;
};

}  // namespace Core::Input
