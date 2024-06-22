// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/assert.h"
#include "core/libraries/kernel/event_queue.h"

namespace Libraries::Kernel {

EqueueInternal::~EqueueInternal() = default;

int EqueueInternal::addEvent(EqueueEvent& event) {
    std::scoped_lock lock{m_mutex};

    const auto start_clock = std::chrono::high_resolution_clock::now();
    event.filter.added_time_us =
        std::chrono::time_point_cast<std::chrono::microseconds>(start_clock);

    const auto& it = std::ranges::find(m_events, event);
    if (it != m_events.cend()) {
        *it = event;
    } else {
        m_events.push_back(event);
    }
    return 0;
}

int EqueueInternal::removeEvent(u64 id) {
    std::scoped_lock lock{m_mutex};

    const auto& event_q =
        std::ranges::find_if(m_events, [id](auto& ev) { return ev.event.ident == id; });
    ASSERT(event_q != m_events.cend());
    m_events.erase(event_q);
    return 0;
}

int EqueueInternal::waitForEvents(SceKernelEvent* ev, int num, u32 micros) {
    std::unique_lock lock{m_mutex};
    int ret = 0;

    const auto predicate = [&] {
        ret = getTriggeredEvents(ev, num);
        return ret > 0;
    };

    if (micros == 0) {
        m_cond.wait(lock, predicate);
    } else {
        m_cond.wait_for(lock, std::chrono::microseconds(micros), predicate);
    }
    return ret;
}

bool EqueueInternal::triggerEvent(u64 ident, s16 filter, void* trigger_data) {
    {
        std::scoped_lock lock{m_mutex};

        for (auto& event : m_events) {
            if (event.event.ident == ident && event.event.filter == filter) {
                event.Trigger(trigger_data);
            }
        }
    }
    m_cond.notify_one();

    return true;
}

int EqueueInternal::getTriggeredEvents(SceKernelEvent* ev, int num) {
    int ret = 0;

    for (auto& event : m_events) {
        if (event.IsTriggered()) {
            ev[ret++] = event.event;
            event.Reset();
        }
    }

    return ret;
}

} // namespace Libraries::Kernel
