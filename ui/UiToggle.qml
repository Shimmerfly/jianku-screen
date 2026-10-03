import QtQuick
import QtQuick.Layouts

Item {
    id: root
    property string label: ""
    property string detail: ""
    property bool checked: false
    signal toggled(bool value)

    implicitHeight: detail.length > 0 ? 48 : 34

    RowLayout {
        anchors.fill: parent
        spacing: 10

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2
            Text { text: root.label; color: Theme.text; font.pixelSize: 12 }
            Text {
                visible: root.detail.length > 0
                Layout.fillWidth: true
                text: root.detail
                color: Theme.textFaint
                font.pixelSize: 10
                elide: Text.ElideRight
            }
        }

        Rectangle {
            id: track
            Layout.preferredWidth: 38
            Layout.preferredHeight: 22
            radius: 11
            color: root.checked ? Theme.accent : "#414147"
            border.width: 1
            border.color: root.checked ? Theme.accent : "#4c4c52"

            Rectangle {
                width: 16
                height: 16
                radius: 8
                y: 3
                x: root.checked ? parent.width - width - 3 : 3
                color: "#ffffff"
                Behavior on x { NumberAnimation { duration: 130; easing.type: Easing.OutCubic } }
            }
            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: root.toggled(!root.checked)
            }
        }
    }
}
