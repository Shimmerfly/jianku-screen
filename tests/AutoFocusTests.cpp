#include "../src/animation/AutoFocusEngine.h"

#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace Animation;

namespace {
void require(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
bool close(double a, double b, double tolerance = 1e-6) {
    return std::abs(a - b) <= tolerance;
}
}

int main() {
    try {
        // Range generation delegates to the verified click-range rules.
        const auto ranges = AutoFocusEngine::ranges({2000.0}, 10000.0, 2.0);
        require(ranges.size() == 1 && close(ranges[0].startMs, 1700.0) && close(ranges[0].endMs, 4500.0),
            "auto-focus range generation");

        // Greedy grouping: 1000x1000 content, zoom 2 -> group box 250 x 350.
        const ZoomRange range{0.0, 5000.0, 2.0};
        std::vector<InputEvent> events{
            {100.0, 100.0, 100.0, InputKind::Move},
            {200.0, 200.0, 200.0, InputKind::Move},
            {300.0, 600.0, 600.0, InputKind::Move},
        };
        // First group = (100,100),(200,200) -> centre (150,150) -> normalised 0.15.
        const FocusPoint first = AutoFocusEngine::focusAt(range, events, 1000.0, 1000.0, 250.0);
        require(close(first.x, 0.15) && close(first.y, 0.15), "grouped focus centre");
        // After the third event's time the second group is selected -> (600,600).
        const FocusPoint second = AutoFocusEngine::focusAt(range, events, 1000.0, 1000.0, 400.0);
        require(close(second.x, 0.6) && close(second.y, 0.6), "later group selection");

        // Empty range falls back to a previous event position, else centre.
        require(close(AutoFocusEngine::focusAt(range, {}, 1000.0, 1000.0, 100.0).x, 0.5),
            "empty range centre");

        // Camera transform: focus centred, clamped to cover the canvas.
        {
            const ScreenTransform centred = AutoFocusEngine::transformFor({0.5, 0.5}, 2.0,
                1000.0, 1000.0, 1000.0, 1000.0, 0.0);
            require(close(centred.scale, 2.0) && close(centred.offsetX, -500.0, 0.5), "centred transform");
        }
        {
            const ScreenTransform edge = AutoFocusEngine::transformFor({0.0, 0.0}, 2.0,
                1000.0, 1000.0, 1000.0, 1000.0, 0.0);
            require(close(edge.offsetX, 0.0) && close(edge.offsetY, 0.0), "top-left clamp");
        }
        {
            const ScreenTransform far = AutoFocusEngine::transformFor({1.0, 1.0}, 2.0,
                1000.0, 1000.0, 1000.0, 1000.0, 0.0);
            require(close(far.offsetX, -1000.0) && close(far.offsetY, -1000.0), "bottom-right clamp");
        }

        std::cout << "auto-focus engine checks passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
