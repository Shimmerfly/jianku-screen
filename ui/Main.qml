import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Jianku.Screen

ApplicationWindow {
    id: root
    visible: true
    width: 1280
    height: 800
    minimumWidth: 1060
    minimumHeight: 680
    title: "简库镜传"
    color: Theme.windowBg

    property string mode: "record"
    property string section: "background"
    property bool openPresentationWhenReady: false
    readonly property bool savingRecording: capture.recordingStatus.indexOf("正在保存") === 0
    readonly property var outputOptions: {
        const list = ["虚拟窗口（可共享）"]
        const displays = screens.displays
        for (let i = 0; i < displays.length; ++i)
            list.push("输出到 " + displays[i].name + " · " + displays[i].width + "×" + displays[i].height)
        return list
    }

    function showAudience() {
        const index = outputChoice.currentIndex - 1
        if (index >= 0)
            screens.placeWindowOnDisplay(audience, index, true)
        else
            screens.placeWindowOnDisplay(audience, -1, false)
    }

    function selectMode(next) {
        if (capture.recording || capture.busy || savingRecording)
            return
        openPresentationWhenReady = false
        audience.hide()
        if (capture.running)
            capture.stop()
        mode = next
        section = "background"
    }

    OutputWindow { id: audience }

    Component.onCompleted: {
        anim.setSettings(settings.current)
        anim.start()
    }
    Connections {
        target: settings
        function onCurrentChanged() { anim.setSettings(settings.current) }
    }
    Connections {
        target: capture
        function onScreenAuthorizedChanged() {
            if (capture.screenAuthorized)
                permissionGuide.opened = false
        }
        function onCaptureAccessDenied() {
            permissionGuide.opened = true
        }
        function onPermissionIssueChanged() {
            if (capture.permissionIssue.length > 0)
                permissionGuide.opened = true
        }
    }
    onActiveChanged: {
        if (active)
            capture.refreshScreenAuthorization()
    }

    Window {
        id: recordingHud
        visible: false
        width: 392
        height: 64
        x: root.x + (root.width - width) / 2
        y: root.y + 35
        flags: Qt.Tool | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint
        transientParent: null
        color: "transparent"
        property int elapsedSeconds: 0

        Timer {
            interval: 1000
            repeat: true
            // The on-screen clock freezes while paused, like the recorded file.
            running: recordingHud.visible && !capture.recordingPaused
            onTriggered: recordingHud.elapsedSeconds++
        }
        Rectangle {
            anchors.fill: parent
            radius: 13
            color: "#343437"
            border.width: 1
            border.color: "#59595d"
            MouseArea { anchors.fill: parent; onPressed: recordingHud.startSystemMove() }
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 19
                anchors.rightMargin: 11
                spacing: 12
                Rectangle {
                    width: 9
                    height: 9
                    radius: 5
                    color: capture.recordingPaused ? Theme.textDim : Theme.record
                }
                Text {
                    text: (capture.recordingPaused ? "已暂停  " : "录制中  ")
                          + Math.floor(recordingHud.elapsedSeconds / 60).toString().padStart(2, "0")
                          + ":" + (recordingHud.elapsedSeconds % 60).toString().padStart(2, "0")
                    color: "#f0f0f1"
                    font.pixelSize: 14
                    font.weight: Font.Medium
                }
                Item { Layout.fillWidth: true }
                UiButton {
                    implicitWidth: 84
                    text: capture.recordingPaused ? "继续" : "暂停"
                    tone: "quiet"
                    onClicked: capture.recordingPaused ? capture.resumeRecording() : capture.pauseRecording()
                }
                UiButton { text: "结束"; tone: "quiet"; implicitWidth: 72; onClicked: capture.stop() }
            }
        }
    }

    Connections {
        target: exporter
        function onFinished(ok) {
            if (ok) exporter.revealOutput()
        }
    }

    Connections {
        target: capture
        function onRunningChanged() {
            if (capture.running && root.openPresentationWhenReady) {
                root.openPresentationWhenReady = false
                root.showAudience()
            }
        }
        function onRecordingChanged() {
            if (capture.recording) {
                recordingHud.elapsedSeconds = 0
                recordingHud.show()
                root.hide()
            } else if (recordingHud.visible) {
                recordingHud.hide()
                root.show()
                root.raise()
                root.requestActivate()
            }
        }
        function onStatusChanged() {
            if (capture.status.indexOf("权限") >= 0 || capture.status.indexOf("拒绝") >= 0)
                permissionGuide.opened = true
        }
    }

    Item {
        anchors.fill: parent

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 56
            color: Theme.railBg

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 16
                anchors.rightMargin: 14
                spacing: 12

                Text {
                    text: "简库镜传"
                    color: Theme.text
                    font.pixelSize: 15
                    font.weight: Font.DemiBold
                }
                UiSegmented {
                    Layout.preferredWidth: 148
                    options: ["录制", "演示"]
                    currentIndex: root.mode === "record" ? 0 : 1
                    onActivated: i => root.selectMode(i === 0 ? "record" : "present")
                }

                Item { Layout.fillWidth: true }

                Text { text: "来源"; color: Theme.textDim; font.pixelSize: 11 }
                UiComboBox {
                    id: displayChoice
                    Layout.preferredWidth: 210
                    model: capture.displayNames
                    enabled: !capture.running && !capture.busy
                }
                UiButton {
                    implicitWidth: 58
                    text: "刷新"
                    tone: "ghost"
                    enabled: !capture.running && !capture.busy
                    onClicked: capture.refreshDisplays()
                }

                Text {
                    visible: root.mode === "present"
                    text: "输出"
                    color: Theme.textDim
                    font.pixelSize: 11
                }
                UiComboBox {
                    id: outputChoice
                    visible: root.mode === "present"
                    Layout.preferredWidth: 210
                    model: root.outputOptions
                    enabled: !capture.running
                }

                Rectangle { width: 1; height: 22; color: Theme.stroke }

                Text {
                    readonly property bool ready: !capture.running && !capture.recording
                        && capture.screenAuthorized && capture.displayNames.length > 0
                    text: capture.recording ? "\u25CF 录制中"
                        : capture.running ? "\u25CF 画面采集中"
                        : ready ? "\u25CF 就绪" : "\u25CF 未就绪"
                    color: capture.recording ? Theme.record
                        : capture.running ? Theme.accent
                        : ready ? Theme.ok : Theme.textFaint
                    font.pixelSize: 11
                }
                UiButton {
                    implicitWidth: 62
                    text: "权限"
                    tone: capture.screenAuthorized ? "ghost" : "primary"
                    onClicked: permissionGuide.opened = true
                }
                UiButton {
                    implicitWidth: 62
                    text: "截图"
                    tone: "quiet"
                    onClicked: screenshot.capture(settings.current)
                }
                UiButton {
                    // Export the recording through the offline compositor: this is
                    // the only path that puts the smooth pointer and the camera
                    // into the finished file.
                    implicitWidth: 78
                    text: exporter.busy ? "取消导出" : "导出成片"
                    tone: "quiet"
                    enabled: exporter.busy || exporter.defaultOutputPath.length > 0
                    onClicked: {
                        if (exporter.busy) exporter.cancel()
                        else {
                            exporter.reset()
                            exporter.start(exporter.defaultOutputPath, true,
                                !!settings.current.autoZoom, true)
                        }
                    }
                }
                UiButton {
                    visible: root.mode === "present"
                    implicitWidth: 92
                    text: "观众窗口"
                    tone: "quiet"
                    enabled: capture.running
                    onClicked: root.showAudience()
                }
                UiButton {
                    implicitWidth: 112
                    implicitHeight: 36
                    text: root.mode === "record"
                        ? (capture.recording ? "结束录制" : root.savingRecording ? "正在保存…" : "开始录制")
                        : (capture.running ? "结束演示" : "开始演示")
                    tone: (capture.recording || (root.mode === "present" && capture.running)) ? "danger"
                        : root.mode === "record" ? "record" : "primary"
                    enabled: root.mode === "record"
                        ? (capture.recording || (!capture.busy && !root.savingRecording && displayChoice.count > 0))
                        : (!capture.busy && (capture.running || displayChoice.count > 0))
                    onClicked: {
                        if (root.mode === "record") {
                            if (capture.recording) capture.stop()
                            else capture.startRecordingDisplay(displayChoice.currentIndex, settings.current)
                        } else if (capture.running) {
                            audience.hide()
                            capture.stop()
                        } else {
                            root.openPresentationWhenReady = true
                            capture.startDisplay(displayChoice.currentIndex)
                        }
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            IconRail {
                Layout.preferredWidth: Theme.railWidth
                Layout.fillHeight: true
                current: root.section
                onSelected: s => root.section = s
            }

            SettingsPage {
                Layout.preferredWidth: Theme.panelWidth
                Layout.fillHeight: true
                section: root.section
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: Theme.windowBg

                PreviewCanvas {
                    anchors.fill: parent
                    live: capture.running
                }

                Text {
                    anchors.left: parent.left
                    anchors.bottom: parent.bottom
                    anchors.margins: 16
                    text: capture.running
                        ? (root.mode === "record" ? "实时预览 · 录制画面" : "实时预览 · 演示输出")
                        : "未开始采集 · 显示的是画布外观预览"
                    color: Theme.textFaint
                    font.pixelSize: 11
                }

                Text {
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.margins: 16
                    text: {
                        if (exporter.error.length > 0)
                            return "导出失败：" + exporter.error
                        if (exporter.status.length > 0)
                            return exporter.status
                        return capture.recordingStatus.length > 0 ? capture.recordingStatus : capture.status
                    }
                    color: exporter.error.length > 0 ? Theme.danger : Theme.textFaint
                    font.pixelSize: 11
                    elide: Text.ElideMiddle
                    width: Math.min(implicitWidth, parent.width - 40)
                    horizontalAlignment: Text.AlignRight
                }

                // Export progress: a thin bar along the bottom edge of the preview.
                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: 2
                    visible: exporter.busy
                    color: Theme.stroke
                    Rectangle {
                        height: parent.height
                        width: parent.width * Math.max(0, Math.min(1, exporter.progress))
                        color: Theme.accent
                    }
                }
            }
        }
    }

    PermissionGuide {
        id: permissionGuide
        anchors.fill: parent
    }
    }
}
