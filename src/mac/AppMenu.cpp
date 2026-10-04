#include "AppMenu.h"

#include "../render/EditorSessionRegistry.h"

#include <QAction>
#include <QApplication>
#include <QFileDialog>
#include <QKeySequence>
#include <QMenu>
#include <QMenuBar>
#include <QQuickWindow>

// Every item exists because a keyboard shortcut needs somewhere to be discovered from:
// a shortcut with no menu entry is a shortcut nobody finds. The set is deliberately
// small for that reason — an item that nothing can act on is worse than a missing one.
void installApplicationMenu(Render::EditorSessionRegistry *registry) {
    // Parented to the application object, not to a window: on macOS a QMenuBar with no
    // window is the system-wide menu bar, and parenting it to a window would tie the menu
    // to that window's lifetime.
    auto *bar = new QMenuBar();

    QMenu *file = bar->addMenu(QStringLiteral("文件"));

    QAction *newWindow = file->addAction(QStringLiteral("新建编辑器窗口"));
    newWindow->setShortcut(QKeySequence::New);              // ⌘N
    QObject::connect(newWindow, &QAction::triggered, bar, [registry] {
        registry->newWindow();
    });

    QAction *open = file->addAction(QStringLiteral("打开录制工程…"));
    open->setShortcut(QKeySequence::Open);                  // ⌘O
    QObject::connect(open, &QAction::triggered, bar, [registry] {
        // A recording is a *directory*, so this is a directory picker: a file picker
        // would ask the user to select a package they cannot see inside.
        const QString path = QFileDialog::getExistingDirectory(nullptr,
            QStringLiteral("打开录制工程"), QString(), QFileDialog::ShowDirsOnly);
        if (!path.isEmpty())
            registry->openProject(path, false);
    });

    file->addSeparator();
    QAction *save = file->addAction(QStringLiteral("保存编辑"));
    save->setShortcut(QKeySequence::Save);                  // ⌘S
    QObject::connect(save, &QAction::triggered, bar, [registry] { registry->saveActive(); });

    file->addSeparator();
    QAction *closeTab = file->addAction(QStringLiteral("关闭标签页"));
    closeTab->setShortcut(QKeySequence::Close);             // ⌘W
    QObject::connect(closeTab, &QAction::triggered, bar, [registry] {
        Render::EditorSession *session = registry->activeSession();
        QQuickWindow *window = registry->windowAt(registry->activeWindowId());
        if (!session || !window)
            return;
        registry->closeTab(window->property("editorWindowId").toInt(),
            session->projectDirectory());
    });

    QMenu *edit = bar->addMenu(QStringLiteral("编辑"));
    QAction *undo = edit->addAction(QStringLiteral("撤销"));
    undo->setShortcut(QKeySequence::Undo);                  // ⌘Z
    QObject::connect(undo, &QAction::triggered, bar, [registry] { registry->undoActive(); });
    QAction *redo = edit->addAction(QStringLiteral("重做"));
    redo->setShortcut(QKeySequence::Redo);                  // ⇧⌘Z
    QObject::connect(redo, &QAction::triggered, bar, [registry] { registry->redoActive(); });

    // Enabled state follows the *active* session, which changes as focus moves between
    // editor windows. Recomputing when the menu opens is enough and costs no signal per
    // edit; a permanently-enabled Undo that does nothing is the thing to avoid.
    QObject::connect(edit, &QMenu::aboutToShow, bar, [registry, undo, redo] {
        undo->setEnabled(registry->activeCanUndo());
        redo->setEnabled(registry->activeCanRedo());
    });
    QObject::connect(file, &QMenu::aboutToShow, bar, [registry, save, closeTab] {
        save->setEnabled(registry->hasActive());
        closeTab->setEnabled(registry->hasActive());
    });

    // No `setMenuBar` call: Qt 6 removed `QApplication::setMenuBar()`. On macOS a
    // windowless QMenuBar *is* the system-wide menu bar — that is the documented behaviour
    // and the reason this file exists rather than a QML MenuBar per window.
    bar->setNativeMenuBar(true);
}
