#pragma once

#include <QImage>
#include <QPointF>
#include <functional>
#include <vector>

// Motion blur, following the static findings for the reference's 3.7.5 renderer
// (research/桌面动画处理链-官方3.7.5静态.md §6).
//
// Everything here is a pure function of the layer motion, so the offline
// compositor can drive it from the exact pose sequence instead of from live GPU
// state. That matters: an earlier real-time GLSL attempt flickered while zooming
// and tore while panning, and was reverted. Sampling positions that come from a
// deterministic timeline can be reasoned about and tested.
//
// What the static research pins down:
//   - strengths are multiplied by fps / 60
//   - a frame's change is either a move or a zoom, decided by which is larger
//     (a size change strictly larger than the centre displacement picks zoom);
//     a selected change shorter than 1 px gets no filter at all
//   - move: kernel 21, one-sided distribution — the centre plus 20 equally
//     weighted samples at velocity * i/20, i = 0..19, so i = 0 repeats the centre
//   - zoom: max kernel 13, sampled toward the blur centre at ratio
//     (t + prng(textureCoordinate)) / 13, weight 4 * (p - p²), normalised
//   - a cursor layer has its parent's motion subtracted, and an axis whose sign
//     then differs from the parent's is zeroed
//
// Not verified: the final appearance at different frame rates, preview/export
// parity, and the alpha handling at transparent edges.
namespace Render::MotionBlur {

// Which channel drives this frame's blur. The reference picks one, never both.
enum class Channel { None, Move, Zoom };

// The frame-to-frame change of one layer, in canvas pixels.
//
// Both quantities the channel decision compares are lengths in canvas pixels, and
// the static notes' two thresholds are consistent only in those units:
//   - the selected change must be at least 1 px, otherwise nothing is blurred;
//   - the strength-scaled move vector must be at least 0.01 px. With the default
//     strengths (1 × 1 × fps/60) that is implied by the first threshold, so it
//     only ever bites when a channel's strength is dialled almost to zero — which
//     is exactly the case it exists to guard.
struct LayerMotion {
    QPointF centre;          // boundary centre
    QPointF centreDelta;     // boundary centre displacement since the previous frame
    double diagonal = 0.0;   // boundary diagonal
    double previousDiagonal = 0.0;
};

struct Decision {
    Channel channel = Channel::None;
    // Move displacement in canvas pixels — the vector to sample along.
    QPointF moveVector;
    double moveLength = 0.0;
    double zoomStrength = 0.0;
};

// Minimum selected change before any filter is applied at all, in canvas pixels.
inline constexpr double kMinimumChange = 1.0;
// A move whose strength-scaled length is below this (also canvas pixels) gets no
// filter. It only matters when a strength is dialled almost to zero.
inline constexpr double kMinimumMoveLength = 0.01;
// Upper bound on the smear, as a fraction of the layer's diagonal.
//
// The static findings do not pin down the unit of the strength multiplier: the two
// thresholds they do give (1 and 0.01) are a hundred apart, so at most one of them
// can be in canvas pixels. Taking the vector as a literal one-frame displacement
// was measured on a real recording and produces a smear of over 200 px during a
// fast camera release — most of the frame ends up translucent and the background
// shows through the middle of the picture. That is neither the "slight trailing"
// the effect is for nor usable, and an unbounded version is exactly the kind of
// per-frame swing that made the earlier real-time attempt flicker.
//
// So the behaviour is bounded on purpose, and the bound is one named constant
// rather than a magic number buried in the maths. 0.3% of the diagonal is about
// 12 px on a 3360x2100 canvas: a visible trailing on text without making it
// unreadable, and nowhere near a full-canvas wipe.
//
// This constant is the one part of the effect that is a choice rather than a
// finding. The reference's own multiplier units are not pinned down, and no
// reference output frames have been compared against yet, so the value is expected
// to be corrected once that comparison exists — the queue records it as pending.
inline constexpr double kMaximumSmearRatio = 0.003;
inline constexpr int kMoveKernel = 21;
inline constexpr int kZoomMaxKernel = 13;

// `amount` is the global strength, `moveAmount` / `zoomAmount` the per-channel
// ones, `strengthFactor` the fps / 60 conversion.
Decision decide(const LayerMotion &motion, double amount, double moveAmount, double zoomAmount,
    double strengthFactor);

// One sampling position for the move filter, as a pixel offset plus its weight.
// Offsets that land on the same pixel are merged, so a slow move costs a few
// draws instead of 21 — the reference's uniform weights are preserved exactly.
struct Sample {
    int dx = 0;
    int dy = 0;
    double weight = 0.0;
};

// The 21 one-sided positions the reference asks for, as offsets in pixels.
std::vector<QPointF> moveSampleOffsets(const QPointF &vector, int kernel = kMoveKernel);

// The same list with pixel-identical offsets merged, weights summing to 1.
std::vector<Sample> mergeSamples(const std::vector<QPointF> &offsets);

// Subtracts a parent layer's motion from a child's, zeroing an axis whose sign
// then disagrees with the parent's. This is what keeps a cursor that is being
// carried by a panning camera from being blurred twice.
QPointF subtractParentMotion(const QPointF &child, const QPointF &parent);

// Deterministic pseudo-random value in [0,1) for a pixel, standing in for the
// texture-coordinate driven offset the reference's zoom shader uses.
double pixelNoise(int x, int y);

// Averages `layer` over the given pixel offsets. Weights are applied as opacity
// into a premultiplied accumulator, so transparent edges average correctly
// instead of darkening.
QImage accumulate(const QImage &layer, const std::vector<Sample> &samples);

// Move blur: the one-sided smear along `vector`. Returns `layer` unchanged when
// the filter does not apply.
QImage applyMove(const QImage &layer, const QPointF &vector);

// Zoom blur toward `centre` (layer-local pixels). `strength` scales both the
// sampling radius and the gradient, as the reference's radius term does.
QImage applyZoom(const QImage &layer, const QPointF &centre, double strength,
    int maxKernel = kZoomMaxKernel);

} // namespace Render::MotionBlur
