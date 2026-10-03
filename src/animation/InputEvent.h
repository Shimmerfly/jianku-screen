#pragma once

#include <algorithm>
#include <utility>
#include <vector>

namespace Animation {

enum class InputKind { Move, Down, Up, Drag };

struct InputEvent {
    double timeMs = 0.0;
    double x = 0.0;
    double y = 0.0;
    InputKind kind = InputKind::Move;
    int cursorIndex = -1;
};

// Time-ordered input track. Mirrors the reference event model (moves and clicks
// merged, later events visible to lookahead queries).
class EventTrack {
public:
    void setEvents(std::vector<InputEvent> events) {
        events_ = std::move(events);
        std::stable_sort(events_.begin(), events_.end(),
            [](const InputEvent &a, const InputEvent &b) { return a.timeMs < b.timeMs; });
    }

    const std::vector<InputEvent> &events() const { return events_; }
    bool empty() const { return events_.empty(); }

    // Live append: events must be appended in non-decreasing time order.
    void append(const InputEvent &event) { events_.push_back(event); }

    void pruneBefore(double timeMs) {
        events_.erase(std::remove_if(events_.begin(), events_.end(),
                          [timeMs](const InputEvent &event) { return event.timeMs < timeMs; }),
            events_.end());
    }

    // First event with time >= t (reference "first at or after").
    const InputEvent *firstAtOrAfter(double t) const {
        for (const auto &event : events_) {
            if (event.timeMs >= t)
                return &event;
        }
        return nullptr;
    }

    // Last event with time <= t.
    const InputEvent *lastAtOrBefore(double t) const {
        const InputEvent *found = nullptr;
        for (const auto &event : events_) {
            if (event.timeMs <= t)
                found = &event;
            else
                break;
        }
        return found;
    }

    // First mouse-down within [t, t + windowMs].
    const InputEvent *firstClickWithin(double t, double windowMs) const {
        for (const auto &event : events_) {
            if (event.timeMs < t)
                continue;
            if (event.timeMs > t + windowMs)
                break;
            if (event.kind == InputKind::Down)
                return &event;
        }
        return nullptr;
    }

    // First movement within [t, t + windowMs] (used by the idle-hide restore).
    const InputEvent *firstMoveWithin(double t, double windowMs) const {
        for (const auto &event : events_) {
            if (event.timeMs < t)
                continue;
            if (event.timeMs > t + windowMs)
                break;
            if (event.kind == InputKind::Move || event.kind == InputKind::Drag)
                return &event;
        }
        return nullptr;
    }

private:
    std::vector<InputEvent> events_;
};

} // namespace Animation
