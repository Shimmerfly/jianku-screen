#pragma once

#include "CanvasLayout.h"
#include "MotionBlur.h"

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

// The microphone track, recorded separately as microphone.m4a.
//
// It is kept as its own file so it stays re-editable, which means the exporter
// has to align it. The recorder stamps `startHostTimeNs` on the same host clock
// the video frames use; both timelines fold out the same pause intervals, so the
// two stay a constant offset apart for the whole recording and a single delay is
// enough to line them up.
struct MicrophoneTrack {
    bool present = false;
    QString file;
    double durationMs = 0.0;
    qint64 startHostTimeNs = 0;   // 0 when the recording predates this field
    bool startKnown = false;
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
    qint64 mediaZeroHostNs = 0;
    QString videoFile;
    MicrophoneTrack microphone;

    std::vector<double> frameMediaMs;            // one entry per recorded frame
    std::vector<Animation::InputEvent> events;   // source-pixel coordinates
    std::vector<CursorObservation> cursorObservations;
    QHash<QString, CursorDefinition> cursors;
    std::vector<ZoomRangeEntry> zoomRanges;

    // Microphone delay in milliseconds, or 0 when it cannot be determined.
    // Positive means the microphone has to be held back to line up with the
    // video, which is the usual case: the recorder starts it before the first
    // frame arrives.
    double microphoneDelayMs() const;
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

struct Snapshot {
    CameraPose camera;
    CursorPose cursor;
};

// Motion blur strengths, as stored in a project and as a caller may override them.
// A negative value means "leave whatever the project says", so a caller can turn a
// single channel up without having to restate the others.
struct MotionBlurSettings {
    double amount = -1.0;
    double cursorAmount = -1.0;
    double screenMoveAmount = -1.0;
    double screenZoomAmount = -1.0;
    double fps = -1.0;
};

// Settings the compositor honours, resolved once from the project map.
struct ComposerSettings {
    QSizeF canvasSize;    double paddingPercent = 0.0;
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

    // Motion blur strengths, straight from the project's settings. The reference
    // keeps one global amount plus a per-channel one for the pointer, screen
    // movement and screen zoom; the same split is kept here so a project's saved
    // values keep their meaning.
    MotionBlurSettings motionBlur;
};

// One layer's blur for a single frame, already resolved into canvas pixels.
struct LayerBlur {
    MotionBlur::Channel channel = MotionBlur::Channel::None;
    QPointF moveVector;        // canvas pixels
    double zoomStrength = 0.0;
    QPointF zoomCentre;        // canvas pixels
};

// Both blurrable layers of a frame. The background never blurs: it does not move
// with the camera, so blurring it would smear the wallpaper while the content
// stayed sharp.
struct BlurPlan {
    LayerBlur screen;
    LayerBlur cursor;

    bool empty() const {
        return screen.channel == MotionBlur::Channel::None
            && cursor.channel == MotionBlur::Channel::None;
    }
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

ComposeContext makeComposeContext(ProjectData project, const QString &backgroundRoot,
    const MotionBlurSettings &blurOverride = {});

// Applies caller overrides on top of the project's own values. Any field left
// negative in `overrides` keeps what `settings` already holds.
void applyMotionBlurOverrides(MotionBlurSettings &settings, const MotionBlurSettings &overrides);

// Works out this frame's blur from where the two layers were last frame and where
// they are now. `previous`/`current` are the poses the compositor is about to draw;
// `mediaTimeMs` picks the pointer shape whose size defines the pointer boundary.
BlurPlan planBlur(const ComposeContext &context, const CameraPose &previousCamera,
    const CursorPose &previousCursor, const CameraPose &camera, const CursorPose &cursor,
    double mediaTimeMs, bool includeCursor);

// Renders one composited canvas. `blur` is optional; when given, the screen layer
// and the pointer are smeared according to the plan before being drawn.
QImage composeFrame(const ComposeContext &context, const QImage &source,
    const CameraPose &camera, const CursorPose &cursor, double mediaTimeMs, bool includeCursor,
    const BlurPlan *blur = nullptr);

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
    // Mix the separately recorded microphone track into the output. Ignored when
    // the project has no microphone.m4a; see `ComposeResult::microphoneMuxed`.
    bool includeMicrophone = true;
    // Overrides for the project's motion blur strengths. A negative field keeps
    // what the project saved, so the CLI can compare a clip with and without blur
    // without editing the project.
    MotionBlurSettings motionBlur;
    // Mix levels, applied only on the microphone path (the system-only path is a
    // stream copy and stays untouched).
    double systemAudioVolume = 1.0;
    double microphoneVolume = 1.0;
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
    // True when microphone.m4a was mixed in, and the offset that was applied.
    bool microphoneMuxed = false;
    double microphoneDelayMs = 0.0;
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
