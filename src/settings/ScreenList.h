#pragma once

#include <QObject>
#include <QRectF>
#include <QVariantList>

// Enumerates screens and lets QML place the audience window on a chosen display.
class ScreenList final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList displays READ displays NOTIFY displaysChanged)
    // Bounding rect of every screen in the same logical coordinates the screens
    // report, so the region selector can cover a multi-monitor desktop instead of
    // being trapped on one display.
    Q_PROPERTY(QRectF unionGeometry READ unionGeometry NOTIFY displaysChanged)

public:
    explicit ScreenList(QObject *parent = nullptr);

    QVariantList displays() const;
    QRectF unionGeometry() const;

    // index < 0 keeps the current screen; fullscreen shows it as an exclusive
    // full-screen output, otherwise a normal shareable window.
    Q_INVOKABLE void placeWindowOnDisplay(QObject *window, int index, bool fullscreen);

signals:
    void displaysChanged();
};
