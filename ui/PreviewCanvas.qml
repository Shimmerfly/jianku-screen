import QtQuick
import QtQuick.Effects
import QtQuick.Shapes
import Jianku.Screen

// Real-time compositing preview: background (colour / gradient / image) + padding +
// rounded corners + inset + shadow, with the live capture inside the content frame.
// The same component is reused by the audience/output window.
//
// Every rectangle here is a *fraction* of the canvas, computed once by
// CanvasPreview from the same C++ layout the export and the screenshot use. The
// values are not recomputed in QML: that is what previously made the preview's
// padding, corners and background disagree with the finished film.
Item {
    id: root
    property var options: settings.current
    property bool live: capture.running
    property bool showStageShadow: true
    property real stageMargin: 22

    readonly property var plan: canvasPreview
    // `Math.max(0.05, plan.planAspect)` looked reasonable and was silently wrong: the
    // QML type of `plan` here is `var`, and reading a property that the *engine* has
    // not resolved yet yields undefined rather than an error, so the max() collapsed
    // to its floor and the stage became a square. Reading it through a typed local
    // makes a missing property loud instead. The debug log that found this is gone;
    // what it found is this comment.
    readonly property real planAspect: {
        var a = Number(plan.planAspect)
        if (!(a > 0)) a = 16.0 / 9.0
        return a
    }
    readonly property real aspect: Math.max(0.05, planAspect)
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

        // Canvas: the whole output frame. The camera moves the *content* inside
        // it, never the canvas itself.
        Rectangle {
            id: canvas
            x: Math.round((stage.width - stage.w) / 2)
            y: Math.round((stage.height - stage.h) / 2)
            width: Math.round(stage.w)
            height: Math.round(stage.h)
            radius: Math.round(root.plan.radiusRatio * height)
            clip: true
            color: "#0c0c0e"

            // Background layer, drawn through MultiEffect so it can be blurred.
            Item {
                id: backgroundLayer
                anchors.fill: parent
                visible: false
                clip: true

                // Colours come from the same resolved style the export uses, so a
                // background cannot look one way in the preview and another in the
                // finished file.
                Rectangle {
                    anchors.fill: parent
                    visible: root.plan.backgroundType !== "gradient"
                        && root.plan.backgroundType !== "image"
                        && root.plan.backgroundType !== "system"
                    color: root.plan.backgroundColor
                }

                Rectangle {
                    anchors.centerIn: parent
                    visible: root.plan.backgroundType === "gradient"
                    width: Math.hypot(parent.width, parent.height)
                    height: Math.hypot(parent.width, parent.height)
                    rotation: root.plan.gradientAngle
                    gradient: Gradient {
                        orientation: Gradient.Horizontal
                        GradientStop { position: 0.0; color: root.plan.gradientStart }
                        GradientStop { position: 1.0; color: root.plan.gradientEnd }
                    }
                }

                Image {
                    anchors.fill: parent
                    visible: root.plan.backgroundType === "image"
                        || root.plan.backgroundType === "system"
                    source: {
                        if (root.plan.backgroundType === "system")
                            return canvasPreview.backgroundUrl(
                                String(root.options.backgroundSystemName || ""))
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
                blurEnabled: root.plan.backgroundBlur > 0.001
                blur: root.plan.backgroundBlur
                blurMax: 64
                autoPaddingEnabled: true
            }

            // Content frame, straight from the shared layout (frameRect is the
            // outer frame; the source is contained inside it).
            Item {
                id: frameHost
                x: Math.round(root.plan.frameRect.x * canvas.width)
                y: Math.round(root.plan.frameRect.y * canvas.height)
                width: Math.max(1, Math.round(root.plan.frameRect.width * canvas.width))
                height: Math.max(1, Math.round(root.plan.frameRect.height * canvas.height))

                Rectangle {
                    id: frame
                    // The camera moves/scales the WHOLE frame (corners travel
                    // with the content, exactly like the reference).
                    x: frame.camOffsetX * frame.fitScale
                    y: frame.camOffsetY * frame.fitScale
                    width: parent.width
                    height: parent.height
                    radius: Math.round(root.plan.radiusRatio * height)
                    clip: true
                    color: "#101013"
                    transformOrigin: Item.TopLeft
                    scale: frame.camScale

                    readonly property real contentW: Math.max(1, anim.contentWidth)
                    readonly property real contentH: Math.max(1, anim.contentHeight)
                    readonly property real fitScale: Math.min(width / contentW, height / contentH)
                    // `anim.cursorX/Y` are source *pixels* (the space the recorded
                    // events and the compositor use), while fitScale below maps source
                    // *points* onto this item. The driver exposes both, converted in
                    // one place, because reading the pixel one here put the pointer at
                    // twice its distance from the top-left corner on a Retina display
                    // and the error grew the further it was dragged.
                    readonly property real cursorXPoints: anim.cursorXPoints
                    readonly property real cursorYPoints: anim.cursorYPoints
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
                        radius: Math.round(root.plan.radiusRatio * height)
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
                        visible: root.plan.insetRatio * canvas.height > 0.5
                        color: "transparent"
                        radius: frame.radius
                        border.width: root.plan.insetRatio * canvas.height
                        border.color: Qt.rgba(root.plan.insetColor.r, root.plan.insetColor.g,
                            root.plan.insetColor.b, root.plan.insetAlpha)
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
                    x: frame.camOffsetX * frame.fitScale
                       + (frame.layerX + frame.cursorXPoints * frame.fitScale) * frame.camScale
                       - anim.cursorHotspotX * width
                    y: frame.camOffsetY * frame.fitScale
                       + (frame.layerY + frame.cursorYPoints * frame.fitScale) * frame.camScale
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
