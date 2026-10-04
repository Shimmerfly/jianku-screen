import QtQuick
import QtQuick.Layouts
import Jianku.Screen

// The editable content of one tab: the recording preview on top, the edit timeline
// under it, and the appearance settings to the side.
//
// The split of responsibilities is the point of this file. Everything here is a *view*
// of the session handed to it by the window; the session owns the timeline, its undo
// stack, the playhead and the exporter. That is why a tab can be dragged to another
// window without losing anything — the view is rebuilt, the session is not.
Item {
    id: tab
    // The session this tab shows, set by EditorWindow when it creates the tab.
    //
    // It cannot be reached through the window's id: a component in its own file has no
    // access to the ids of the file that instantiates it, so `editor.session` here is
    // `undefined` — which silently produced a strip bound to nothing and a wall of
    // "Cannot read property of undefined" instead of a visible failure.
    property var session: null
    readonly property var controller: session ? session.timeline : null
    readonly property var exporter: session ? session.exporter : null

    RowLayout {
        anchors.fill: parent
        spacing: 0

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true

                PreviewCanvas {
                    anchors.fill: parent
                    anchors.bottomMargin: timelineStrip.height
                    // The editor shows the *recording*, not the live screen: the
                    // preview draws the canvas appearance around a placeholder until a
                    // decoder is wired in, which is the same picture the export will
                    // produce.
                    live: false
                }

                TimelineStrip {
                    id: timelineStrip
                    controller: tab.controller
                    anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                    height: 74
                    visible: tab.session !== null
                }
            }
        }

        // --- right-hand settings and export -----------------------------------
        Rectangle {
            Layout.preferredWidth: 300
            Layout.fillHeight: true
            color: Theme.panelBg
            border.width: 1
            border.color: Theme.strokeSoft

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 14
                spacing: 10

                Text {
                    Layout.fillWidth: true
                    text: tab.session ? tab.session.title : ""
                    color: Theme.text
                    font.pixelSize: 14
                    font.weight: Font.DemiBold
                    elide: Text.ElideMiddle
                }
                Text {
                    Layout.fillWidth: true
                    text: tab.session ? tab.session.projectDirectory : ""
                    color: Theme.textFaint
                    font.pixelSize: 10
                    elide: Text.ElideMiddle
                }
                Text {
                    Layout.fillWidth: true
                    visible: tab.session && tab.session.error.length > 0
                    text: tab.session ? tab.session.error : ""
                    color: Theme.danger
                    font.pixelSize: 11
                    wrapMode: Text.WordWrap
                }

                Item { Layout.fillHeight: true }

                // Export lives here, at the bottom of the settings column, the way
                // Screen Studio puts it: the whole point of opening the editor is to
                // produce the file, so the action that does it should not be somewhere
                // the user has to go looking.
                Text {
                    Layout.fillWidth: true
                    text: "导出设置"
                    color: Theme.textDim
                    font.pixelSize: 12
                }
                UiSegmented {
                    Layout.fillWidth: true
                    options: ["原始", "1080p", "720p"]
                    currentIndex: {
                        const heights = [0, 1080, 720]
                        const i = heights.indexOf(Number(tab.exporter ? tab.exporter.exportHeight : 0))
                        return i >= 0 ? i : 0
                    }
                    onActivated: (i) => {
                        if (tab.exporter)
                            tab.exporter.exportHeight = [0, 1080, 720][i]
                    }
                }
                UiSegmented {
                    Layout.fillWidth: true
                    options: ["24", "30", "60", "120"]
                    currentIndex: {
                        const rates = [24, 30, 60, 120]
                        const i = rates.indexOf(Number(tab.exporter ? tab.exporter.frameRate : 60))
                        return i >= 0 ? i : 2
                    }
                    onActivated: (i) => {
                        if (tab.exporter)
                            tab.exporter.frameRate = [24, 30, 60, 120][i]
                    }
                }
                Text {
                    Layout.fillWidth: true
                    text: tab.exporter && tab.exporter.outputSizeLabel.length > 0
                        ? "成片 " + tab.exporter.outputSizeLabel + " · " + tab.exporter.frameRate + " fps"
                        : "—"
                    color: Theme.textFaint
                    font.pixelSize: 10
                }
                UiButton {
                    Layout.fillWidth: true
                    implicitHeight: 34
                    text: tab.exporter && tab.exporter.busy ? "取消导出" : "导出成片"
                    tone: "primary"
                    enabled: !!tab.exporter && (tab.exporter.busy || tab.exporter.defaultOutputPath.length > 0)
                    onClicked: {
                        if (!tab.exporter)
                            return
                        if (tab.exporter.busy)
                            tab.exporter.cancel()
                        else
                            tab.exporter.start(tab.exporter.defaultOutputPath, true, true, true, true)
                    }
                }
                // A drawn bar rather than QtQuick.Controls' ProgressBar: the rest of
                // this UI is drawn from the same few primitives, and pulling in the
                // Controls style for one progress bar would give it a different
                // palette from everything around it.
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: 4
                    radius: 2
                    color: Theme.fieldBg
                    visible: !!tab.exporter && tab.exporter.busy
                    Rectangle {
                        width: parent.width * Math.max(0, Math.min(1, tab.exporter ? tab.exporter.progress : 0))
                        height: parent.height
                        radius: 2
                        color: Theme.accent
                    }
                }
                Text {
                    Layout.fillWidth: true
                    visible: !!tab.exporter && tab.exporter.status.length > 0
                    text: tab.exporter ? tab.exporter.status : ""
                    color: tab.exporter && tab.exporter.error.length > 0 ? Theme.danger : Theme.textFaint
                    font.pixelSize: 10
                    wrapMode: Text.WordWrap
                }
            }
        }
    }
}
