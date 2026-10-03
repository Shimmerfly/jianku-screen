#pragma once

#include <QJsonObject>
#include <QSize>
#include <cstdint>
#include <memory>

class PointerEventRecorder final {
public:
    PointerEventRecorder();
    ~PointerEventRecorder();

    bool start(std::uint32_t displayId, const QSize &pixelSize, const QString &projectDirectory);
    void pause();
    void resume();
    QJsonObject stop();
    bool active() const;
    QString error() const;

private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
};
