#import "MacWindowStyle.h"

#import <AppKit/AppKit.h>

#include <QQuickWindow>

void applyDarkWindowStyle(QQuickWindow *window) {
    if (!window) return;
    auto *view = (__bridge NSView *)reinterpret_cast<void *>(window->winId());
    if (!view.window) return;
    view.window.appearance = [NSAppearance appearanceNamed:NSAppearanceNameDarkAqua];
}

void showAboutPanel() {
    // The credits string is the one piece the panel cannot take from the bundle, and
    // it is where the version of the animation model belongs: it is the one thing a
    // user comparing two builds needs to know.
    NSString *credits = @"屏幕录制与实时演示\n动画模型 desktop-3.7.5-research-v1";
    NSDictionary *options = @{@"Credits": credits};
    [NSApp orderFrontStandardAboutPanelWithOptions:options];
}
