import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

Item {
    id: root
    property string label: ""
    property string value: "#000000"
    signal edited(string value)

    implicitHeight: 36

    RowLayout {
        anchors.fill: parent
        spacing: 8
        Text {
            visible: root.label.length > 0
            text: root.label
            color: Theme.text
            font.pixelSize: 12
        }
        Item { Layout.fillWidth: true }
        Rectangle {
            Layout.preferredWidth: 26
            Layout.preferredHeight: 22
            radius: Theme.radiusSm
            color: root.value
            border.width: 1
            border.color: "#55000000"
            HoverHandler { cursorShape: Qt.PointingHandCursor }
            TapHandler {
                onTapped: colorDialog.open()
            }
        }
        UiField {
            Layout.preferredWidth: 92
            text: root.value
            onEditingFinished: root.edited(text)
        }
    }

    ColorDialog {
        id: colorDialog
        selectedColor: root.value
        onAccepted: root.edited(selectedColor.toString())
    }
}
