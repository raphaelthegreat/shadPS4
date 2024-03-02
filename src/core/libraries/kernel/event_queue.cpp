// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/assert.h"
#include "core/libraries/kernel/event_queue.h"

namespace Core::Kernel {

EqueueInternal::~EqueueInternal() = default;

int EqueueInternal::AddEvent(const EqueueEvent& event) {
    std::scoped_lock lock{m_mutex};

    ASSERT_MSG(m_events.empty(), "Event queue must be empty");
    ASSERT_MSG(!event.is_triggered, "Triggered events are not supported!");

    // TODO check if event is already exists and return it. Currently we just add in m_events array
    m_events.push_back(event);

    return 0;
}

int EqueueInternal::WaitForEvents(SceKernelEvent* ev, int num, u32 micros) {
    std::unique_lock lock{m_mutex};
    int ret = 0;

    const auto predicate = [&] {
        ret = GetTriggeredEvents(ev, num);
        return ret > 0;
    };

    if (micros == 0) {
        m_cond.wait(lock, predicate);
    } else {
        m_cond.wait_for(lock, std::chrono::microseconds(micros), predicate);
    }
    return ret;
}

bool EqueueInternal::TriggerEvent(u64 ident, s16 filter, void* trigger_data) {
    std::scoped_lock lock{m_mutex};

    ASSERT_MSG(m_events.size() == 1, "Only one event is supported currently");
    m_events[0].Trigger(trigger_data);
    m_cond.notify_one();

    return true;
}

int EqueueInternal::GetTriggeredEvents(SceKernelEvent* ev, int num) {
    int ret = 0;

    ASSERT_MSG(m_events.size() == 1, "Only one event is supported currently");
    auto& event = m_events[0];

    if (event.is_triggered) {
        ev[ret++] = event.event;
        event.Reset();
    }

    return ret;
}

} // namespace Core::Kernel
