import QtQuick
import QtQuick.Effects
import QtQuick.Shapes
import Jianku.Screen

// Real-time compositing preview: background (colour / gradient / image) + padding +
// rounded corners + inset + shadow, with the live capture inside the content frame.
// The same component is reused by the audience/output window.
Item {
    id: root
    property var options: settings.current
    property bool live: capture.running
    property bool showStageShadow: true
    property real stageMargin: 22

    readonly property string aspectKey: String(options.outputAspectRatio || "auto")
    readonly property real aspect: aspectKey === "9:16" ? 0.5625
        : aspectKey === "1:1" ? 1.0
        : aspectKey === "4:3" ? 1.3333
        : aspectKey === "16:10" ? 1.6
        : aspectKey === "21:9" ? 2.3333
        : aspectKey === "auto" ? (anim.contentHeight > 0 ? anim.contentWidth / anim.contentHeight : 1.7778)
        : 1.7778

    readonly property real padRatio: Math.max(0, Number(options.backgroundPaddingRatio || 0))
    readonly property real radius: Math.max(0, Number(options.windowBorderRadius || 0))
    readonly property real insetSize: Math.max(0, Number(options.insetSize || 0))
    readonly property real blurAmount: Math.min(1.0, Math.max(0, Number(options.backgroundBlur || 0)) / 40)
    readonly property real shadowIntensity: Math.max(0, Number(options.shadowIntensity || 0))
    readonly property real shadowAngle: Number(options.shadowAngle || 90)
    readonly property real shadowDistance: Number(options.shadowDistance || 0)
    readonly property real shadowBlur: Math.min(1.0, Math.max(0, Number(options.shadowBlur || 0)) / 40)

    readonly property bool hasContent: live
        || String(options.backgroundImagePath || "").length > 0

    Item {
        id: stage
        anchors.fill: parent
        anchors.margins: root.stageMargin

        readonly property real w: Math.min(Math.max(1, width), Math.max(1, height) * root.aspect)
        readonly property real h: w / root.aspect

        // Shadow backing (rendered blurred behind the canvas).
        Rectangle {
            id: shadowSrc
            visible: false
            x: canvas.x + Math.cos(root.shadowAngle * Math.PI / 180) * root.shadowDistance
            y: canvas.y + Math.sin(root.shadowAngle * Math.PI / 180) * root.shadowDistance
            width: canvas.width
            height: canvas.height
            radius: canvas.radius
            color: "#000000"
            opacity: Math.min(0.6, root.shadowIntensity * 0.6)
        }
        MultiEffect {
            source: shadowSrc
            anchors.fill: shadowSrc
            visible: root.showStageShadow && root.shadowIntensity > 0.001
            blurEnabled: true
            blur: Math.max(0.15, root.shadowBlur)
            blurMax: 48
            autoPaddingEnabled: true
        }

        // Canvas: the output frame at the requested aspect ratio.
        Rectangle {
            id: canvas
            x: Math.round((stage.width - stage.w) / 2)
            y: Math.round((stage.height - stage.h) / 2)
            width: Math.round(stage.w)
            height: Math.round(stage.h)
            radius: 10
            clip: true
            color: "#0c0c0e"

            // Background layer, drawn through MultiEffect so it can be blurred.
            Item {
                id: backgroundLayer
                anchors.fill: parent
                visible: false
                clip: true

                Rectangle {
                    anchors.fill: parent
                    visible: String(root.options.backgroundType) === "color"
                    color: String(root.options.backgroundColor || "#1b2230")
                }

                Rectangle {
                    anchors.centerIn: parent
                    visible: String(root.options.backgroundType || "gradient") === "gradient"
                    width: Math.hypot(parent.width, parent.height)
                    height: Math.hypot(parent.width, parent.height)
                    rotation: Number(root.options.gradientAngle || 135)
                    gradient: Gradient {
                        orientation: Gradient.Horizontal
                        GradientStop { position: 0.0; color: String(root.options.gradientStartColor || "#3F37C9") }
                        GradientStop { position: 1.0; color: String(root.options.gradientEndColor || "#8C87DF") }
                    }
                }

                Image {
                    anchors.fill: parent
                    visible: {
                        const t = String(root.options.backgroundType)
                        return t === "image" || t === "system"
                    }
                    source: {
                        const t = String(root.options.backgroundType)
                        if (t === "system")
                            return backgrounds.urlFor(String(root.options.backgroundSystemName || ""))
                        const p = String(root.options.backgroundImagePath || "")
                        return p.length > 0 ? "file://" + p : ""
                    }
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                    cache: true
                }
            }
            MultiEffect {
                source: backgroundLayer
                anchors.fill: backgroundLayer
                blurEnabled: root.blurAmount > 0.001
                blur: root.blurAmount
                blurMax: 64
                autoPaddingEnabled: true
            }

            // Content frame.
            Item {
                id: frameHost
                x: Math.round(canvas.width * root.padRatio / 100)
                y: Math.round(canvas.height * root.padRatio / 100)
                width: Math.max(1, canvas.width - x * 2)
                height: Math.max(1, canvas.height - y * 2)

                Rectangle {
                    id: frame
                    // The camera moves/scales the WHOLE frame (corners travel
                    // with the content, exactly like the reference).
                    x: frame.camOffsetX * frame.fitScale
                    y: frame.camOffsetY * frame.fitScale
                    width: parent.width
                    height: parent.height
                    radius: root.radius
                    clip: true
                    color: "#101013"
                    transformOrigin: Item.TopLeft
                    scale: frame.camScale

                    readonly property real contentW: Math.max(1, anim.contentWidth)
                    readonly property real contentH: Math.max(1, anim.contentHeight)
                    readonly property real fitScale: Math.min(width / contentW, height / contentH)
                    readonly property real layerW: contentW * fitScale
                    readonly property real layerH: contentH * fitScale
                    readonly property real layerX: (width - layerW) / 2
                    readonly property real layerY: (height - layerH) / 2
                    readonly property real camScale: anim.active ? anim.camScale : 1
                    readonly property real camOffsetX: anim.active ? anim.camOffsetX : 0
                    readonly property real camOffsetY: anim.active ? anim.camOffsetY : 0

                    Rectangle {
                        id: frameMask
                        anchors.fill: parent
                        radius: root.radius
                        color: "white"
                        visible: false
                        layer.enabled: true
                    }

                    Item {
                        id: contentLayer
                        anchors.fill: parent
                        layer.enabled: true
                        layer.effect: MultiEffect {
                            maskEnabled: true
                            maskSource: frameMask
                            maskThresholdMin: 0.5
                            maskSpreadAtMin: 0.0
                        }

                        VideoSurface {
                            anchors.fill: parent
                            visible: root.live
                            frameStore: capture.frameStore
                        }

                    // Placeholder so padding / background are visible before capture.
                    Item {
                        anchors.fill: parent
                        visible: !root.live
                        Rectangle {
                            anchors.fill: parent
                            gradient: Gradient {
                                orientation: Gradient.Vertical
                                GradientStop { position: 0.0; color: "#2b2f3a" }
                                GradientStop { position: 1.0; color: "#1b1e26" }
                            }
                        }
                        Rectangle {
                            anchors { left: parent.left; right: parent.right; top: parent.top; margins: 14 }
                            height: 10
                            radius: 5
                            color: "#14ffffff"
                        }
                        Text {
                            anchors.centerIn: parent
                            text: "屏幕画面将在此实时合成"
                            color: "#55ffffff"
                            font.pixelSize: Math.max(11, Math.min(18, frame.width / 26))
                        }
                    }
                    }

                    // Inset border drawn inside the frame edge.
                    Rectangle {
                        anchors.fill: parent
                        visible: root.insetSize > 0.01
                        color: "transparent"
                        radius: frame.radius
                        border.width: root.insetSize
                        border.color: Qt.rgba(
                            Qt.color(String(root.options.insetColor || "#000000")).r,
                            Qt.color(String(root.options.insetColor || "#000000")).g,
                            Qt.color(String(root.options.insetColor || "#000000")).b,
                            Math.max(0, Math.min(1, Number(root.options.insetAlpha || 0.5))))
                    }
                }

                // Smoothing pointer overlay: positioned and scaled by the camera
                // mapping (the cursor grows with the zoom, like the reference).
                Item {
                    id: cursorOverlay
                    readonly property real cursorScaleFactor: Math.max(0.5, Number(root.options.cursorSize || 1.5))
                    // The reference renders the cursor inside the zoomed screen
                    // layer, so its size follows the camera scale.
                    readonly property real uniformScale: frame.fitScale * cursorScaleFactor * frame.camScale
                    visible: anim.active && anim.cursorAvailable
                    width: Math.max(6, anim.cursorPointWidth * uniformScale)
                    height: Math.max(8, anim.cursorPointHeight * uniformScale)
                    x: frame.camOffsetX * frame.fitScale + (frame.layerX + anim.cursorX * frame.fitScale) * frame.camScale
                       - anim.cursorHotspotX * width
                    y: frame.camOffsetY * frame.fitScale + (frame.layerY + anim.cursorY * frame.fitScale) * frame.camScale
                       - anim.cursorHotspotY * height
                    opacity: anim.cursorAlpha * (Number(root.options.hideCursor) ? 0 : 1)
                    transform: [
                        Rotation {
                            origin.x: anim.cursorHotspotX * cursorOverlay.width
                            origin.y: anim.cursorHotspotY * cursorOverlay.height
                            angle: anim.cursorRotation
                        },
                        Scale {
                            origin.x: anim.cursorHotspotX * cursorOverlay.width
                            origin.y: anim.cursorHotspotY * cursorOverlay.height
                            xScale: anim.cursorScale
                            yScale: anim.cursorScale
                        }
                    ]
                    Image {
                        anchors.fill: parent
                        source: anim.cursorAvailable
                            ? ("image://cursor/current?" + anim.cursorImageRevision) : ""
                        fillMode: Image.Stretch
                        smooth: true
                        cache: false
                    }
                }
            }
        }
    }
}
