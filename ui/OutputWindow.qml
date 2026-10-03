import QtQuick
import QtQuick.Controls
import Jianku.Screen

Window {
    id: output
    objectName: "audienceWindow"
    visible: false
    width: 1280
    height: 720
    minimumWidth: 480
    minimumHeight: 270
    title: "简库镜传 — 演示输出"
    color: "#0d0d0f"

    PreviewCanvas {
        anchors.fill: parent
        live: capture.running
        showStageShadow: false
        stageMargin: 0
    }

    Label {
        anchors.centerIn: parent
        text: "等待屏幕画面"
        color: Theme.textFaint
        visible: !capture.running
    }
}
