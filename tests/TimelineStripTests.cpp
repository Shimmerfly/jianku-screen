// Edit-strip QML checks.
//
// The strip is only exercised once a project is loaded: without one it hides, so an
// ordinary app launch evaluates almost none of its bindings. A broken binding in QML
// prints a warning and renders nothing rather than failing to load, which is exactly
// how a timeline ends up silently blank — so the test instantiates the real
// component against a real controller, in a window so the bindings are actually
// evaluated, and fails on any QML warning.
//
// What this does *not* catch, measured rather than assumed: reading a property that
// does not exist (`timeline.playheadLabelTypo`) produces no warning at all in Qt
// 6.11 — it evaluates to undefined. Only a call of a missing *function*, reached at
// run time, warns. So this test guards the bindings that do run, and the rest of the
// strip's correctness rests on the controller tests, which have the arithmetic.
#include "../src/render/TimelineController.h"

#include <QGuiApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QTimer>
#include <iostream>
#include <stdexcept>

using namespace Render;

namespace {
void require(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}

QStringList warnings;
void collectMessages(QtMsgType type, const QMessageLogContext &, const QString &message) {
    if (type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg)
        warnings.append(message);
}

// A project the controller will accept: a manifest, the video it names, a frame
// timeline, and the (possibly empty) event timelines the loader insists on.
bool writeProject(const QString &directory, double durationMs) {
    if (!QDir().mkpath(directory))
        return false;
    for (const char *name : {"/raw.mp4", "/pointer-timeline.jsonl", "/cursor-timeline.jsonl"}) {
        QFile file(directory + QString::fromLatin1(name));
        if (!file.open(QIODevice::WriteOnly))
            return false;
        if (QString::fromLatin1(name) == QStringLiteral("/raw.mp4"))
            file.write("placeholder");
    }
    QFile frames(directory + "/video-frames.jsonl");
    if (!frames.open(QIODevice::WriteOnly))
        return false;
    for (int i = 0; i < 10; ++i) {
        const QJsonObject row{{"mediaTimeNs", QString::number(qint64(durationMs * 1e6) * i / 10)},
            {"displayHostTimeNs", QString::number(1000 + qint64(durationMs * 1e6) * i / 10)},
            {"displayTimeAvailable", true}};
        frames.write(QJsonDocument(row).toJson(QJsonDocument::Compact) + '\n');
    }
    frames.close();
    QFile manifest(directory + "/project.json");
    if (!manifest.open(QIODevice::WriteOnly))
        return false;
    const QJsonObject object{
        {"schemaVersion", 1},
        {"source", QJsonObject{{"type", "display"}, {"widthPx", 1280}, {"heightPx", 720}}},
        {"settings", QJsonObject{{"outputAspectRatio", "auto"}}},
        {"video", QJsonObject{{"file", "raw.mp4"},
            {"durationNs", QString::number(qint64(durationMs * 1e6))},
            {"mediaZeroHostTimeNs", "1000"}, {"frameCount", 10}}}};
    const QByteArray bytes = QJsonDocument(object).toJson();
    return manifest.write(bytes) == bytes.size();
}

// The strip lives inside Main.qml, which needs every context property the app
// registers. Rather than reproduce them all, the component is instantiated on its
// own with the one property it actually uses.
const char *kStripSource = R"QML(
import QtQuick
import Jianku.Screen

Item {
    width: 900
    height: 200
    TimelineStrip {
        id: strip
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: 74
    }
    property alias stripReady: strip.ready
}
)QML";
} // namespace

int main(int argc, char **argv) {
    // The app uses the Basic style because the native macOS style refuses the custom
    // background and contentItem every control here relies on. Matching it keeps the
    // harness from reporting style warnings the real app never produces.
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    // A GUI application, not a core one: the QML engine creates items that expect a
    // window system to be initialised, and without it the harness segfaults inside
    // Qt before any check runs.
    QGuiApplication app(argc, argv);
    // Qt's default handler prints to stderr; installing one lets the test treat a
    // QML binding error as a failure instead of a line of log nobody reads.
    qInstallMessageHandler(collectMessages);
    try {
        QTemporaryDir temp;
        require(temp.isValid(), "temporary directory");
        const QString project = temp.path() + "/strip.jianku";
        require(writeProject(project, 20000.0), "fixture project");

        TimelineController controller;
        require(controller.load(project), "controller loads the fixture");
        controller.setFrameRate(60.0);
        controller.setPlayheadRatio(0.4);

        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("timeline"), &controller);
        // The module is compiled into this test binary, so the default qrc path
        // resolves without any import path setup.

        QQmlComponent component(&engine);
        component.setData(QByteArray(kStripSource), QUrl(QStringLiteral("qrc:/strip-harness.qml")));
        if (component.isError()) {
            for (const QQmlError &error : component.errors())
                std::cerr << "QML 错误: " << error.toString().toStdString() << '\n';
            throw std::runtime_error("harness 组件加载失败");
        }
        QObject *root = component.create();
        require(root != nullptr, "harness 组件实例化成功");
        auto *item = qobject_cast<QQuickItem *>(root);
        require(item != nullptr, "根对象是 Item");

        // The populated branch has to have really run: a strip that renders nothing
        // is exactly the failure this test exists to catch.
        require(!controller.rulerTicks().isEmpty(), "the ruler produced ticks");
        require(!controller.segments().isEmpty(), "the strip has segments");
        require(root->property("stripReady").toBool(), "the strip reports itself ready");

        // A window is needed for two reasons. Delegates are only created once the
        // item is polished/rendered, and a binding error is reported when the binding
        // is evaluated rather than when the file loads — without this, a broken
        // binding passes silently, which is the whole failure mode being tested for.
        QQuickWindow window;
        window.resize(900, 200);
        item->setParentItem(window.contentItem());
        item->setWidth(900);
        item->setHeight(200);
        window.show();
        for (int i = 0; i < 30; ++i) {
            QCoreApplication::processEvents();
            window.update();
        }
        window.hide();

        // Now exercise the state changes the strip has to survive.
        controller.splitAtPlayhead();
        controller.removeAroundPlayhead(1000.0);
        controller.undo();
        controller.redo();
        controller.reset();
        controller.clear();
        for (int i = 0; i < 10; ++i)
            QCoreApplication::processEvents();

        // A hidden strip still must not error, but the interesting case is populated.
        require(controller.rulerTicks().isEmpty(),
            "clearing the project empties the ruler, so the strip hides");

        qInstallMessageHandler(nullptr);
        if (!warnings.isEmpty()) {
            for (const QString &message : warnings)
                std::cerr << "QML: " << message.toStdString() << '\n';
            throw std::runtime_error("QML 输出了告警（见上）");
        }

        std::cout << "timeline strip checks passed\n";
        return 0;
    } catch (const std::exception &error) {
        qInstallMessageHandler(nullptr);
        std::cerr << error.what() << '\n';
        return 1;
    }
}
