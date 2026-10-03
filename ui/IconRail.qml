import QtQuick
import QtQuick.Layouts

Item {
    id: root
    property string current: "background"
    signal selected(string section)

    readonly property var items: [
        { key: "record", glyph: "\u25CF", label: "录制" },
        { key: "background", glyph: "\u25A7", label: "背景" },
        { key: "cursor", glyph: "\u27A4", label: "光标" },
        { key: "motion", glyph: "\u2922", label: "缩放" },
        { key: "camera", glyph: "\u25C9", label: "摄像头" },
        { key: "audio", glyph: "\u266A", label: "音频" },
        { key: "transcript", glyph: "T", label: "字幕" },
        { key: "shortcuts", glyph: "\u2318", label: "快捷键" },
        { key: "speed", glyph: "\u00BB", label: "导出" },
        { key: "layout", glyph: "\u25A6", label: "布局" }
    ]

    Rectangle {
        anchors.fill: parent
        color: Theme.railBg
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.topMargin: 14
        anchors.bottomMargin: 12
        spacing: 2

        Repeater {
            model: root.items
            delegate: Item {
                required property var modelData
                Layout.fillWidth: true
                Layout.preferredHeight: 48
                Rectangle {
                    anchors.centerIn: parent
                    width: 42
                    height: 42
                    radius: Theme.radiusMd
                    color: root.current === modelData.key ? Theme.cardBgHover
                        : hover.hovered ? Theme.cardBg : "transparent"
                    ColumnLayout {
                        anchors.centerIn: parent
                        spacing: 1
                        Text {
                            Layout.alignment: Qt.AlignHCenter
                            text: modelData.glyph
                            color: root.current === modelData.key ? Theme.text : Theme.textDim
                            font.pixelSize: 16
                        }
                        Text {
                            Layout.alignment: Qt.AlignHCenter
                            text: modelData.label
                            color: root.current === modelData.key ? Theme.textDim : Theme.textFaint
                            font.pixelSize: 8
                        }
                    }
                    HoverHandler { id: hover }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.selected(modelData.key)
                    }
                }
            }
        }

        Item { Layout.fillHeight: true }
    }
}
