import QtQuick
import QtQuick.Controls

Button {
    id: control
    property string tone: "quiet" // primary, quiet, ghost, record, danger
    property bool selected: false
    implicitHeight: 36
    implicitWidth: 100
    leftPadding: 12
    rightPadding: 12
    font.pixelSize: 13
    font.weight: tone === "primary" || tone === "record" ? Font.DemiBold : Font.Medium

    contentItem: Text {
        text: control.text
        font: control.font
        color: !control.enabled ? "#77777b"
            : control.tone === "primary" ? "#242426"
            : control.tone === "record" ? "#ffffff"
            : control.tone === "danger" ? "#ff8e89" : "#e9e9ea"
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
    background: Rectangle {
        radius: 8
        color: !control.enabled ? "#303033"
            : control.tone === "primary" ? (control.down ? "#d3d3d6" : "#eeeeef")
            : control.tone === "record" ? (control.down ? "#c94440" : control.hovered ? "#ef625d" : "#df514c")
            : control.tone === "danger" ? (control.hovered ? "#423030" : "#34292a")
            : control.selected ? "#3d3d40"
            : control.tone === "ghost" ? (control.hovered ? "#333336" : "transparent")
            : control.hovered ? "#49494d" : "#3a3a3e"
        border.width: control.tone === "quiet" || control.selected ? 1 : 0
        border.color: control.selected ? "#66666b" : "#4d4d51"
    }
}
