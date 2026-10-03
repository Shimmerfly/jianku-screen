#pragma once

#include <QJsonObject>
#include <QRectF>
#include <QSize>
#include <cstdint>
#include <memory>

class PointerEventRecorder final {
public:
    PointerEventRecorder();
    ~PointerEventRecorder();

    // `displayId` picks the refresh clock for cursor sampling; `pixelSize` and
    // `boundsPoints` describe the rectangle actually being captured. They are
    // passed in rather than derived from the display so window and region sources
    // map global mouse positions onto the frame correctly — assuming the whole
    // display would place every event of a window recording in the wrong spot.
    bool start(std::uint32_t displayId, const QSize &pixelSize, const QRectF &boundsPoints,
        const QString &projectDirectory);
    void pause();
    void resume();
    QJsonObject stop();
    bool active() const;
    QString error() const;

private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
};
