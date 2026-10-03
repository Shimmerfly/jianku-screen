#pragma once

#include <QObject>
#include <QVariantList>

// Enumerates screens and lets QML place the audience window on a chosen display.
class ScreenList final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList displays READ displays NOTIFY displaysChanged)

public:
    explicit ScreenList(QObject *parent = nullptr);

    QVariantList displays() const;

    // index < 0 keeps the current screen; fullscreen shows it as an exclusive
    // full-screen output, otherwise a normal shareable window.
    Q_INVOKABLE void placeWindowOnDisplay(QObject *window, int index, bool fullscreen);

signals:
    void displaysChanged();
};
