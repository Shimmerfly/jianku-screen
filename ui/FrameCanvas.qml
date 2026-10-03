import QtQuick
import Jianku.Screen

Item {
    id: canvas
    property var options: settings.current
    property bool showPlaceholder: true

    readonly property real inset: Math.max(0, Math.min(width, height) * Number(options.backgroundPaddingRatio || 0) / 100)

    Rectangle {
        anchors.fill: parent
        color: options.backgroundColor || "#1c2630"
        clip: true

        Image {
            anchors.fill: parent
            source: options.backgroundType === "image" && options.backgroundImagePath
                    ? "file://" + options.backgroundImagePath : ""
            fillMode: Image.PreserveAspectCrop
            visible: status === Image.Ready
        }

        Rectangle {
            id: frame
            x: canvas.inset
            y: canvas.inset
            width: Math.max(0, canvas.width - canvas.inset * 2)
            height: Math.max(0, canvas.height - canvas.inset * 2)
            radius: Math.min(Number(options.windowBorderRadius || 0), Math.min(width, height) / 2)
            clip: true
            color: "#192027"

            VideoSurface {
                anchors.fill: parent
                frameStore: capture.frameStore
            }
        }
    }
}
