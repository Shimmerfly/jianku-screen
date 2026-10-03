#import "QuickScreenshot.h"

#import <ScreenCaptureKit/ScreenCaptureKit.h>
#import <CoreGraphics/CoreGraphics.h>

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QMetaObject>
#include <QPainter>
#include <QPainterPath>
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

QString saveComposed(const QImage &source, const QVariantMap &options) {
    if (source.isNull()) return QStringLiteral("截图为空");
    const double ratio = std::clamp(options.value("backgroundPaddingRatio").toDouble(), 0.0, 40.0) / 100.0;
    const int padding = qRound(std::min(source.width(), source.height()) * ratio / (1.0 - 2.0 * ratio));
    QImage result(source.width() + 2 * padding, source.height() + 2 * padding,
                  QImage::Format_ARGB32_Premultiplied);
    result.fill(QColor(options.value("backgroundColor", "#1c2630").toString()));
    QPainter painter(&result);
    painter.setRenderHint(QPainter::Antialiasing);
    if (options.value("backgroundType").toString() == "image") {
        QImage background(options.value("backgroundImagePath").toString());
        if (!background.isNull()) {
            const QSize size = background.size().scaled(result.size(), Qt::KeepAspectRatioByExpanding);
            const QRect target((result.width() - size.width()) / 2,
                               (result.height() - size.height()) / 2,
                               size.width(), size.height());
            painter.drawImage(target, background);
        }
    }
    QPainterPath rounded;
    const QRectF inner(padding, padding, source.width(), source.height());
    rounded.addRoundedRect(inner, options.value("windowBorderRadius").toDouble(),
                           options.value("windowBorderRadius").toDouble());
    painter.setClipPath(rounded);
    painter.drawImage(inner.topLeft(), source);
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
                        : saveComposed(imageFromCG(image), frozenOptions);
                    qInfo() << "Jianku screenshot result" << result;
                    QMetaObject::invokeMethod(qApp, [self, result] {
                        if (self) self->setStatus(result);
                    }, Qt::QueuedConnection);
                }];
        }];
}
