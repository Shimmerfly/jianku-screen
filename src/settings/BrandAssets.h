#pragma once

#include <QIcon>
#include <QObject>
#include <QString>
#include <QUrl>

// Brand assets: the application mark used inside the UI.
//
// The files are produced by `scripts/build-branding.py` from the source logo and
// bundled into `Resources/branding`, so the lookup follows the same two-step rule as
// the background library: the bundle first, the source tree second. The second path
// is what makes the app work when it is run straight out of the build directory
// during development.
//
// Nothing here is loaded through the QML resource system: the .icns has to exist as a
// real file for the bundle, and keeping all of them together means one lookup rule
// instead of two.
//
// The free functions live in `namespace Branding`; the QML-facing object is the
// `BrandAssets` class below (the two cannot share a name in C++). QML uses the URLs,
// C++ uses the QIcons, and both resolve through the same lookup.
namespace Branding {

// The coloured application mark at the requested pixel size (22/48/128 are the sizes
// that ship; any other size scales the nearest one). Returns a null icon when the
// assets are missing, which the callers treat as "draw nothing" — an empty tray icon
// is better than a crash or a placeholder that misrepresents the brand.
QIcon mark(int size = 0);

// The menu bar template: a shape whose alpha macOS paints black or white to match the
// menu bar. Set on a QAction/QSystemTrayIcon to get the automatic behaviour; used as a
// plain icon it renders black, which is why the tray keeps the coloured mark.
QIcon menuBarIcon();

// Absolute path to the branding directory, or empty when it cannot be found.
QString directory();

// Absolute path to one file inside it, or empty when it does not exist.
QString file(const QString &name);

} // namespace Branding

// QML-facing wrapper. Exposed as `brand` in the root context.
class BrandAssets : public QObject {
    Q_OBJECT
    // file:// URLs, empty when the asset is missing. QML's Image treats an empty
    // source as "draw nothing", which is the intended fallback.
    Q_PROPERTY(QUrl markSmall READ markSmall CONSTANT)
    Q_PROPERTY(QUrl markMedium READ markMedium CONSTANT)
    Q_PROPERTY(QUrl markLarge READ markLarge CONSTANT)
    Q_PROPERTY(QUrl menuBar READ menuBar CONSTANT)
    Q_PROPERTY(QString name READ name CONSTANT)
    Q_PROPERTY(QString displayName READ displayName CONSTANT)

public:
    explicit BrandAssets(QObject *parent = nullptr) : QObject(parent) {}

    QUrl markSmall() const { return url(QStringLiteral("jianku-mark-22.png")); }
    QUrl markMedium() const { return url(QStringLiteral("jianku-mark-48.png")); }
    QUrl markLarge() const { return url(QStringLiteral("jianku-mark-128.png")); }
    QUrl menuBar() const { return url(QStringLiteral("jianku-menubar.png")); }
    QString name() const { return QStringLiteral("Jianku Screen"); }
    QString displayName() const { return QStringLiteral("简库镜传"); }

private:
    static QUrl url(const QString &name) {
        const QString path = Branding::file(name);
        return path.isEmpty() ? QUrl() : QUrl::fromLocalFile(path);
    }
};
