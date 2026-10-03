// Offline compositor entry point.
//
//   jianku-compose <project-directory> [options]
//
// Replays a recorded project through the shared animation engine and the shared
// canvas layout, and writes a composited MP4. This is the first code path that
// actually consumes the recorded pointer timeline, cursor shapes and auto-zoom
// ranges — before it existed those files were written and never read, so the
// recorded MP4 contained neither the pointer nor the camera (docs/项目目标.md §10.1).
#include "ProjectCompositor.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QTextStream>
#include <cstdio>

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("jianku-compose"));
    app.setApplicationVersion(QStringLiteral("0.1.0"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral(
        "简库镜传离线合成器：把已录制的工程目录合成带指针与镜头的 MP4。"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("project"),
        QStringLiteral("录制工程目录（含 project.json 的 .jianku 目录）"));

    const QCommandLineOption outputOption({QStringLiteral("o"), QStringLiteral("output")},
        QStringLiteral("输出文件，默认 <工程目录>/composed.mp4"), QStringLiteral("path"));
    const QCommandLineOption fpsOption(QStringLiteral("fps"),
        QStringLiteral("输出帧率，默认 60"), QStringLiteral("n"), QStringLiteral("60"));
    const QCommandLineOption backgroundOption(QStringLiteral("background-root"),
        QStringLiteral("内置背景图根目录，默认取应用资源或源码 assets/backgrounds"),
        QStringLiteral("dir"));
    const QCommandLineOption ffmpegOption(QStringLiteral("ffmpeg"),
        QStringLiteral("ffmpeg 可执行文件路径，默认从 PATH 查找"), QStringLiteral("path"),
        QStringLiteral("ffmpeg"));
    const QCommandLineOption noCursorOption(QStringLiteral("no-cursor"),
        QStringLiteral("不合成指针（用于对照）"));
    const QCommandLineOption noZoomOption(QStringLiteral("no-auto-zoom"),
        QStringLiteral("不使用自动缩放区间（用于对照）"));
    const QCommandLineOption noAudioOption(QStringLiteral("no-audio"),
        QStringLiteral("不封装系统声音轨"));
    const QCommandLineOption framesOption(QStringLiteral("frames"),
        QStringLiteral("只合成前 N 帧，用于冒烟验证"), QStringLiteral("n"));
    const QCommandLineOption startOption(QStringLiteral("start-ms"),
        QStringLiteral("从第几毫秒开始合成"), QStringLiteral("ms"), QStringLiteral("0"));
    parser.addOptions({outputOption, fpsOption, backgroundOption, ffmpegOption, noCursorOption,
        noZoomOption, noAudioOption, framesOption, startOption});
    parser.process(app);

    const QStringList positional = parser.positionalArguments();
    if (positional.isEmpty()) {
        QTextStream(stderr) << "缺少工程目录。用 --help 查看用法。\n";
        return 2;
    }
    const QString directory = QFileInfo(positional.first()).absoluteFilePath();
    if (!QFileInfo::exists(directory + QStringLiteral("/project.json"))) {
        QTextStream(stderr) << "不是录制工程目录（找不到 project.json）：" << directory << '\n';
        return 2;
    }

    bool fpsOk = false;
    const int fps = parser.value(fpsOption).toInt(&fpsOk);
    if (!fpsOk || fps <= 0) {
        QTextStream(stderr) << "无效的帧率：" << parser.value(fpsOption) << '\n';
        return 2;
    }

    Render::ComposeOptions options;
    options.projectDirectory = directory;
    options.outputPath = parser.value(outputOption);
    options.ffmpegPath = parser.value(ffmpegOption);
    options.backgroundRoot = parser.value(backgroundOption);
    options.fps = fps;
    options.includeCursor = !parser.isSet(noCursorOption);
    options.includeAutoZoom = !parser.isSet(noZoomOption);
    options.includeAudio = !parser.isSet(noAudioOption);
    options.maxOutputFrames = parser.isSet(framesOption) ? parser.value(framesOption).toInt() : 0;
    options.startMs = parser.value(startOption).toDouble();

    QElapsedTimer timer;
    timer.start();
    qint64 lastReported = -1;
    const Render::ComposeResult result = Render::composeProject(options,
        [&](qint64 done, qint64 total) {
            if (done == lastReported)
                return;
            lastReported = done;
            const double percent = total > 0 ? 100.0 * done / total : 0.0;
            QTextStream(stderr) << QStringLiteral("\r合成中 %1/%2 帧 (%3%)")
                .arg(done).arg(total).arg(percent, 0, 'f', 1).toUtf8().constData();
        });
    QTextStream(stderr) << "\r" << QString(60, ' ') << "\r";

    if (!result.ok) {
        QTextStream(stderr) << "合成失败：" << result.error << '\n';
        if (!result.encoderLog.isEmpty())
            QTextStream(stderr) << "编码器输出：" << result.encoderLog << '\n';
        if (!result.decoderLog.isEmpty())
            QTextStream(stderr) << "解码器输出：" << result.decoderLog << '\n';
        return 1;
    }

    const double seconds = timer.elapsed() / 1000.0;
    QTextStream(stdout)
        << "输出：" << result.outputPath << '\n'
        << "画布：" << result.width << "×" << result.height << '\n'
        << "帧数：" << result.writtenFrames << "（源时间轴 " << result.sourceFrames << " 帧）\n"
        << "时长：" << QString::number(result.durationMs / 1000.0, 'f', 3) << " 秒\n"
        << "音轨：" << (result.audioMuxed ? "已封装系统声音（直接复制，未重编码）" : "无") << '\n'
        << "耗时：" << QString::number(seconds, 'f', 1) << " 秒（"
        << QString::number(result.writtenFrames / std::max(0.001, seconds), 'f', 1) << " 帧/秒）\n";
    return 0;
}
