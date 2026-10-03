#include "MotionBlur.h"

#include <QtMath>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <thread>
#include <vector>

namespace Render::MotionBlur {
namespace {

// Deterministic hash of a pixel coordinate. The reference derives its zoom
// sample offset from a texture coordinate; the exact bit pattern is unknown, so
// this stands in for it. What matters for reproduction is that the offsets are
// stable for a given pixel and spread over [0,1), which keeps the sampling
// pattern from crawling between frames.
double hashNoise(int x, int y) {
    quint32 h = static_cast<quint32>(x) * 0x9E3779B1u ^ static_cast<quint32>(y) * 0x85EBCA77u;
    h ^= h >> 15;
    h *= 0x2545F491u;
    h ^= h >> 13;
    return (h & 0xFFFFFFu) / double(0x1000000);
}

// Splits [0, height) into one contiguous range per worker and runs `body` on each.
// The blur passes are pure per-pixel work over an 8 MP canvas, so they parallelise
// trivially and this is the difference between a minute and ten for a long export.
void parallelRows(int height, const std::function<void(int, int)> &body) {
    const unsigned hardware = std::max(1u, std::thread::hardware_concurrency());
    const int workers = static_cast<int>(std::min<unsigned>(hardware, static_cast<unsigned>(height)));
    if (workers <= 1) {
        body(0, height);
        return;
    }
    // Rows are handed out in blocks rather than split once, so a machine with
    // fewer free cores than advertised still finishes evenly.
    const int block = std::max(1, height / (workers * 4));
    std::atomic<int> next{0};
    std::vector<std::thread> pool;
    pool.reserve(static_cast<size_t>(workers));
    for (int i = 0; i < workers; ++i) {
        pool.emplace_back([&] {
            while (true) {
                const int begin = next.fetch_add(block);
                if (begin >= height)
                    return;
                body(begin, std::min(height, begin + block));
            }
        });
    }
    for (std::thread &thread : pool)
        thread.join();
}

} // namespace

Decision decide(const LayerMotion &motion, double amount, double moveAmount, double zoomAmount,
    double strengthFactor) {
    Decision decision;
    if (!(amount > 0.0) || !(strengthFactor > 0.0))
        return decision;

    // The two candidates are compared as lengths on the layer's boundary —
    // "how far did the centre travel" against "how much did the size change" —
    // not as a spring's raw velocity, so a fast pan and a fast zoom are told
    // apart by what a viewer would actually see moving.
    const double centreLength = std::hypot(motion.centreDelta.x(), motion.centreDelta.y());
    const double sizeChange = std::abs(motion.diagonal - motion.previousDiagonal);
    const bool zoomDominant = sizeChange > centreLength;

    if (zoomDominant) {
        // A zoom amount of 0 does not fall back to move: the reference leaves the
        // frame unfiltered rather than blurring along the wrong axis.
        if (zoomAmount <= 0.0 || sizeChange < kMinimumChange)
            return decision;
        // The strength itself is the relative size change, a ratio, even though
        // the dominance test above used an absolute length.
        const double relative = motion.previousDiagonal > 0.0
            ? std::abs(1.0 - motion.diagonal / motion.previousDiagonal) : 0.0;
        decision.channel = Channel::Zoom;
        decision.zoomStrength = relative * zoomAmount * amount * strengthFactor;
        return decision;
    }

    if (moveAmount <= 0.0 || centreLength < kMinimumChange)
        return decision;
    QPointF scaled = motion.centreDelta * (moveAmount * amount * strengthFactor);
    // Bound the smear. See kMaximumSmearRatio: an unbounded one-frame
    // displacement wipes most of the frame translucent during a fast camera
    // release, which is not the trailing the effect is meant to be.
    const double limit = kMaximumSmearRatio * std::max(motion.diagonal, motion.previousDiagonal);
    const double length = std::hypot(scaled.x(), scaled.y());
    if (limit > 0.0 && length > limit)
        scaled *= limit / length;
    decision.moveVector = scaled;
    decision.moveLength = std::hypot(scaled.x(), scaled.y());
    if (decision.moveLength < kMinimumMoveLength)
        return decision;
    decision.channel = Channel::Move;
    decision.zoomStrength = 0.0;
    return decision;
}

std::vector<QPointF> moveSampleOffsets(const QPointF &vector, int kernel) {
    std::vector<QPointF> offsets;
    if (kernel < 1)
        return offsets;
    // The reference requests offset = -length/2 and then walks
    //     bias vector * i/(kernel-1), i = 0..kernel-1
    // over that shifted range. The centre sample is taken first, then kernel-1
    // more, and the whole sum is divided by kernel — a one-sided distribution,
    // not a symmetric one. Reproduced literally so a slow move smears forward
    // the way the reference does instead of straddling the original position.
    offsets.reserve(kernel);
    const QPointF bias = vector / std::max(1, kernel - 1);
    for (int i = 0; i < kernel; ++i)
        offsets.push_back(bias * static_cast<double>(i));
    return offsets;
}

std::vector<Sample> mergeSamples(const std::vector<QPointF> &offsets) {
    std::vector<Sample> samples;
    if (offsets.empty())
        return samples;
    const double weight = 1.0 / static_cast<double>(offsets.size());
    for (const QPointF &offset : offsets) {
        const int dx = static_cast<int>(std::lround(offset.x()));
        const int dy = static_cast<int>(std::lround(offset.y()));
        auto found = std::find_if(samples.begin(), samples.end(),
            [dx, dy](const Sample &sample) { return sample.dx == dx && sample.dy == dy; });
        if (found != samples.end())
            found->weight += weight;
        else
            samples.push_back(Sample{dx, dy, weight});
    }
    return samples;
}

QPointF subtractParentMotion(const QPointF &child, const QPointF &parent) {
    QPointF result = child - parent;
    // An axis that ends up pointing the other way from the parent is cancelled.
    // Without this a cursor carried along by a panning camera would keep a
    // backwards smear of its own on top of the camera's.
    if (child.x() * parent.x() < 0.0)
        result.setX(0.0);
    if (child.y() * parent.y() < 0.0)
        result.setY(0.0);
    return result;
}

double pixelNoise(int x, int y) { return hashNoise(x, y); }

QImage accumulate(const QImage &layer, const std::vector<Sample> &samples) {
    if (layer.isNull() || samples.empty())
        return layer;
    const QImage source = layer.format() == QImage::Format_ARGB32_Premultiplied
        ? layer : layer.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    QImage result(source.size(), QImage::Format_ARGB32_Premultiplied);
    result.fill(Qt::transparent);

    // Accumulate in floating point: summing premultiplied bytes with integer
    // rounding loses the low bits of every partial sample, which shows up as a
    // dark halo exactly where the blur is strongest.
    const int width = source.width(), height = source.height();
    std::vector<double> sums(static_cast<size_t>(width) * height * 4, 0.0);
    for (const Sample &sample : samples) {
        if (sample.weight <= 0.0)
            continue;
        parallelRows(height, [&](int begin, int end) {
            for (int y = begin; y < end; ++y) {
                const int sy = y + sample.dy;
                if (sy < 0 || sy >= height)
                    continue;
                const QRgb *row = reinterpret_cast<const QRgb *>(source.constScanLine(sy));
                double *out = sums.data() + (static_cast<size_t>(y) * width) * 4;
                for (int x = 0; x < width; ++x) {
                    const int sx = x + sample.dx;
                    if (sx < 0 || sx >= width)
                        continue;
                    const QRgb pixel = row[sx];
                    double *cell = out + static_cast<size_t>(x) * 4;
                    cell[0] += qAlpha(pixel) * sample.weight;
                    cell[1] += qRed(pixel) * sample.weight;
                    cell[2] += qGreen(pixel) * sample.weight;
                    cell[3] += qBlue(pixel) * sample.weight;
                }
            }
        });
    }
    parallelRows(height, [&](int begin, int end) {
        for (int y = begin; y < end; ++y) {
            QRgb *row = reinterpret_cast<QRgb *>(result.scanLine(y));
            const double *in = sums.data() + (static_cast<size_t>(y) * width) * 4;
            for (int x = 0; x < width; ++x) {
                const double *cell = in + static_cast<size_t>(x) * 4;
                row[x] = qRgba(static_cast<int>(std::lround(cell[1])),
                    static_cast<int>(std::lround(cell[2])),
                    static_cast<int>(std::lround(cell[3])),
                    static_cast<int>(std::lround(cell[0])));
            }
        }
    });
    return result;
}

QImage applyMove(const QImage &layer, const QPointF &vector) {
    const double length = std::hypot(vector.x(), vector.y());
    if (layer.isNull() || length < kMinimumMoveLength)
        return layer;
    return accumulate(layer, mergeSamples(moveSampleOffsets(vector)));
}

QImage applyZoom(const QImage &layer, const QPointF &centre, double strength, int maxKernel) {
    if (layer.isNull() || maxKernel < 1 || !(strength > 0.0))
        return layer;
    const QImage source = layer.format() == QImage::Format_ARGB32_Premultiplied
        ? layer : layer.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    const int width = source.width(), height = source.height();
    QImage result(source.size(), QImage::Format_ARGB32_Premultiplied);
    result.fill(Qt::transparent);

    const double radius = std::clamp(strength, 0.0, 1.0);
    std::vector<double> sums(static_cast<size_t>(width) * height * 4, 0.0);
    std::vector<double> weights(static_cast<size_t>(width) * height, 0.0);
    parallelRows(height, [&](int begin, int end) {
        for (int y = begin; y < end; ++y) {
            const QRgb *row = reinterpret_cast<const QRgb *>(source.constScanLine(y));
            for (int x = 0; x < width; ++x) {
                // Sample along the direction to the blur centre, with a stable
                // per-pixel offset so the pattern does not crawl between frames.
                const double dx = centre.x() - x, dy = centre.y() - y;
                const double distance = std::hypot(dx, dy);
                if (distance < 1e-6)
                    continue;
                const double offset = hashNoise(x, y);
                double totalWeight = 0.0;
                for (int t = 0; t < maxKernel; ++t) {
                    const double percent = std::clamp((t + offset) / maxKernel, 0.0, 1.0) * radius;
                    // 4*(p - p²): zero at both ends, peaked in the middle, so the
                    // smear fades out instead of ending in a hard edge.
                    const double weight = 4.0 * (percent - percent * percent);
                    if (weight <= 0.0)
                        continue;
                    const int sx = static_cast<int>(std::lround(x + dx * percent));
                    const int sy = static_cast<int>(std::lround(y + dy * percent));
                    if (sx < 0 || sx >= width || sy < 0 || sy >= height)
                        continue;
                    const QRgb pixel = reinterpret_cast<const QRgb *>(source.constScanLine(sy))[sx];
                    double *cell = sums.data() + (static_cast<size_t>(y) * width + x) * 4;
                    cell[0] += qAlpha(pixel) * weight;
                    cell[1] += qRed(pixel) * weight;
                    cell[2] += qGreen(pixel) * weight;
                    cell[3] += qBlue(pixel) * weight;
                    totalWeight += weight;
                }
                if (totalWeight > 0.0)
                    weights[static_cast<size_t>(y) * width + x] = totalWeight;
            }
        }
    });
    parallelRows(height, [&](int begin, int end) {
        for (int y = begin; y < end; ++y) {
            QRgb *row = reinterpret_cast<QRgb *>(result.scanLine(y));
            for (int x = 0; x < width; ++x) {
                const size_t index = static_cast<size_t>(y) * width + x;
                const double weight = weights[index];
                const double *cell = sums.data() + index * 4;
                if (weight <= 0.0) {
                    // No valid sample (a pixel sitting on the blur centre): keep
                    // the original instead of leaving a transparent hole.
                    row[x] = reinterpret_cast<const QRgb *>(source.constScanLine(y))[x];
                    continue;
                }
                row[x] = qRgba(static_cast<int>(std::lround(cell[1] / weight)),
                    static_cast<int>(std::lround(cell[2] / weight)),
                    static_cast<int>(std::lround(cell[3] / weight)),
                    static_cast<int>(std::lround(cell[0] / weight)));
            }
        }
    });
    return result;
}

} // namespace Render::MotionBlur
