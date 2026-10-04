#pragma once

namespace Render { class EditorSessionRegistry; }

// Installs the application menu bar.
//
// Built in C++ rather than as a QML `MenuBar` because on macOS the menu bar is
// *application-wide* — one per process. N editor windows each declaring a QML MenuBar
// would fight over that one bar, and "Save" would belong to whichever window Qt happened
// to consider outermost. Here every item routes through the registry's notion of the
// active session instead, so "Save" always means the window you are looking at.
//
// Not a class: it has no state of its own. The menu lives as long as the application,
// and the actions read the registry each time they fire, so no state has to be kept in
// sync with the sessions.
void installApplicationMenu(Render::EditorSessionRegistry *registry);
