#pragma once

#include "CanvasLayout.h"

#include "../animation/CursorEngine.h"
#include "../animation/InputEvent.h"
#include "../animation/SpringSolver.h"

#include <QColor>
#include <QHash>
#include <QImage>
#include <QSizeF>
#include <QString>
#include <QVariantMap>
#include <functional>
#include <vector>

namespace Render {

// One recorded cursor shape: a PNG written during capture plus its hotspot.
struct CursorDefinition {
    QString id;
    QString imagePath;
    double widthPx = 0.0;
    double heightPx = 0.0;
    double hotspotXPx = 0.0;
    double hotspotYPx = 0.0;
    QImage image;
};

// A cursor-shape observation on the folded media clock.
struct CursorObservation {
    double mediaTimeMs = 0.0;
    QString cursorId;
};

// A generated auto-zoom range, straight from automatic-zooms.json.
struct ZoomRangeEntry {
    double startMs = 0.0;
    double endMs = 0.0;
    double zoom = 2.0;
    double snapToEdgesRatio = 0.25;
};

// Everything the compositor needs, loaded and validated once. Nothing here is
// platform specific, so it is fully testable without a capture device.
struct ProjectData {
    bool valid = false;
    QString error;

    QString directory;
    QVariantMap settings;

    QSizeF sourceSize;          // captured pixels
    double durationMs = 0.0;
    QString videoFile;

    std::vector<double> frameMediaMs;            // one entry per recorded frame
    std::vector<Animation::InputEvent> events;   // source-pixel coordinates
    std::vector<CursorObservation> cursorObservations;
    QHash<QString, CursorDefinition> cursors;
    std::vector<ZoomRangeEntry> zoomRanges;
};

// Loads a recorded project directory. Fails loudly instead of composing a
// half-understood timeline: a missing or inconsistent file leaves `valid` false
// and fills `error`.
ProjectData loadProject(const QString &directory, QString *error = nullptr);

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------

// Camera pose for one instant. `offset` is in canvas pixels, applied before the
// frame is scaled — the same order the live preview uses.
struct CameraPose {
    double scale = 1.0;
    double offsetX = 0.0;
    double offsetY = 0.0;
};

// Smoothed pointer pose in source pixels.
struct CursorPose {
    double x = 0.0;
    double y = 0.0;
    double scale = 1.0;
    double rotationDeg = 0.0;
    double alpha = 1.0;
};

// Settings the compositor honours, resolved once from the project map.
struct ComposerSettings {
    QSizeF canvasSize;
    double paddingPercent = 0.0;
    double radius = 0.0;
    double insetSize = 0.0;
    QColor insetColor;
    double insetAlpha = 0.5;
    double shadowIntensity = 0.0;
    double shadowAngle = 90.0;
    double shadowDistance = 0.0;
    double shadowBlur = 0.0;
    double backgroundBlur = 0.0;
    QString backgroundType = QStringLiteral("gradient");
    QColor backgroundColor;
    QColor gradientStart;
    QColor gradientEnd;
    double gradientAngle = 135.0;
    QString backgroundImagePath;
    double cursorSizeFactor = 1.5;
    bool hideCursor = false;
};

// Everything that is constant for a whole clip. Building this once keeps the
// per-frame work to "draw the source and the pointer".
struct ComposeContext {
    ProjectData project;
    ComposerSettings settings;
    CanvasLayout layout;
    QImage background;
    QImage shadow;
    bool valid = false;
    QString error;

    int width() const { return static_cast<int>(settings.canvasSize.width()); }
    int height() const { return static_cast<int>(settings.canvasSize.height()); }
};

ComposeContext makeComposeContext(ProjectData project, const QString &backgroundRoot);

// Renders one composited canvas.
QImage composeFrame(const ComposeContext &context, const QImage &source,
    const CameraPose &camera, const CursorPose &cursor, double mediaTimeMs, bool includeCursor);

// Resolves the cursor definition in force at `mediaTimeMs` (last observation at
// or before it, falling back to the first one).
const CursorDefinition *cursorAt(const ProjectData &project, double mediaTimeMs);

// ---------------------------------------------------------------------------
// Driving the whole clip
// ---------------------------------------------------------------------------

// Walks the springs across the clip so the camera arrives at a zoom the same way
// the preview does, instead of jumping between computed positions.
class AnimationSequence {
public:
    AnimationSequence(const ProjectData &project, const Animation::SpringConfig &screenSpring,
        const Animation::CursorSettings &cursorSettings);

    // Advances to `mediaTimeMs`; times must be non-decreasing.
    CameraPose cameraAt(double mediaTimeMs);
    CursorPose cursorAt(double mediaTimeMs);

    // Framing the springs are heading for, without smoothing.
    static CameraPose cameraTargetAt(const ProjectData &project, double mediaTimeMs,
        double snapFallback);

private:
    const ProjectData &project_;
    Animation::Spring scale_;
    Animation::Spring offsetX_;
    Animation::Spring offsetY_;
    Animation::CursorEngine cursor_;
    Animation::CursorSettings cursorSettings_;
    double timeMs_ = -1.0;
    bool started_ = false;
};

// ---------------------------------------------------------------------------
// Encoding
// ---------------------------------------------------------------------------

struct ComposeOptions {
    QString projectDirectory;
    QString outputPath;          // empty = <project>/composed.mp4
    QString ffmpegPath = QStringLiteral("ffmpeg");
    QString backgroundRoot;
    int fps = 60;
    bool includeCursor = true;
    bool includeAutoZoom = true;
    bool includeAudio = true;
    int maxOutputFrames = 0;     // 0 = the whole clip (used by smoke tests)
    double startMs = 0.0;
    // Polled once per frame. Returning true stops the export and leaves the
    // partial file on disk as .part.mp4 so the work can be inspected.
    std::function<bool()> shouldCancel;
};

struct ComposeResult {
    bool ok = false;
    // True when `shouldCancel` stopped the run. The caller must not report this
    // as a failure; the user asked for it.
    bool cancelled = false;
    QString error;
    QString outputPath;
    qint64 sourceFrames = 0;
    qint64 writtenFrames = 0;
    // Frame count read back out of the finished file. Kept separate from
    // writtenFrames so a container that silently dropped frames (a stray
    // -shortest, a truncated mux) is reported instead of passing as success.
    qint64 encodedFrames = 0;
    int width = 0;
    int height = 0;
    double durationMs = 0.0;
    bool audioMuxed = false;
    QString decoderLog;
    QString encoderLog;
};

// Progress callback: (framesWritten, framesTotal).
using ComposeProgress = std::function<void(qint64, qint64)>;

// Locates ffmpeg / ffprobe. A GUI app launched from Finder inherits a minimal
// PATH (/usr/bin:/bin:/usr/sbin:/sbin), so a Homebrew install is invisible to a
// plain name lookup; these probe the usual prefixes and then the real PATH.
// `ffprobe` is looked for next to the given ffmpeg first, so a matched pair from
// one install is always used together.
QString findFfmpeg();
QString findFfprobe(const QString &ffmpeg = QString());

ComposeResult composeProject(const ComposeOptions &options, const ComposeProgress &progress = {});

} // namespace Render
