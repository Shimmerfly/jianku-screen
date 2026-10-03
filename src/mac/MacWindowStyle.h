#pragma once

// Shows the standard macOS About panel, which reads the application icon, name and
// version straight out of the bundle. Using the native panel rather than a custom
// window is what makes the logo appear there without any extra plumbing — and it is
// the panel macOS users expect from the application menu.
void showAboutPanel();

class QQuickWindow;
void applyDarkWindowStyle(QQuickWindow *window);
