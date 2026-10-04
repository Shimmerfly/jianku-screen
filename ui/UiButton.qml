import QtQuick
import QtQuick.Controls

Button {
    id: control
    property string tone: "quiet" // primary, quiet, ghost, record, danger
    property bool selected: false
    implicitHeight: 36
    // Wide enough for the label, never narrower.
    //
    // The fixed 100 was hidden by every existing caller passing an explicit width. The
    // editor's timeline row does not, so "删除 2 秒" was clipped to "删除 2 …" and the
    // speed buttons became "…" — the labels were being elided to nothing, in a row
    // where every button had plenty of room.
    implicitWidth: Math.max(56, contentItem.implicitWidth + leftPadding + rightPadding + 2)
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
