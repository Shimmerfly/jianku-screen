import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Jianku.Screen

// Full-desktop region picker.
//
// Covers the union of every screen while a selection is in progress, so a drag may
// cross monitor boundaries. The window is click-through-free only while open: it
// grabs the mouse, dims the desktop, and reports the chosen rectangle in the same
// global logical points the capture layer and the project manifest use.
Window {
    id: overlay
    signal picked(real x, real y, real width, real height)
    signal dismissed()

    // One point-space rectangle covering every screen.
    property rect desktop: screens.unionGeometry
    property real dragStartX: -1
    property real dragStartY: -1
    property bool dragging: false
    readonly property real selectionX: dragging ? Math.min(dragStartX, pointerX) : 0
    readonly property real selectionY: dragging ? Math.min(dragStartY, pointerY) : 0
    readonly property real selectionW: dragging ? Math.abs(pointerX - dragStartX) : 0
    readonly property real selectionH: dragging ? Math.abs(pointerY - dragStartY) : 0
    property real pointerX: 0
    property real pointerY: 0

    // Below this a "selection" is a stray click, not a region.
    readonly property real minimumSize: 24

    function begin() {
        if (desktop.width <= 0 || desktop.height <= 0)
            return
        setGeometry(desktop.x, desktop.y, desktop.width, desktop.height)
        dragging = false
        dragStartX = dragStartY = -1
        show()
        raise()
        requestActivate()
    }

    flags: Qt.Window | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint
    color: "#66000000"
    visible: false

    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.CrossCursor
        hoverEnabled: true
        onPositionChanged: mouse => {
            overlay.pointerX = mouse.x
            overlay.pointerY = mouse.y
        }
        onPressed: mouse => {
            overlay.dragging = true
            overlay.dragStartX = mouse.x
            overlay.dragStartY = mouse.y
            overlay.pointerX = mouse.x
            overlay.pointerY = mouse.y
        }
        onReleased: mouse => {
            overlay.pointerX = mouse.x
            overlay.pointerY = mouse.y
            const w = overlay.selectionW
            const h = overlay.selectionH
            overlay.dragging = false
            overlay.hide()
            if (w < overlay.minimumSize || h < overlay.minimumSize) {
                overlay.dismissed()
                return
            }
            // Window-local coordinates are already global logical points here
            // because the window sits exactly on the union rectangle.
            overlay.picked(overlay.desktop.x + overlay.selectionX,
                overlay.desktop.y + overlay.selectionY, w, h)
        }
    }

    // Live readout of the drag, in the capture pixel size the recording will use.
    Rectangle {
        visible: overlay.dragging
        x: overlay.selectionX
        y: overlay.selectionY
        width: overlay.selectionW
        height: overlay.selectionH
        color: "transparent"
        border.width: 1
        border.color: Theme.accent

        Rectangle {
            anchors.fill: parent
            anchors.margins: 1
            color: "#26ffffff"
        }

        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: parent.top
            anchors.topMargin: 6
            width: label.implicitWidth + 16
            height: 22
            radius: 4
            color: "#cc000000"
            Text {
                id: label
                anchors.centerIn: parent
                color: "white"
                font.pixelSize: 11
                text: Math.round(overlay.selectionW) + " × " + Math.round(overlay.selectionH) + " pt"
            }
        }
    }

    Text {
        anchors.centerIn: parent
        visible: !overlay.dragging
        color: "white"
        font.pixelSize: 15
        text: "拖动选择录制区域 · 按 Esc 取消"
    }

    Shortcut {
        sequence: "Escape"
        onActivated: {
            overlay.dragging = false
            overlay.hide()
            overlay.dismissed()
        }
    }
}
