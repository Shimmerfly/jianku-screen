#import "MacWindowStyle.h"

#import <AppKit/AppKit.h>

#include <QQuickWindow>

void applyDarkWindowStyle(QQuickWindow *window) {
    if (!window) return;
    auto *view = (__bridge NSView *)reinterpret_cast<void *>(window->winId());
    if (!view.window) return;
    view.window.appearance = [NSAppearance appearanceNamed:NSAppearanceNameDarkAqua];
}
