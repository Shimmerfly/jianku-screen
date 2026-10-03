#pragma once

#include "CursorEngine.h"
#include "SpringSolver.h"

#include <QString>
#include <QVariantMap>

namespace Animation {

// Single mapping from the persisted settings map to engine parameters.
//
// The preview driver and the offline compositor must never disagree about what a
// setting means, and they already did once: `cursorSmoothing` and `clickEffect`
// were written by the settings page and read by nobody, so turning smoothing off
// still ran the 470/70/3 spring and "no click feedback" still pulsed the cursor.
// Both readers go through these functions now.
struct DriverSettings {
    CursorSettings cursor;
    SpringConfig screenSpring{200.0, 40.0, 2.25, false, 0.002};
    bool autoZoom = true;
    double zoomLevel = 2.0;
    double snapToEdgesRatio = 0.25;
};

// Overlays the values present in `settings` onto `base`, leaving everything else
// untouched so a partial map cannot silently reset unrelated parameters.
DriverSettings driverSettingsFromMap(const QVariantMap &settings, const DriverSettings &base = {});

} // namespace Animation
