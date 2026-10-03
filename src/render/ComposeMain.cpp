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
        QStringLiteral("不封装任何音轨"));
    const QCommandLineOption noMicrophoneOption(QStringLiteral("no-microphone"),
        QStringLiteral("只封装系统声音，不混入麦克风"));
    const QCommandLineOption blurOption(QStringLiteral("motion-blur"),
        QStringLiteral("覆盖动态模糊总强度（工程默认 0，即关闭）"), QStringLiteral("amount"));
    const QCommandLineOption blurCursorOption(QStringLiteral("motion-blur-cursor"),
        QStringLiteral("覆盖指针分项强度"), QStringLiteral("amount"));
    const QCommandLineOption blurMoveOption(QStringLiteral("motion-blur-move"),
        QStringLiteral("覆盖画面平移分项强度"), QStringLiteral("amount"));
    const QCommandLineOption blurZoomOption(QStringLiteral("motion-blur-zoom"),
        QStringLiteral("覆盖画面缩放分项强度"), QStringLiteral("amount"));
    const QCommandLineOption framesOption(QStringLiteral("frames"),
        QStringLiteral("只合成前 N 帧，用于冒烟验证"), QStringLiteral("n"));
    const QCommandLineOption startOption(QStringLiteral("start-ms"),
        QStringLiteral("从第几毫秒开始合成"), QStringLiteral("ms"), QStringLiteral("0"));
    // Edit timeline. Without any of these the export is the whole recording in real
    // time; these are the non-destructive equivalents of the editor's operations,
    // so a cut can be verified from the command line before the UI exists.
    const QCommandLineOption trimFromOption(QStringLiteral("trim-from"),
        QStringLiteral("裁掉开头这么多毫秒"), QStringLiteral("ms"));
    const QCommandLineOption trimToOption(QStringLiteral("trim-to"),
        QStringLiteral("只保留到这么多毫秒（输出时间）"), QStringLiteral("ms"));
    const QCommandLineOption cutOption(QStringLiteral("cut"),
        QStringLiteral("删掉一段输出时间，格式 起:止（毫秒）"), QStringLiteral("from:to"));
    const QCommandLineOption speedOption(QStringLiteral("speed"),
        QStringLiteral("对一段输出时间变速，格式 起:止:倍率"), QStringLiteral("from:to:rate"));
    const QCommandLineOption systemVolumeOption(QStringLiteral("system-volume"),
        QStringLiteral("系统声音音量（默认取工程里的值）"), QStringLiteral("x"));
    const QCommandLineOption microphoneVolumeOption(QStringLiteral("microphone-volume"),
        QStringLiteral("麦克风音量（默认取工程里的值）"), QStringLiteral("x"));
    parser.addOptions({outputOption, fpsOption, backgroundOption, ffmpegOption, noCursorOption,
        noZoomOption, noAudioOption, noMicrophoneOption, blurOption, blurCursorOption,
        blurMoveOption, blurZoomOption, framesOption, startOption, trimFromOption, trimToOption,
        cutOption, speedOption, systemVolumeOption, microphoneVolumeOption});
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
    options.includeMicrophone = !parser.isSet(noMicrophoneOption);
    options.maxOutputFrames = parser.isSet(framesOption) ? parser.value(framesOption).toInt() : 0;
    options.startMs = parser.value(startOption).toDouble();
    // Unset blur options stay negative, which means "keep the project's value".
    auto blurValue = [&](const QCommandLineOption &option) {
        if (!parser.isSet(option))
            return -1.0;
        bool valid = false;
        const double value = parser.value(option).toDouble(&valid);
        return valid && value >= 0.0 ? value : -1.0;
    };
    options.motionBlur.amount = blurValue(blurOption);
    options.motionBlur.cursorAmount = blurValue(blurCursorOption);
    options.motionBlur.screenMoveAmount = blurValue(blurMoveOption);
    options.motionBlur.screenZoomAmount = blurValue(blurZoomOption);
    // The strength factor is fps / 60 relative to the reference's 60 fps, so the
    // export frame rate decides it rather than a project setting.
    options.motionBlur.fps = fps;

    // Edit operations, applied by the compositor once it knows how long the
    // recording is. Doing it there keeps the CLI from having to load the project
    // twice, and keeps the ordering rule in one place.
    auto numberPair = [](const QString &value, double *first, double *second) {
        const QStringList parts = value.split(QLatin1Char(':'));
        if (parts.size() != 2)
            return false;
        bool okFirst = false, okSecond = false;
        *first = parts[0].toDouble(&okFirst);
        *second = parts[1].toDouble(&okSecond);
        return okFirst && okSecond;
    };

    if (parser.isSet(speedOption)) {
        const QStringList parts = parser.value(speedOption).split(QLatin1Char(':'));
        bool okFrom = false, okTo = false, okRate = false;
        double from = 0.0, to = 0.0, rate = 0.0;
        if (parts.size() == 3) {
            from = parts[0].toDouble(&okFrom);
            to = parts[1].toDouble(&okTo);
            rate = parts[2].toDouble(&okRate);
        }
        if (!okFrom || !okTo || !okRate || !(rate > 0.0) || to <= from) {
            QTextStream(stderr) << "无效的 --speed（应为 起:止:倍率，倍率大于 0）："
                                << parser.value(speedOption) << '\n';
            return 2;
        }
        options.edits.push_back({Render::EditKind::Speed, from, to, rate});
    }
    if (parser.isSet(cutOption)) {
        double from = 0.0, to = 0.0;
        if (!numberPair(parser.value(cutOption), &from, &to) || to <= from || from < 0.0) {
            QTextStream(stderr) << "无效的 --cut（应为 起:止，起小于止）："
                                << parser.value(cutOption) << '\n';
            return 2;
        }
        options.edits.push_back({Render::EditKind::Cut, from, to, 0.0});
    }
    if (parser.isSet(systemVolumeOption)) {
        bool ok = false;
        const double value = parser.value(systemVolumeOption).toDouble(&ok);
        if (!ok || value < 0.0) {
            QTextStream(stderr) << "无效的 --system-volume：" << parser.value(systemVolumeOption) << '\n';
            return 2;
        }
        options.systemAudioVolume = value;
    }
    if (parser.isSet(microphoneVolumeOption)) {
        bool ok = false;
        const double value = parser.value(microphoneVolumeOption).toDouble(&ok);
        if (!ok || value < 0.0) {
            QTextStream(stderr) << "无效的 --microphone-volume：" << parser.value(microphoneVolumeOption) << '\n';
            return 2;
        }
        options.microphoneVolume = value;
    }
    if (parser.isSet(trimFromOption)) {
        bool ok = false;
        const double value = parser.value(trimFromOption).toDouble(&ok);
        if (!ok || value < 0.0) {
            QTextStream(stderr) << "无效的 --trim-from：" << parser.value(trimFromOption) << '\n';
            return 2;
        }
        options.edits.push_back({Render::EditKind::TrimStart, value, 0.0, 0.0});
    }
    if (parser.isSet(trimToOption)) {
        bool ok = false;
        const double value = parser.value(trimToOption).toDouble(&ok);
        if (!ok || value <= 0.0) {
            QTextStream(stderr) << "无效的 --trim-to：" << parser.value(trimToOption) << '\n';
            return 2;
        }
        options.edits.push_back({Render::EditKind::TrimEnd, value, 0.0, 0.0});
    }

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
        << "帧数：" << result.writtenFrames << "（成片回读 " << result.encodedFrames
        << "，源时间轴 " << result.sourceFrames << " 帧）\n"
        << "时长：" << QString::number(result.durationMs / 1000.0, 'f', 3) << " 秒"
        << (result.timelineIdentity
               ? QString()
               : QStringLiteral("（剪辑后，%1 段）").arg(result.timelineSegments))
        << '\n' 
        << "音轨：" << (result.microphoneMuxed
               ? QStringLiteral("系统声音 + 麦克风（已按 %1 ms 对齐后混音）")
                     .arg(QString::number(result.microphoneDelayMs, 'f', 1))
               : result.audioMuxed ? QStringLiteral("仅系统声音（直接复制，未重编码）")
                                   : QStringLiteral("无"))
        << '\n'
        << "耗时：" << QString::number(seconds, 'f', 1) << " 秒（"
        << QString::number(result.writtenFrames / std::max(0.001, seconds), 'f', 1) << " 帧/秒）\n";
    return 0;
}
