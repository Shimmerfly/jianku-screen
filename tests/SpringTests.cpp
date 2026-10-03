#include "../src/animation/SpringSolver.h"

#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace Animation;

namespace {
void require(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}

bool close(double a, double b, double tolerance = 1e-9) {
    return std::abs(a - b) <= tolerance;
}
}

int main() {
    try {
        // Precision rule: 1 when equal, otherwise max(1, distance) * precision.
        require(close(effectivePrecision(5.0, 5.0, 0.002), 1.0), "equal precision must be 1");
        require(close(effectivePrecision(0.0, 1000.0, 0.002), 2.0), "scaled precision");
        require(close(effectivePrecision(0.0, 0.5, 0.002), 0.002), "sub-unit precision");

        // mass == 0 snaps immediately.
        {
            Spring spring(SpringConfig{90.0, 9.0, 0.0, false, 0.002});
            spring.setTargetValue(1.0);
            spring.advanceBy(16.0);
            require(close(spring.value(), 1.0) && close(spring.velocity(), 0.0), "mass 0 must snap");
        }

        // Negative time is rejected.
        {
            Spring spring(SpringConfig{470.0, 70.0, 3.0, false, 0.002});
            bool threw = false;
            try { spring.advanceBy(-1.0); } catch (const std::exception &) { threw = true; }
            require(threw, "negative dt must throw");
        }

        // Target change preserves velocity.
        {
            Spring spring(SpringConfig{470.0, 70.0, 3.0, false, 0.002});
            spring.setTargetValue(1.0);
            spring.advanceBy(16.0);
            const double before = spring.velocity();
            require(std::abs(before) > 0.0, "spring must be moving");
            spring.setTargetValue(2.0);
            require(close(spring.velocity(), before), "target change must preserve velocity");
        }

        // Integration happens (not a snap) and then settles.
        {
            Spring spring(SpringConfig{470.0, 70.0, 3.0, false, 0.002});
            spring.setTargetValue(1.0);
            spring.advanceBy(16.666);
            require(spring.value() > 0.0 && spring.value() < 1.0, "spring must integrate, not snap");
            for (int i = 0; i < 400; ++i)
                spring.advanceBy(1000.0 / 60.0);
            require(close(spring.value(), 1.0, 1e-6), "spring must converge to target");
            require(spring.isAtRest(), "spring must come to rest");
        }

        // Fractional-tail identity: 16.666 == 16.0 then 0.666, both end with a 0.334 ms step.
        {
            Spring a(SpringConfig{470.0, 70.0, 3.0, false, 0.002});
            Spring b(SpringConfig{470.0, 70.0, 3.0, false, 0.002});
            a.setTargetValue(1.0);
            b.setTargetValue(1.0);
            a.advanceBy(16.666);
            b.advanceBy(16.0);
            b.advanceBy(0.666);
            require(close(a.value(), b.value(), 1e-9), "fractional tail must use i - totalMs");
            require(close(a.velocity(), b.velocity(), 1e-9), "fractional tail velocity");
        }

        // Step cap: only reachable when dt is below the settle shortcut.
        {
            Spring spring(SpringConfig{1.0, 0.0, 1.0, false, 0.002});
            spring.setTargetValue(1.0);
            require(close(spring.settleTime(), 20000.0), "undamped settle estimate hits cap");
            bool threw = false;
            try { spring.advanceBy(10001.0); } catch (const std::exception &) { threw = true; }
            require(threw, "oversized substep count must throw");
        }

        // A jump beyond the settle estimate snaps instead of integrating.
        {
            Spring spring(SpringConfig{470.0, 70.0, 3.0, false, 0.002});
            spring.setTargetValue(1.0);
            spring.advanceBy(10001.0);
            require(close(spring.value(), 1.0) && spring.isAtRest(), "large jump must snap");
        }

        // Settle estimate is positive and capped.
        {
            const double settle = estimateSettleTime(SpringConfig{470.0, 70.0, 3.0, false, 0.002});
            require(settle > 0.0 && settle <= 20000.0, "settle estimate bounds");
            require(close(estimateSettleTime(SpringConfig{90.0, 9.0, 0.0, false, 0.002}), 0.0),
                "mass 0 settle estimate");
        }

        std::cout << "spring solver checks passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
