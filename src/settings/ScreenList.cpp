#include "ScreenList.h"

#include <QGuiApplication>
#include <QQuickWindow>
#include <QScreen>

ScreenList::ScreenList(QObject *parent) : QObject(parent) {
    connect(qApp, &QGuiApplication::screenAdded, this, &ScreenList::displaysChanged);
    connect(qApp, &QGuiApplication::screenRemoved, this, &ScreenList::displaysChanged);
}

QVariantList ScreenList::displays() const {
    QVariantList list;
    int index = 0;
    for (QScreen *screen : QGuiApplication::screens()) {
        const QRect geometry = screen->geometry();
        list.append(QVariantMap{
            {"index", index++},
            {"name", screen->name()},
            {"width", geometry.width()},
            {"height", geometry.height()},
            {"primary", screen == QGuiApplication::primaryScreen()}});
    }
    return list;
}

void ScreenList::placeWindowOnDisplay(QObject *window, int index, bool fullscreen) {
    auto *quickWindow = qobject_cast<QQuickWindow *>(window);
    if (!quickWindow) {
        if (window)
            qWarning("placeWindowOnDisplay: not a window");
        return;
    }
    const QList<QScreen *> screens = QGuiApplication::screens();
    if (index >= 0 && index < screens.size())
        quickWindow->setScreen(screens[index]);
    if (fullscreen) {
        quickWindow->showFullScreen();
    } else {
        quickWindow->showNormal();
        quickWindow->show();
        quickWindow->raise();
    }
}
