#include "SpringSolver.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace Animation {
namespace {
constexpr double kMaxCeilDtMs = 10000.0;
constexpr double kSettleCapMs = 20000.0;
constexpr double kSettleTarget = 1000.0;

void integrate(double dtMs, double &x, double &v, double target,
    const SpringConfig &config, double precision) {
    const double dt = dtMs / 1000.0;
    const double springForce = -(x - target) * config.stiffness;
    const double dampingForce = -v * config.damping;
    const double acceleration = (springForce + dampingForce) / config.mass;
    const double nextVelocity = v + acceleration * dt;
    const double nextValue = x + nextVelocity * dt;
    if (config.clamp
        && ((x < target && nextValue > target) || (x > target && nextValue < target))) {
        x = target;
        v = 0.0;
        return;
    }
    if (std::abs(nextVelocity) < precision && std::abs(nextValue - target) < precision) {
        x = target;
        v = 0.0;
        return;
    }
    x = nextValue;
    v = nextVelocity;
}

void advanceSteps(double totalMs, double &x, double &v, double target,
    const SpringConfig &config, double precision) {
    const double steps = std::ceil(totalMs);
    if (steps > kMaxCeilDtMs)
        throw std::runtime_error("spring advance step too large");
    for (double i = 1.0; i <= steps; i += 1.0) {
        if (i > totalMs)
            integrate(i - totalMs, x, v, target, config, precision);
        else
            integrate(1.0, x, v, target, config, precision);
    }
}
} // namespace

double effectivePrecision(double value, double target, double precision) {
    if (value == target)
        return 1.0;
    return std::max(1.0, std::abs(value - target)) * precision;
}

double estimateSettleTime(const SpringConfig &config) {
    if (config.mass == 0.0)
        return 0.0;
    double x = 0.0;
    double v = 0.000001;
    const double step = 1000.0 / 60.0;
    const double precision = effectivePrecision(x, kSettleTarget, config.precision);
    double elapsed = 0.0;
    while (x != kSettleTarget && v != 0.0) {
        integrate(step, x, v, kSettleTarget, config, precision);
        if (std::isnan(x) || std::isnan(v))
            break;
        elapsed += step;
        if (elapsed > kSettleCapMs)
            break;
    }
    return elapsed;
}

void Spring::setConfig(const SpringConfig &config) {
    config_ = config;
    cachedSettleTime_ = -1.0;
}

void Spring::setTargetValue(double target) {
    if (target_ == target)
        return;
    if (!std::isfinite(target))
        throw std::runtime_error("spring target must be finite");
    lastTargetChangeMs_ = timeMs_;
    cachedPrecision_ = effectivePrecision(value_, target, config_.precision);
    target_ = target;
}

void Spring::advanceBy(double dtMs) {
    if (dtMs == 0.0)
        return;
    if (dtMs < 0.0)
        throw std::runtime_error("spring can't go back in time");
    timeMs_ += dtMs;
    if (disabled_)
        return;
    if (dtMs >= settleTime()) {
        snapToTarget();
        return;
    }
    if (config_.mass == 0.0) {
        snapToTarget();
        return;
    }
    advanceSteps(dtMs, value_, velocity_, target_, config_, cachedPrecision_);
}

void Spring::advanceTo(double timeMs) {
    advanceBy(timeMs - timeMs_);
}

void Spring::snapToTarget() {
    value_ = target_;
    velocity_ = 0.0;
}

void Spring::reset(double value) {
    setValue(value);
    target_ = value;
    velocity_ = 0.0;
}

double Spring::settleTime() const {
    if (cachedSettleTime_ < 0.0)
        cachedSettleTime_ = estimateSettleTime(config_);
    return cachedSettleTime_;
}

} // namespace Animation
