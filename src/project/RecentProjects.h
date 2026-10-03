#pragma once

#include <QString>
#include <QStringList>

// Finds recordings on disk.
//
// Until now the app only knew about the project it had just finished recording: the
// path lived in memory, so restarting the app left the export button and the whole
// edit timeline disabled with no way to point them at an existing recording. Every
// other screen recorder lets you go back to yesterday's recording, and this is also
// what makes a crash recoverable — a recording that finished writing but was never
// processed is still a usable project.
namespace RecentProjects {

// The directory the recorder writes into: the user's setting, or the default when it
// is unset. Kept in one place because both the recorder and this search must agree —
// a search of a different directory would silently find nothing.
QString recordingDirectory(const QString &configured);

// Every `*.jianku` directory in `directory` that has a readable manifest, newest
// first. `project.json` is checked rather than the folder name because a directory
// without a manifest is not something the loader can open.
QStringList list(const QString &directory);

// The most recent project, or empty when there is none. This is what the app should
// open on launch.
QString mostRecent(const QString &directory);

// "<time> · <duration>" for a project, or the directory name when the manifest cannot
// be read. Duration is read from the manifest's own video block, so an interrupted
// recording (which has no video block) is still listed rather than hidden.
QString describe(const QString &projectDirectory);

} // namespace RecentProjects
