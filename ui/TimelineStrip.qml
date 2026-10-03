import QtQuick
import QtQuick.Layouts

// The edit timeline: a ruler, one bar per segment, and a draggable playhead.
//
// All the arithmetic lives in TimelineController (which delegates to
// Project::EditTimeline and Render::TimelineGeometry, both unit tested). This file
// only lays out what the controller reports and sends times back — deliberately, so
// the position a click produces here is the same position the export uses. Doing the
// maths in QML would create a second answer to "where is the cut".
Item {
    id: root
    implicitHeight: 74
    // False when there is no recording to edit: the whole strip hides rather than
    // showing an empty ruler that does nothing.
    readonly property bool ready: timeline.loaded

    readonly property real stripLeft: 12
    readonly property real stripRight: 12
    readonly property real stripWidth: Math.max(1, width - stripLeft - stripRight)

    function timeAtX(x) {
        return timeline.setPlayheadRatio((x - stripLeft) / stripWidth)
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.panelBg
        border.width: 1
        border.color: Theme.strokeSoft
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: root.stripLeft
        anchors.rightMargin: root.stripRight
        anchors.topMargin: 8
        anchors.bottomMargin: 8
        spacing: 6

        // --- controls ------------------------------------------------------
        RowLayout {
            Layout.fillWidth: true
            spacing: 6

            Text {
                text: "时间线"
                color: Theme.text
                font.pixelSize: 12
                font.weight: Font.DemiBold
            }
            Text {
                text: timeline.error.length > 0
                    ? timeline.error
                    : (Number(timeline.outputRatio) < 0.999
                        ? "已剪到 " + (timeline.outputDurationMs / 1000).toFixed(1) + " 秒"
                        : "整段录制")
                color: timeline.error.length > 0 ? Theme.danger : Theme.textFaint
                font.pixelSize: 10
                elide: Text.ElideRight
                Layout.fillWidth: true
            }

            UiButton {
                text: "切分"
                implicitWidth: 54
                implicitHeight: 26
                enabled: root.ready
                onClicked: timeline.splitAtPlayhead()
            }
            UiButton {
                text: "删除 2 秒"
                implicitWidth: 82
                implicitHeight: 26
                enabled: root.ready
                onClicked: timeline.removeAroundPlayhead(2000)
            }
            UiButton {
                text: "撤销"
                implicitWidth: 54
                implicitHeight: 26
                enabled: root.ready && timeline.canUndo
                onClicked: timeline.undo()
            }
            UiButton {
                text: "重做"
                implicitWidth: 54
                implicitHeight: 26
                enabled: root.ready && timeline.canRedo
                onClicked: timeline.redo()
            }
            // Retimes the segment the playhead is inside. The values the reference
            // offers; 1× restores real time.
            Repeater {
                model: [0.5, 1.0, 2.0, 4.0]
                delegate: UiButton {
                    required property var modelData
                    text: modelData === 1.0 ? "1×" : modelData + "×"
                    implicitWidth: 38
                    implicitHeight: 26
                    enabled: root.ready
                    onClicked: timeline.setSpeedAtPlayhead(modelData)
                }
            }
            UiButton {
                text: "复原"
                implicitWidth: 54
                implicitHeight: 26
                enabled: root.ready && timeline.edited
                onClicked: timeline.reset()
            }
        }

        // --- ruler + segments + playhead ------------------------------------
        Item {
            id: track
            Layout.fillWidth: true
            Layout.preferredHeight: 30

            // Ruler ticks.
            Repeater {
                model: timeline.rulerTicks
                delegate: Item {
                    required property var modelData
                    x: root.stripLeft + modelData.ratio * root.stripWidth
                    width: 1
                    height: track.height

                    Rectangle {
                        width: 1
                        height: 5
                        color: Theme.stroke
                    }
                    Text {
                        y: 6
                        x: 3
                        text: modelData.label
                        color: Theme.textFaint
                        font.pixelSize: 9
                    }
                }
            }

            // Segment bars. A retimed segment is tinted so a speed change is visible
            // without opening anything.
            Repeater {
                model: timeline.segments
                delegate: Rectangle {
                    required property var modelData
                    x: root.stripLeft + modelData.startRatio * root.stripWidth
                    width: Math.max(1, modelData.lengthRatio * root.stripWidth)
                    height: 14
                    y: track.height - 15
                    color: modelData.retimed ? Theme.accentSoft : Theme.cardBgHover
                    border.width: 1
                    border.color: modelData.retimed ? Theme.accent : Theme.stroke
                    radius: 2

                    Text {
                        anchors.centerIn: parent
                        visible: parent.width > 42
                        text: Number(modelData.speed).toFixed(2) + "×"
                        color: Theme.textDim
                        font.pixelSize: 9
                    }
                }
            }

            // Dragging anywhere on the track moves the playhead. Declared before the
            // handles so the handles sit on top of it and win the press.
            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onPressed: mouse => root.timeAtX(mouse.x)
                onPositionChanged: mouse => { if (pressed) root.timeAtX(mouse.x) }
            }

            // Trim handles at both ends of the strip: drag to move where the output
            // starts and stops. The handle reports an *output* time, which is what the
            // trim operations take — the media clock is not what the user is dragging.
            Repeater {
                model: [
                    { edge: "start", ratio: 0 },
                    { edge: "end", ratio: 1 }
                ]
                delegate: Rectangle {
                    required property var modelData
                    x: root.stripLeft + modelData.ratio * root.stripWidth
                        - (modelData.edge === "start" ? 0 : width)
                    y: track.height - 17
                    width: 7
                    height: 18
                    radius: 2
                    color: trimHover.hovered || trimDrag.pressed ? Theme.accent : Theme.textFaint
                    opacity: 0.9

                    HoverHandler { id: trimHover; cursorShape: Qt.SizeHorCursor }
                    DragHandler {
                        id: trimDrag
                        target: null
                        onActiveChanged: {
                            if (!active) return
                            const ratio = Math.max(0, Math.min(1,
                                (centroid.scenePosition.x - track.mapToScene(0, 0).x
                                    - root.stripLeft) / root.stripWidth))
                            if (modelData.edge === "start")
                                timeline.trimStartTo(ratio * timeline.outputDurationMs)
                            else
                                timeline.trimEndTo(ratio * timeline.outputDurationMs)
                        }
                    }
                }
            }

            // The playhead. Drawn last so it is never hidden behind a segment.
            Item {
                x: root.stripLeft + timeline.playheadRatio * root.stripWidth - 1
                width: 2
                height: track.height
                Rectangle {
                    width: 2
                    height: parent.height
                    color: Theme.accent
                }
                Rectangle {
                    width: 9
                    height: 9
                    radius: 4.5
                    x: -3.5
                    color: Theme.accent
                }
            }
        }

        // --- readouts --------------------------------------------------------
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Text {
                text: "播放头 " + timeline.playheadLabel
                color: Theme.textDim
                font.pixelSize: 10
            }
            Item { Layout.fillWidth: true }
            Text {
                text: "输出 " + (timeline.outputDurationMs / 1000).toFixed(1) + " 秒"
                    + " · 原片 " + (timeline.sourceDurationMs / 1000).toFixed(1) + " 秒"
                color: Theme.textFaint
                font.pixelSize: 10
            }
        }
    }
}
