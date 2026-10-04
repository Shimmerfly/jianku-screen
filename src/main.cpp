#include "animation/AnimationDriver.h"
#include "mac/MacCapture.h"
#include "mac/QuickScreenshot.h"
#include "mac/GlobalHotkey.h"
#include "mac/MacWindowStyle.h"
#include "project/RecentProjects.h"
#include "render/CanvasPreview.h"
#include "render/ExportController.h"
#include "render/TimelineController.h"
#include "render/VideoSurface.h"
#include "settings/BackgroundLibrary.h"
#include "settings/BrandAssets.h"
#include "settings/ScreenList.h"
#include "settings/SettingsStore.h"

#include <QApplication>
#include <QAction>
#include <QMenu>
#include <QIcon>
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
    // The window icon: on macOS this is what the About panel and any window list
    // show, and it falls back to the same source as the tray icon.
    app.setWindowIcon(Branding::mark());
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
    Render::ExportController exporter;
    Render::CanvasPreview canvasPreview;
    BrandAssets brand;
    Render::TimelineController timeline;
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
    engine.rootContext()->setContextProperty(QStringLiteral("exporter"), &exporter);
    engine.rootContext()->setContextProperty(QStringLiteral("canvasPreview"), &canvasPreview);
    engine.rootContext()->setContextProperty(QStringLiteral("timeline"), &timeline);
    engine.rootContext()->setContextProperty(QStringLiteral("brand"), &brand);
    // The exporter always targets the project the user just recorded, and exports
    // the edit timeline as it stands. The two are wired together here so an export
    // started from the UI can never use a stale timeline: the controller pushes
    // every change into the exporter.
    QObject::connect(&capture, &MacCapture::lastRecordingPathChanged, &exporter, [&] {
        exporter.setProjectDirectory(capture.lastProjectPath());
    });
    QObject::connect(&capture, &MacCapture::lastRecordingPathChanged, &timeline, [&] {
        timeline.load(capture.lastProjectPath());
    });
    QObject::connect(&timeline, &Render::TimelineController::timelineChanged, &exporter, [&] {
        exporter.setTimeline(timeline.timeline());
    });
    // The scan needs the recording directory, which lives in the settings — the
    // recorder's own copy of them is only filled in when a recording starts, so a
    // fresh launch would otherwise search the default directory even after the user
    // changed it.
    auto refreshRecent = [&] {
        capture.setRecordingSettings(settings.current());
        capture.refreshRecentProjects();
    };
    QObject::connect(&settings, &SettingsStore::currentChanged, &capture, refreshRecent);
    refreshRecent();
    if (capture.lastProjectPath().isEmpty())
        capture.openProject(RecentProjects::mostRecent(RecentProjects::recordingDirectory(
            settings.current().value("recordingDirectory").toString())));
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

    // The menu bar mark is a template image, like every other macOS status item: a
    // single-colour glyph that the system paints black or white to match the menu bar.
    // The coloured app icon was tried here first and is wrong for the spot — it is a
    // small picture among monochrome symbols, and on a dark menu bar its dark tile
    // nearly disappears against the bar.
    const QIcon trayIcon = Branding::menuBarIcon();
    QSystemTrayIcon tray{trayIcon};
    QMenu trayMenu;
    QAction *openAction = trayMenu.addAction(QStringLiteral("打开简库镜传"));
    QAction *screenshotAction = trayMenu.addAction(QStringLiteral("立即截图"));
    trayMenu.addSeparator();
    // The About panel is where macOS shows the application icon at its largest, and
    // it is the one place a user looks to check which build they are running.
    QAction *aboutAction = trayMenu.addAction(QStringLiteral("关于简库镜传"));
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
    QObject::connect(aboutAction, &QAction::triggered, &app, [] { showAboutPanel(); });
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
    tray.setIcon(trayIcon);
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
