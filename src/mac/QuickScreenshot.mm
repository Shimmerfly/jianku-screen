#import "QuickScreenshot.h"

#import <ScreenCaptureKit/ScreenCaptureKit.h>
#import <CoreGraphics/CoreGraphics.h>

#include "../render/CanvasRenderer.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QMetaObject>
#include <QPainter>
#include <QPointer>
#include <QDebug>
#include <QStandardPaths>
#include <algorithm>

namespace {
QImage imageFromCG(CGImageRef source) {
    if (!source) return {};
    const int width = static_cast<int>(CGImageGetWidth(source));
    const int height = static_cast<int>(CGImageGetHeight(source));
    QImage image(width, height, QImage::Format_ARGB32_Premultiplied);
    if (image.isNull()) return {};
    CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB();
    CGContextRef context = CGBitmapContextCreate(image.bits(), width, height, 8,
        image.bytesPerLine(), colorSpace,
        kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Host);
    CGColorSpaceRelease(colorSpace);
    if (!context) return {};
    CGContextDrawImage(context, CGRectMake(0, 0, width, height), source);
    CGContextRelease(context);
    return image;
}

// Composes the screenshot through the same CanvasRenderer the preview and the
// offline compositor use. Before this it had its own copy of the padding maths and
// knew only about a flat colour, so choosing a gradient or a built-in background
// produced a screenshot that looked nothing like the preview.
QString saveComposed(const QImage &source, const QVariantMap &options, const QString &backgroundRoot) {
    if (source.isNull()) return QStringLiteral("截图为空");
    const Render::CanvasStyle style = Render::canvasStyleFromMap(options, backgroundRoot);
    // No canvas size: the image grows to make room for the padding, which is the
    // one place the layout rule is solved from the content instead of the canvas.
    const Render::CanvasPlan plan = Render::planCanvas(style, QSizeF(),
        QSizeF(source.width(), source.height()));
    if (!plan.valid) return QStringLiteral("截图合成失败：") + plan.error;

    QImage result(plan.width(), plan.height(), QImage::Format_ARGB32_Premultiplied);
    result.fill(Qt::transparent);
    QPainter painter(&result);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    Render::drawCanvasBackdrop(painter, plan);
    Render::beginFrame(painter, plan, QColor(QString::fromLatin1(Render::kFrameColor)));
    const QRectF content = plan.layout.contentRect.translated(-plan.layout.frameRect.topLeft());
    painter.drawImage(content, source);
    Render::drawInsetBorder(painter, plan);
    Render::endFrame(painter, plan);
    painter.end();

    QString directory = options.value("screenshotDirectory").toString();
    if (directory.isEmpty())
        directory = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation)
                    + "/Jianku Screen";
    if (!QDir().mkpath(directory)) return QStringLiteral("无法创建截图目录：") + directory;
    const QString fileName = directory + "/Jianku Screen "
        + QDateTime::currentDateTime().toString("yyyy-MM-dd HH-mm-ss-zzz") + ".png";
    return result.save(fileName, "PNG") ? fileName : QStringLiteral("截图保存失败：") + fileName;
}
} // namespace

QuickScreenshot::QuickScreenshot(QObject *parent) : QObject(parent) {}

void QuickScreenshot::setStatus(const QString &status) {
    status_ = status;
    emit statusChanged();
}

void QuickScreenshot::capture(const QVariantMap &options) {
    qInfo() << "Jianku screenshot requested";
    setStatus(QStringLiteral("正在截图…"));
    QPointer<QuickScreenshot> self(this);
    const QVariantMap frozenOptions = options;
    [SCShareableContent getShareableContentExcludingDesktopWindows:NO
        onScreenWindowsOnly:NO
        completionHandler:^(SCShareableContent *content, NSError *error) {
            if (error || content.displays.count == 0) {
                const QString message = error ? QString::fromNSString(error.localizedDescription)
                                              : QStringLiteral("没有可截图的显示器");
                QMetaObject::invokeMethod(qApp, [self, message] {
                    if (self) self->setStatus(QStringLiteral("截图失败：") + message);
                }, Qt::QueuedConnection);
                return;
            }
            SCDisplay *display = content.displays.firstObject;
            NSMutableArray<SCRunningApplication *> *excluded = [NSMutableArray array];
            NSString *bundleId = NSBundle.mainBundle.bundleIdentifier;
            for (SCRunningApplication *app in content.applications)
                if ([app.bundleIdentifier isEqualToString:bundleId]) [excluded addObject:app];
            SCContentFilter *filter = [[SCContentFilter alloc] initWithDisplay:display
                excludingApplications:excluded exceptingWindows:@[]];
            SCStreamConfiguration *config = [[SCStreamConfiguration alloc] init];
            CGDisplayModeRef mode = CGDisplayCopyDisplayMode(display.displayID);
            config.width = mode ? CGDisplayModeGetPixelWidth(mode) : display.width;
            config.height = mode ? CGDisplayModeGetPixelHeight(mode) : display.height;
            if (mode) CGDisplayModeRelease(mode);
            config.showsCursor = NO;
            config.pixelFormat = kCVPixelFormatType_32BGRA;
            [SCScreenshotManager captureImageWithFilter:filter configuration:config
                completionHandler:^(CGImageRef image, NSError *captureError) {
                    const QString result = captureError
                        ? QStringLiteral("截图失败：") + QString::fromNSString(captureError.localizedDescription)
                        : saveComposed(imageFromCG(image), frozenOptions,
                              QStringLiteral(JIANKU_SOURCE_DIR "/assets/backgrounds"));
                    qInfo() << "Jianku screenshot result" << result;
                    QMetaObject::invokeMethod(qApp, [self, result] {
                        if (self) self->setStatus(result);
                    }, Qt::QueuedConnection);
                }];
        }];
}
