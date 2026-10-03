#include "animation/AnimationDriver.h"
#include "mac/MacCapture.h"
#include "mac/QuickScreenshot.h"
#include "mac/GlobalHotkey.h"
#include "mac/MacWindowStyle.h"
#include "render/VideoSurface.h"
#include "settings/BackgroundLibrary.h"
#include "settings/ScreenList.h"
#include "settings/SettingsStore.h"

#include <QApplication>
#include <QAction>
#include <QMenu>
#include <QPainter>
#include <QPixmap>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QQuickStyle>
#include <QSGRendererInterface>
#include <QTimer>
#include <QSystemTrayIcon>

int main(int argc, char *argv[]) {
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Metal);
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("Jianku Screen"));
    app.setOrganizationName(QStringLiteral("Jianku"));
    app.setQuitOnLastWindowClosed(false);

    qmlRegisterType<VideoSurface>("Jianku.Screen", 1, 0, "VideoSurface");
    MacCapture capture;
    SettingsStore settings;
    BackgroundLibrary backgrounds;
    ScreenList screens;
    AnimationDriver anim;
    QuickScreenshot screenshot;
    GlobalHotkey hotkey;
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("capture"), &capture);
    engine.rootContext()->setContextProperty(QStringLiteral("settings"), &settings);
    engine.rootContext()->setContextProperty(QStringLiteral("backgrounds"), &backgrounds);
    engine.rootContext()->setContextProperty(QStringLiteral("screens"), &screens);
    engine.rootContext()->setContextProperty(QStringLiteral("anim"), &anim);
    // addImageProvider takes ownership; allocate on the heap.
    engine.addImageProvider(QStringLiteral("cursor"), new CursorImageProvider(&anim));
    engine.rootContext()->setContextProperty(QStringLiteral("screenshot"), &screenshot);
    engine.rootContext()->setContextProperty(QStringLiteral("hotkey"), &hotkey);
    engine.loadFromModule("Jianku.Screen", "Main");
    if (engine.rootObjects().isEmpty())
        return 1;
    auto *mainWindow = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    if (!mainWindow) return 1;
    applyDarkWindowStyle(mainWindow);
    if (auto *audienceWindow = mainWindow->findChild<QQuickWindow *>(QStringLiteral("audienceWindow"))) {
        QObject::connect(audienceWindow, &QWindow::visibleChanged, audienceWindow,
                         [audienceWindow](bool visible) {
                             if (visible) applyDarkWindowStyle(audienceWindow);
                         });
    }

    QObject::connect(&settings, &SettingsStore::currentChanged, &hotkey, [&] {
        hotkey.setShortcut(settings.current().value("screenshotHotkey").toString());
    });
    hotkey.setShortcut(settings.current().value("screenshotHotkey").toString());
    QObject::connect(&hotkey, &GlobalHotkey::activated, &screenshot, [&] {
        screenshot.capture(settings.current());
    });
    QObject::connect(&app, &QGuiApplication::lastWindowClosed,
                     &capture, &MacCapture::stop);

    QPixmap iconImage(32, 32);
    iconImage.fill(Qt::transparent);
    {
        QPainter painter(&iconImage);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setBrush(QColor("#e8edf3"));
        painter.setPen(Qt::NoPen);
        painter.drawRoundedRect(QRectF(3, 6, 26, 20), 6, 6);
        painter.setBrush(QColor("#263747"));
        painter.drawEllipse(QRectF(11, 10, 10, 10));
    }
    QSystemTrayIcon tray{QIcon(iconImage)};
    QMenu trayMenu;
    QAction *openAction = trayMenu.addAction(QStringLiteral("打开简库镜传"));
    QAction *screenshotAction = trayMenu.addAction(QStringLiteral("立即截图"));
    trayMenu.addSeparator();
    QAction *quitAction = trayMenu.addAction(QStringLiteral("退出"));
    QObject::connect(openAction, &QAction::triggered, mainWindow, [mainWindow] {
        mainWindow->show();
        mainWindow->raise();
        mainWindow->requestActivate();
    });
    QObject::connect(screenshotAction, &QAction::triggered, &screenshot, [&] {
        screenshot.capture(settings.current());
    });
    QObject::connect(quitAction, &QAction::triggered, &app, &QApplication::quit);
    QObject::connect(&tray, &QSystemTrayIcon::activated, mainWindow,
        [mainWindow](QSystemTrayIcon::ActivationReason reason) {
            if (reason == QSystemTrayIcon::DoubleClick) {
                mainWindow->show();
                mainWindow->raise();
                mainWindow->requestActivate();
            }
        });
    tray.setContextMenu(&trayMenu);
    tray.setToolTip(QStringLiteral("简库镜传 · 常驻截图"));
    tray.show();
    QObject::connect(&screenshot, &QuickScreenshot::statusChanged, &tray, [&] {
        if (!screenshot.status().isEmpty() && screenshot.status() != QStringLiteral("正在截图…"))
            tray.showMessage(QStringLiteral("简库镜传截图"), screenshot.status());
    });
    // Probe capture access unconditionally; success is treated as authorised.
    QTimer::singleShot(0, &capture, &MacCapture::refreshDisplays);
    return app.exec();
}
