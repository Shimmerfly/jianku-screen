import QtQuick
import QtQuick.Controls

ComboBox {
    id: control
    implicitHeight: 36
    font.pixelSize: 13
    leftPadding: 11
    rightPadding: 31

    contentItem: Text {
        text: control.displayText
        font: control.font
        color: control.enabled ? "#eeeeef" : "#7d7d81"
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
    indicator: Text {
        text: "⌄"
        color: "#b2b2b5"
        font.pixelSize: 19
        anchors.right: parent.right
        anchors.rightMargin: 11
        anchors.verticalCenter: parent.verticalCenter
    }
    background: Rectangle {
        radius: 7
        color: control.enabled ? "#202022" : "#2b2b2e"
        border.width: 1
        border.color: control.activeFocus ? "#9d9da2" : "#48484b"
    }
    delegate: ItemDelegate {
        width: control.width
        height: 36
        contentItem: Text {
            text: modelData
            color: "#eeeeef"
            font.pixelSize: 13
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle { color: highlighted ? "#454548" : "#2c2c2f" }
    }
    popup: Popup {
        y: control.height + 4
        width: control.width
        implicitHeight: Math.min(contentItem.implicitHeight + 8, 300)
        padding: 4
        contentItem: ListView {
            implicitHeight: contentHeight
            model: control.popup.visible ? control.delegateModel : null
            currentIndex: control.highlightedIndex
            clip: true
        }
        background: Rectangle {
            radius: 8
            color: "#2c2c2f"
            border.width: 1
            border.color: "#4d4d51"
        }
    }
}
