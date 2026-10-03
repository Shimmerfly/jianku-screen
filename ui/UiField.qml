import QtQuick
import QtQuick.Controls

TextField {
    id: control
    implicitHeight: 36
    color: "#eeeeef"
    selectionColor: "#77777c"
    selectedTextColor: "#ffffff"
    placeholderTextColor: "#8d8d91"
    font.pixelSize: 13
    leftPadding: 11
    rightPadding: 11
    background: Rectangle {
        radius: 7
        color: "#202022"
        border.width: 1
        border.color: control.activeFocus ? "#9d9da2" : "#48484b"
    }
}
