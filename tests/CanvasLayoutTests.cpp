// Layout checks for the rule shared by the preview, the quick screenshot and
// the offline compositor. Before this existed each of the three had its own
// copy of the padding maths and they disagreed
// (docs/项目目标.md §10.1: "padding 一处按画布短边、一处按来源短边").
#include "../src/render/CanvasLayout.h"

#include <QCoreApplication>
#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace Render;

namespace {
void require(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}
bool close(double a, double b, double tolerance = 1e-6) {
    return std::abs(a - b) <= tolerance;
}
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        // --- padding is a percentage of the canvas short side ----------------
        require(close(paddingForCanvas({1920.0, 1080.0}, 10.0), 108.0),
            "padding takes 10% of the canvas short side");
        require(close(paddingForCanvas({1080.0, 1920.0}, 10.0), 108.0),
            "short side, not width");
        require(close(paddingForCanvas({1920.0, 1080.0}, 0.0), 0.0), "zero percent is zero");
        require(paddingForCanvas({1920.0, 1080.0}, 100.0) < 540.0,
            "extreme padding cannot consume the canvas");

        // --- the same percentage measured against a grown canvas ------------
        // p = min(content) * r / (1 - 2r) is the fixed point of "measure the
        // padding against the canvas the content is about to be placed in".
        {
            const double p = paddingForContent({1000.0, 1000.0}, 10.0);
            require(close(p, 125.0), "content-relative padding solves the fixed point");
            const QSizeF canvas(1000.0 + 2 * p, 1000.0 + 2 * p);
            require(close(paddingForCanvas(canvas, 10.0), p),
                "both directions agree once the canvas is known");
        }

        // --- contain, never stretch, never crop -----------------------------
        {
            // 16:10 source into a 16:9 canvas at zero padding.
            CanvasLayoutInput input;
            input.canvas = {1920.0, 1080.0};
            input.content = {2560.0, 1600.0};
            input.paddingPercent = 0.0;
            const CanvasLayout layout = computeCanvasLayout(input);
            require(layout.valid, "layout computes");
            require(close(layout.contentRect.height(), 1080.0), "source fills the short axis");
            require(close(layout.contentRect.width(), 1728.0), "source keeps its aspect ratio");
            require(close(layout.contentRect.x(), 96.0), "source is centred horizontally");
            require(close(layout.contentRect.y(), 0.0), "no vertical slack is left");
        }

        {
            // Portrait canvas: the same source leaves background above and below.
            CanvasLayoutInput input;
            input.canvas = {1080.0, 1920.0};
            input.content = {1920.0, 1080.0};
            input.paddingPercent = 0.0;
            const CanvasLayout layout = computeCanvasLayout(input);
            require(close(layout.contentRect.width(), 1080.0), "source fills the width");
            require(close(layout.contentRect.height(), 607.5), "source keeps its aspect ratio");
            require(close(layout.contentRect.y(), (1920.0 - 607.5) / 2.0), "vertically centred");
        }

        // --- padding shrinks the content box, not the canvas -----------------
        {
            CanvasLayoutInput input;
            input.canvas = {1920.0, 1080.0};
            input.content = {2560.0, 1600.0};
            input.paddingPercent = 10.0;
            const CanvasLayout layout = computeCanvasLayout(input);
            require(close(layout.padding, 108.0), "padding from the canvas short side");
            require(close(layout.contentRect.height(), 864.0), "content fits inside the padding");
            require(close(layout.contentRect.width(), 1382.4), "aspect ratio preserved");
            require(layout.contentRect.top() >= layout.paddedBox.top() - 1e-6
                    && layout.contentRect.bottom() <= layout.paddedBox.bottom() + 1e-6,
                "content stays inside the padded box");
        }

        // --- inset grows the frame around the content ------------------------
        // Documented rule (research/画布与运动模糊.md §1): deduct the inset from
        // all four sides of the available box first, contain the source in that
        // inner box, then grow the frame back by the inset and centre it. An
        // inset therefore also changes the available aspect ratio, which is why
        // the frame does not always span the padded box exactly.
        {
            CanvasLayoutInput input;
            input.canvas = {1920.0, 1080.0};
            input.content = {1920.0, 1080.0};
            input.paddingPercent = 0.0;
            input.inset = 20.0;
            const CanvasLayout layout = computeCanvasLayout(input);
            require(close(layout.innerBox.width(), 1880.0)
                    && close(layout.innerBox.height(), 1040.0),
                "inset is deducted from all four sides of the available box");
            // 1920x1080 contained in 1880x1040 is height-limited.
            require(close(layout.contentRect.height(), 1040.0), "source fills the inner short axis");
            require(close(layout.contentRect.width(), 1920.0 * 1040.0 / 1080.0),
                "source keeps its aspect ratio inside the inset box");
            require(close(layout.frameRect.width(), layout.contentRect.width() + 40.0)
                    && close(layout.frameRect.height(), layout.contentRect.height() + 40.0),
                "frame adds the inset back on both sides");
            require(close(layout.frameRect.center().x(), layout.canvas.center().x())
                    && close(layout.frameRect.center().y(), layout.canvas.center().y()),
                "frame is centred in the canvas");
            require(layout.frameRect.width() <= layout.canvas.width() + 1e-6
                    && layout.frameRect.height() <= layout.canvas.height() + 1e-6,
                "frame never overflows the canvas");
        }

        // --- degenerate input must not produce NaN ---------------------------
        {
            CanvasLayoutInput input;
            input.canvas = {1920.0, 1080.0};
            input.content = {0.0, 0.0};
            require(!computeCanvasLayout(input).valid, "zero content is rejected");
            input.content = {1920.0, 1080.0};
            input.canvas = {0.0, 0.0};
            require(!computeCanvasLayout(input).valid, "zero canvas is rejected");
            input.canvas = {1920.0, 1080.0};
            input.inset = 100000.0;
            const CanvasLayout clamped = computeCanvasLayout(input);
            require(clamped.valid && clamped.inset < 540.0, "inset is clamped to the padded box");
        }

        // --- aspect helpers ---------------------------------------------------
        require(close(aspectValue("16:9", {1.0, 1.0}), 16.0 / 9.0), "named ratio wins");
        require(close(aspectValue("auto", {2560.0, 1600.0}), 1.6), "auto uses the source");
        {
            const QSizeF autoSize = canvasSizeForAspect({2560.0, 1600.0}, "auto");
            require(close(autoSize.width(), 2560.0) && close(autoSize.height(), 1600.0),
                "auto keeps the source size");
            const QSizeF wide = canvasSizeForAspect({2560.0, 1600.0}, "16:9");
            require(close(wide.width() / wide.height(), 16.0 / 9.0, 1e-3), "16:9 canvas ratio");
            require(wide.width() <= 2560.0 && wide.height() <= 1600.0,
                "ratio change only ever crops the canvas, never upscales the source");
            require(std::fmod(wide.width(), 2.0) == 0.0 && std::fmod(wide.height(), 2.0) == 0.0,
                "canvas sides stay even for 4:2:0 encoders");
            const QSizeF tall = canvasSizeForAspect({2560.0, 1600.0}, "9:16");
            require(close(tall.width() / tall.height(), 9.0 / 16.0, 1e-3), "9:16 canvas ratio");
        }

        std::cout << "canvas layout checks passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
