#pragma once

namespace Animation {

struct SpringConfig {
    double stiffness = 90.0;
    double damping = 9.0;
    double mass = 1.0;
    bool clamp = false;
    double precision = 0.002;

    bool operator==(const SpringConfig &other) const {
        return stiffness == other.stiffness && damping == other.damping
            && mass == other.mass && clamp == other.clamp && precision == other.precision;
    }
    bool operator!=(const SpringConfig &other) const { return !(*this == other); }
};

// Port of the reference spring solver. Semantics are documented in
// research/弹簧求解器-官方包核对.md and must not be "simplified".
class Spring {
public:
    Spring() = default;
    explicit Spring(const SpringConfig &config) : config_(config) {}

    SpringConfig config() const { return config_; }
    void setConfig(const SpringConfig &config);
    void setDisabled(bool disabled) { disabled_ = disabled; }
    bool disabled() const { return disabled_; }

    double value() const { return value_; }
    double velocity() const { return velocity_; }
    double target() const { return target_; }
    double timeMs() const { return timeMs_; }
    bool isAtRest() const { return target_ == value_ && velocity_ == 0.0; }

    void setValue(double value) { value_ = value; }
    void setTargetValue(double target);
    void advanceBy(double dtMs);
    void advanceTo(double timeMs);
    void snapToTarget();
    void reset(double value);

    double settleTime() const;

private:
    SpringConfig config_;
    double value_ = 0.0;
    double velocity_ = 0.0;
    double target_ = 0.0;
    double timeMs_ = 0.0;
    double lastTargetChangeMs_ = 0.0;
    double cachedPrecision_ = 1.0;
    mutable double cachedSettleTime_ = -1.0;
    bool disabled_ = false;
};

double effectivePrecision(double value, double target, double precision);
double estimateSettleTime(const SpringConfig &config);

} // namespace Animation
