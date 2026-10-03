import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import QtQuick.Effects

Item {
    id: page
    property string section: "background"

    readonly property var gradientPresets: [
        { name: "极光", a: "#3F37C9", b: "#8C87DF", ang: 135 },
        { name: "午夜", a: "#0F2027", b: "#2C5364", ang: 160 },
        { name: "能量", a: "#F12711", b: "#F5AF19", ang: 120 },
        { name: "晨光", a: "#2193B0", b: "#6DD5ED", ang: 135 },
        { name: "落日", a: "#EE0979", b: "#FF6A00", ang: 120 },
        { name: "春日", a: "#11998E", b: "#38EF7D", ang: 135 },
        { name: "深空", a: "#232526", b: "#414345", ang: 150 },
        { name: "海岸", a: "#2B5876", b: "#4E4376", ang: 140 },
        { name: "薄雾", a: "#B993D6", b: "#8CA6DB", ang: 130 },
        { name: "霓虹", a: "#654EA3", b: "#EAAFC8", ang: 125 },
        { name: "珊瑚", a: "#FF5F6D", b: "#FFC371", ang: 120 },
        { name: "森林", a: "#134E5E", b: "#71B280", ang: 145 }
    ]

    readonly property var sectionTitles: ({
        record: ["录制", "保存目录与录制工程"],
        background: ["背景", "画布背景、留白、圆角、内框与阴影"],
        cursor: ["光标", "大小、平滑、旋转、点击反馈"],
        motion: ["缩放与聚焦", "自动聚焦与镜头运动"],
        camera: ["摄像头", "画中画摄像头布局"],
        audio: ["音频", "麦克风、系统声音与音乐"],
        transcript: ["字幕", "自动转写字幕样式"],
        shortcuts: ["快捷键", "按键提示的显示方式"],
        shortcuts2: ["", ""],
        speed: ["速度", "播放与变速"],
        layout: ["布局", "来源与摄像头排布"],
        screenshot: ["快捷截图", "全局快捷键与输出目录"]
    })

    function setSlider(key, v) { settings.setCurrent(key, v) }

    Rectangle {
        anchors.fill: parent
        color: Theme.panelBg
        border.width: 1
        border.color: Theme.strokeSoft
    }

    FileDialog {
        id: backgroundPicker
        title: "选择背景图片"
        nameFilters: ["图片文件 (*.png *.jpg *.jpeg *.webp *.heic)", "所有文件 (*)"]
        onAccepted: {
            settings.setCurrent("backgroundImagePath", selectedFile.toLocalFile())
            settings.setCurrent("backgroundType", "image")
        }
    }
    FolderDialog {
        id: outputFolderPicker
        title: "选择截图保存目录"
        onAccepted: settings.setCurrent("screenshotDirectory", selectedFolder.toLocalFile())
    }
    FolderDialog {
        id: recordingFolderPicker
        title: "选择录制保存目录"
        onAccepted: settings.setCurrent("recordingDirectory", selectedFolder.toLocalFile())
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 58
            color: "transparent"
            ColumnLayout {
                anchors.fill: parent
                anchors.leftMargin: 16
                anchors.rightMargin: 16
                spacing: 2
                Text {
                    text: page.sectionTitles[page.section] ? page.sectionTitles[page.section][0] : ""
                    color: Theme.text
                    font.pixelSize: 15
                    font.weight: Font.DemiBold
                }
                Text {
                    Layout.fillWidth: true
                    text: page.sectionTitles[page.section] ? page.sectionTitles[page.section][1] : ""
                    color: Theme.textFaint
                    font.pixelSize: 11
                    elide: Text.ElideRight
                }
            }
        }

        ScrollView {
            id: scroller
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            contentWidth: availableWidth

            ColumnLayout {
                width: scroller.availableWidth - 24
                x: 12
                spacing: 12

                // ---------------- Background ----------------
                PanelCard {
                    Layout.fillWidth: true
                    visible: page.section === "background"
                    title: "背景画布"
                    resetKeys: ["backgroundType", "backgroundColor", "gradientStartColor", "gradientEndColor", "gradientAngle", "backgroundImagePath"]

                    UiSegmented {
                        Layout.fillWidth: true
                        options: ["图片库", "渐变", "纯色", "本地图片"]
                        currentIndex: Math.max(0, ["system", "gradient", "color", "image"].indexOf(String(settings.current.backgroundType)))
                        onActivated: (i) => settings.setCurrent("backgroundType", ["system", "gradient", "color", "image"][i])
                    }

                    GridView {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 200
                        visible: String(settings.current.backgroundType) === "system"
                        cellWidth: Math.floor(width / 4)
                        cellHeight: 64
                        clip: true
                        model: backgrounds.entries
                        delegate: Item {
                            required property var modelData
                            width: GridView.view.cellWidth
                            height: GridView.view.cellHeight
                            readonly property bool selected: String(settings.current.backgroundSystemName) === modelData.relative
                            Rectangle {
                                id: thumb
                                anchors.fill: parent
                                anchors.margins: 4
                                radius: Theme.radiusSm
                                color: "transparent"
                                border.width: selected ? 2 : 1
                                border.color: selected ? Theme.accent : "#55000000"
                            }
                            Rectangle {
                                id: thumbMask
                                x: thumb.x
                                y: thumb.y
                                width: thumb.width
                                height: thumb.height
                                radius: Theme.radiusSm
                                color: "white"
                                visible: false
                                layer.enabled: true
                            }
                            Image {
                                x: thumb.x
                                y: thumb.y
                                width: thumb.width
                                height: thumb.height
                                source: modelData.url
                                sourceSize.width: 220
                                sourceSize.height: 140
                                fillMode: Image.PreserveAspectCrop
                                asynchronous: true
                                cache: true
                                layer.enabled: true
                                layer.effect: MultiEffect {
                                    maskEnabled: true
                                    maskSource: thumbMask
                                    maskThresholdMin: 0.5
                                    maskSpreadAtMin: 0.0
                                }
                            }
                            HoverHandler { cursorShape: Qt.PointingHandCursor }
                            TapHandler {
                                onTapped: {
                                    settings.setCurrent("backgroundSystemName", modelData.relative)
                                    settings.setCurrent("backgroundType", "system")
                                }
                            }
                        }
                    }

                    GridView {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 96
                        visible: String(settings.current.backgroundType) === "gradient"
                        cellWidth: Math.floor(width / 6)
                        cellHeight: 46
                        interactive: false
                        clip: true
                        model: page.gradientPresets
                        delegate: Item {
                            required property var modelData
                            width: GridView.view.cellWidth
                            height: GridView.view.cellHeight
                            Rectangle {
                                anchors.centerIn: parent
                                width: parent.width - 8
                                height: parent.height - 8
                                radius: Theme.radiusSm
                                border.width: selected.value ? 2 : 1
                                border.color: selected.value ? Theme.accent : "#55000000"
                                gradient: Gradient {
                                    orientation: Gradient.Horizontal
                                    GradientStop { position: 0.0; color: modelData.a }
                                    GradientStop { position: 1.0; color: modelData.b }
                                }
                                readonly property bool selected: Qt.colorEqual(String(settings.current.gradientStartColor), modelData.a)
                                HoverHandler { cursorShape: Qt.PointingHandCursor }
                                TapHandler {
                                    onTapped: {
                                        settings.setCurrent("gradientStartColor", modelData.a)
                                        settings.setCurrent("gradientEndColor", modelData.b)
                                        settings.setCurrent("gradientAngle", modelData.ang)
                                    }
                                }
                            }
                        }
                    }

                    UiColorField {
                        Layout.fillWidth: true
                        visible: String(settings.current.backgroundType) === "color"
                        label: "背景颜色"
                        value: String(settings.current.backgroundColor)
                        onEdited: v => settings.setCurrent("backgroundColor", v)
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        visible: String(settings.current.backgroundType) === "image"
                        spacing: 8
                        Text {
                            Layout.fillWidth: true
                            text: String(settings.current.backgroundImagePath) || "未选择图片"
                            color: Theme.textDim
                            font.pixelSize: 11
                            elide: Text.ElideMiddle
                        }
                        UiButton { implicitWidth: 64; text: "选取"; tone: "quiet"; onClicked: backgroundPicker.open() }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        visible: String(settings.current.backgroundType) === "gradient"
                        spacing: 8
                        UiColorField {
                            Layout.fillWidth: true
                            label: "起始"
                            value: String(settings.current.gradientStartColor)
                            onEdited: v => settings.setCurrent("gradientStartColor", v)
                        }
                        UiColorField {
                            Layout.fillWidth: true
                            label: "结束"
                            value: String(settings.current.gradientEndColor)
                            onEdited: v => settings.setCurrent("gradientEndColor", v)
                        }
                    }
                    UiSlider {
                        Layout.fillWidth: true
                        visible: String(settings.current.backgroundType) === "gradient"
                        label: "渐变角度"
                        from: 0; to: 360; step: 1; suffix: "°"
                        value: Number(settings.current.gradientAngle || 0)
                        onEdited: v => page.setSlider("gradientAngle", v)
                    }
                    UiSlider {
                        Layout.fillWidth: true
                        label: "背景模糊"
                        from: 0; to: 40; step: 1
                        value: Number(settings.current.backgroundBlur || 0)
                        onEdited: v => page.setSlider("backgroundBlur", v)
                    }
                }

                PanelCard {
                    Layout.fillWidth: true
                    visible: page.section === "background"
                    title: "留白与边框"
                    resetKeys: ["backgroundPaddingRatio", "windowBorderRadius", "insetSize", "insetColor", "insetAlpha", "shadowIntensity", "shadowAngle", "shadowDistance", "shadowBlur"]

                    UiSlider {
                        Layout.fillWidth: true
                        label: "留白"
                        detail: "画面四周留白，按画布短边百分比"
                        from: 0; to: 30; step: 0.5; decimals: 1; suffix: "%"
                        value: Number(settings.current.backgroundPaddingRatio || 0)
                        onEdited: v => page.setSlider("backgroundPaddingRatio", v)
                    }
                    UiSlider {
                        Layout.fillWidth: true
                        label: "圆角"
                        from: 0; to: 80; step: 1; suffix: " px"
                        value: Number(settings.current.windowBorderRadius || 0)
                        onEdited: v => page.setSlider("windowBorderRadius", v)
                    }
                    UiSlider {
                        Layout.fillWidth: true
                        label: "内框粗细"
                        from: 0; to: 40; step: 1; suffix: " px"
                        value: Number(settings.current.insetSize || 0)
                        onEdited: v => page.setSlider("insetSize", v)
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        UiColorField {
                            Layout.fillWidth: true
                            label: "内框颜色"
                            value: String(settings.current.insetColor)
                            onEdited: v => settings.setCurrent("insetColor", v)
                        }
                    }
                    UiSlider {
                        Layout.fillWidth: true
                        label: "内框不透明度"
                        from: 0; to: 100; step: 1; suffix: "%"
                        value: Number(settings.current.insetAlpha || 0) * 100
                        onEdited: v => page.setSlider("insetAlpha", v / 100)
                    }
                }

                PanelCard {
                    Layout.fillWidth: true
                    visible: page.section === "background"
                    title: "阴影"
                    resetKeys: ["shadowIntensity", "shadowAngle", "shadowDistance", "shadowBlur"]

                    UiSlider {
                        Layout.fillWidth: true
                        label: "强度"
                        from: 0; to: 1; step: 0.01; decimals: 2
                        value: Number(settings.current.shadowIntensity || 0)
                        onEdited: v => page.setSlider("shadowIntensity", v)
                    }
                    UiSlider {
                        Layout.fillWidth: true
                        label: "方向"
                        from: 0; to: 360; step: 1; suffix: "°"
                        value: Number(settings.current.shadowAngle || 0)
                        onEdited: v => page.setSlider("shadowAngle", v)
                    }
                    UiSlider {
                        Layout.fillWidth: true
                        label: "距离"
                        from: 0; to: 80; step: 1; suffix: " px"
                        value: Number(settings.current.shadowDistance || 0)
                        onEdited: v => page.setSlider("shadowDistance", v)
                    }
                    UiSlider {
                        Layout.fillWidth: true
                        label: "柔化"
                        from: 0; to: 80; step: 1; suffix: " px"
                        value: Number(settings.current.shadowBlur || 0)
                        onEdited: v => page.setSlider("shadowBlur", v)
                    }
                }

                PanelCard {
                    Layout.fillWidth: true
                    visible: page.section === "background"
                    title: "输出比例"
                    resetKeys: ["outputAspectRatio"]
                    UiSegmented {
                        Layout.fillWidth: true
                        options: ["自动", "16:9", "16:10", "4:3", "1:1", "9:16", "21:9"]
                        currentIndex: Math.max(0, ["auto", "16:9", "16:10", "4:3", "1:1", "9:16", "21:9"].indexOf(String(settings.current.outputAspectRatio)))
                        onActivated: i => settings.setCurrent("outputAspectRatio", ["auto", "16:9", "16:10", "4:3", "1:1", "9:16", "21:9"][i])
                    }
                }

                // ---------------- Cursor ----------------
                PanelCard {
                    Layout.fillWidth: true
                    visible: page.section === "cursor"
                    title: "光标外观"
                    resetKeys: ["cursorSize", "cursorRotateOnXMovementRatio", "cursorBaseRotation", "hideCursor", "hideNotMovingCursorAfterMs", "clickEffect"]

                    UiSlider {
                        Layout.fillWidth: true
                        label: "光标大小"
                        from: 0.5; to: 10; step: 0.1; decimals: 1
                        value: Number(settings.current.cursorSize || 1)
                        onEdited: v => page.setSlider("cursorSize", v)
                    }
                    UiSlider {
                        Layout.fillWidth: true
                        label: "水平旋转比例"
                        detail: "水平移动速度→倾斜角度"
                        from: 0; to: 1; step: 0.01; decimals: 2
                        value: Number(settings.current.cursorRotateOnXMovementRatio || 0)
                        onEdited: v => page.setSlider("cursorRotateOnXMovementRatio", v)
                    }
                    UiSlider {
                        Layout.fillWidth: true
                        label: "基础旋转"
                        from: -20; to: 20; step: 1; suffix: "°"
                        value: Number(settings.current.cursorBaseRotation || 0)
                        onEdited: v => page.setSlider("cursorBaseRotation", v)
                    }
                    UiToggle {
                        Layout.fillWidth: true
                        label: "隐藏光标"
                        checked: !!settings.current.hideCursor
                        onToggled: v => settings.setCurrent("hideCursor", v)
                    }
                    UiSlider {
                        Layout.fillWidth: true
                        label: "静止自动隐藏"
                        detail: "0 表示不隐藏"
                        from: 0; to: 3000; step: 50; suffix: " ms"
                        value: Number(settings.current.hideNotMovingCursorAfterMs || 0)
                        onEdited: v => page.setSlider("hideNotMovingCursorAfterMs", v)
                    }
                    UiSegmented {
                        Layout.fillWidth: true
                        options: ["无点击反馈", "缩放反馈"]
                        currentIndex: String(settings.current.clickEffect) === "scale" ? 1 : 0
                        onActivated: i => settings.setCurrent("clickEffect", i === 1 ? "scale" : "none")
                    }
                }

                PanelCard {
                    Layout.fillWidth: true
                    visible: page.section === "cursor"
                    title: "平滑求解"
                    resetKeys: ["cursorSmoothing", "disableMouseMovementSpring", "mouseMovementSpring"]

                    UiSegmented {
                        Layout.fillWidth: true
                        options: ["平滑", "中等", "快速", "无"]
                        currentIndex: Math.max(0, ["Smooth", "Medium", "Rapid", "None"].indexOf(String(settings.current.cursorSmoothing)))
                        onActivated: index => {
                            const keys = ["Smooth", "Medium", "Rapid", "None"]
                            const presets = [
                                { stiffness: 470, damping: 70, mass: 3 },
                                { stiffness: 340, damping: 60, mass: 3 },
                                { stiffness: 530, damping: 40, mass: 1 }
                            ]
                            settings.setCurrent("cursorSmoothing", keys[index])
                            if (index < 3) settings.setCurrent("mouseMovementSpring", presets[index])
                        }
                    }
                    UiToggle {
                        Layout.fillWidth: true
                        label: "关闭平滑"
                        checked: !!settings.current.disableMouseMovementSpring
                        onToggled: v => settings.setCurrent("disableMouseMovementSpring", v)
                    }
                    UiSlider {
                        Layout.fillWidth: true
                        label: "刚度"
                        from: 0; to: 1000; step: 1
                        value: Number(settings.current.mouseMovementSpring.stiffness || 0)
                        onEdited: v => page.setSlider("mouseMovementSpring.stiffness", v)
                    }
                    UiSlider {
                        Layout.fillWidth: true
                        label: "阻尼"
                        from: 0; to: 200; step: 1
                        value: Number(settings.current.mouseMovementSpring.damping || 0)
                        onEdited: v => page.setSlider("mouseMovementSpring.damping", v)
                    }
                    UiSlider {
                        Layout.fillWidth: true
                        label: "质量"
                        from: 0; to: 10; step: 0.05; decimals: 2
                        value: Number(settings.current.mouseMovementSpring.mass || 0)
                        onEdited: v => page.setSlider("mouseMovementSpring.mass", v)
                    }
                }

                // ---------------- Motion / zoom ----------------
                PanelCard {
                    Layout.fillWidth: true
                    visible: page.section === "motion"
                    title: "自动聚焦"
                    resetKeys: ["autoZoom", "defaultZoomLevel", "alwaysKeepZoomedIn", "snapToEdgesRatio", "glideSpeed"]

                    UiToggle {
                        Layout.fillWidth: true
                        label: "自动缩放"
                        detail: "围绕点击自动放大"
                        checked: !!settings.current.autoZoom
                        onToggled: v => settings.setCurrent("autoZoom", v)
                    }
                    UiSlider {
                        Layout.fillWidth: true
                        label: "默认倍率"
                        from: 1; to: 4; step: 0.1; decimals: 1; suffix: "×"
                        value: Number(settings.current.defaultZoomLevel || 2)
                        onEdited: v => page.setSlider("defaultZoomLevel", v)
                    }
                    UiToggle {
                        Layout.fillWidth: true
                        label: "始终保留放大"
                        checked: !!settings.current.alwaysKeepZoomedIn
                        onToggled: v => settings.setCurrent("alwaysKeepZoomedIn", v)
                    }
                    UiSlider {
                        Layout.fillWidth: true
                        label: "边缘吸附"
                        from: 0; to: 0.5; step: 0.01; decimals: 2
                        value: Number(settings.current.snapToEdgesRatio || 0)
                        onEdited: v => page.setSlider("snapToEdgesRatio", v)
                    }
                    UiSlider {
                        Layout.fillWidth: true
                        label: "滑翔速度"
                        from: 0; to: 1; step: 0.01; decimals: 2
                        value: Number(settings.current.glideSpeed || 0)
                        onEdited: v => page.setSlider("glideSpeed", v)
                    }
                }

                PanelCard {
                    Layout.fillWidth: true
                    visible: page.section === "motion"
                    title: "画面与点击弹簧"
                    resetKeys: ["screenMovementSpring", "mouseClickSpring"]

                    UiSlider {
                        Layout.fillWidth: true
                        label: "画面刚度"
                        from: 0; to: 500; step: 1
                        value: Number(settings.current.screenMovementSpring.stiffness || 0)
                        onEdited: v => page.setSlider("screenMovementSpring.stiffness", v)
                    }
                    UiSlider {
                        Layout.fillWidth: true
                        label: "画面阻尼"
                        from: 0; to: 150; step: 1
                        value: Number(settings.current.screenMovementSpring.damping || 0)
                        onEdited: v => page.setSlider("screenMovementSpring.damping", v)
                    }
                    UiSlider {
                        Layout.fillWidth: true
                        label: "画面质量"
                        from: 0; to: 8; step: 0.05; decimals: 2
                        value: Number(settings.current.screenMovementSpring.mass || 0)
                        onEdited: v => page.setSlider("screenMovementSpring.mass", v)
                    }
                    UiSlider {
                        Layout.fillWidth: true
                        label: "点击刚度"
                        from: 0; to: 1500; step: 1
                        value: Number(settings.current.mouseClickSpring.stiffness || 0)
                        onEdited: v => page.setSlider("mouseClickSpring.stiffness", v)
                    }
                    UiSlider {
                        Layout.fillWidth: true
                        label: "点击阻尼"
                        from: 0; to: 150; step: 1
                        value: Number(settings.current.mouseClickSpring.damping || 0)
                        onEdited: v => page.setSlider("mouseClickSpring.damping", v)
                    }
                    UiSlider {
                        Layout.fillWidth: true
                        label: "点击质量"
                        from: 0; to: 8; step: 0.05; decimals: 2
                        value: Number(settings.current.mouseClickSpring.mass || 0)
                        onEdited: v => page.setSlider("mouseClickSpring.mass", v)
                    }
                }

                // ---------------- Camera ----------------
                PanelCard {
                    Layout.fillWidth: true
                    visible: page.section === "camera"
                    title: "摄像头"
                    resetKeys: ["hideCamera", "cameraSize", "cameraRoundness", "cameraScaleDuringZoom", "mirrorCamera", "cameraAspectRatio"]

                    UiToggle {
                        Layout.fillWidth: true
                        label: "显示摄像头"
                        checked: !settings.current.hideCamera
                        onToggled: v => settings.setCurrent("hideCamera", !v)
                    }
                    UiSlider {
                        Layout.fillWidth: true
                        label: "大小"
                        from: 0.05; to: 1; step: 0.01; decimals: 2
                        value: Number(settings.current.cameraSize || 0)
                        onEdited: v => page.setSlider("cameraSize", v)
                    }
                    UiSlider {
                        Layout.fillWidth: true
                        label: "圆角"
                        from: 0; to: 1; step: 0.01; decimals: 2
                        value: Number(settings.current.cameraRoundness || 0)
                        onEdited: v => page.setSlider("cameraRoundness", v)
                    }
                    UiSlider {
                        Layout.fillWidth: true
                        label: "缩放时缩放比"
                        from: 0; to: 1; step: 0.01; decimals: 2
                        value: Number(settings.current.cameraScaleDuringZoom || 0)
                        onEdited: v => page.setSlider("cameraScaleDuringZoom", v)
                    }
                    UiToggle {
                        Layout.fillWidth: true
                        label: "水平镜像"
                        checked: !!settings.current.mirrorCamera
                        onToggled: v => settings.setCurrent("mirrorCamera", v)
                    }
                    UiSegmented {
                        Layout.fillWidth: true
                        options: ["原始", "16:9", "4:3"]
                        currentIndex: Math.max(0, ["original", "16:9", "4:3"].indexOf(String(settings.current.cameraAspectRatio)))
                        onActivated: i => settings.setCurrent("cameraAspectRatio", ["original", "16:9", "4:3"][i])
                    }
                }

                // ---------------- Audio ----------------
                PanelCard {
                    Layout.fillWidth: true
                    visible: page.section === "audio"
                    title: "音频"
                    resetKeys: ["audioVolume", "systemAudioVolume", "muteMicrophone", "muteSystemAudio", "muteExternalDeviceAudio", "improveMicrophoneAudio"]

                    UiSlider {
                        Layout.fillWidth: true
                        label: "麦克风音量"
                        from: 0; to: 1; step: 0.01; decimals: 2
                        value: Number(settings.current.audioVolume || 0)
                        onEdited: v => page.setSlider("audioVolume", v)
                    }
                    UiSlider {
                        Layout.fillWidth: true
                        label: "系统声音音量"
                        from: 0; to: 1; step: 0.01; decimals: 2
                        value: Number(settings.current.systemAudioVolume || 0)
                        onEdited: v => page.setSlider("systemAudioVolume", v)
                    }
                    UiToggle { Layout.fillWidth: true; label: "静音麦克风"; checked: !!settings.current.muteMicrophone; onToggled: v => settings.setCurrent("muteMicrophone", v) }
                    UiToggle { Layout.fillWidth: true; label: "静音系统声音"; checked: !!settings.current.muteSystemAudio; onToggled: v => settings.setCurrent("muteSystemAudio", v) }
                    UiToggle { Layout.fillWidth: true; label: "静音外部设备"; checked: !!settings.current.muteExternalDeviceAudio; onToggled: v => settings.setCurrent("muteExternalDeviceAudio", v) }
                    UiToggle { Layout.fillWidth: true; label: "增强麦克风"; checked: !!settings.current.improveMicrophoneAudio; onToggled: v => settings.setCurrent("improveMicrophoneAudio", v) }
                }

                PanelCard {
                    Layout.fillWidth: true
                    visible: page.section === "audio"
                    title: "背景音乐与点击音"
                    resetKeys: ["backgroundAudioVolume", "muteBackgroundAudio", "clickSoundEffectVolume"]

                    UiSlider {
                        Layout.fillWidth: true
                        label: "背景音乐音量"
                        from: 0; to: 1; step: 0.01; decimals: 2
                        value: Number(settings.current.backgroundAudioVolume || 0)
                        onEdited: v => page.setSlider("backgroundAudioVolume", v)
                    }
                    UiToggle { Layout.fillWidth: true; label: "静音背景音乐"; checked: !!settings.current.muteBackgroundAudio; onToggled: v => settings.setCurrent("muteBackgroundAudio", v) }
                    UiSlider {
                        Layout.fillWidth: true
                        label: "点击音效音量"
                        from: 0; to: 1; step: 0.01; decimals: 2
                        value: Number(settings.current.clickSoundEffectVolume || 0)
                        onEdited: v => page.setSlider("clickSoundEffectVolume", v)
                    }
                }

                // ---------------- Transcript ----------------
                PanelCard {
                    Layout.fillWidth: true
                    visible: page.section === "transcript"
                    title: "字幕"
                    resetKeys: ["showTranscript", "transcriptSizeRatio"]
                    UiToggle { Layout.fillWidth: true; label: "显示字幕"; checked: !!settings.current.showTranscript; onToggled: v => settings.setCurrent("showTranscript", v) }
                    UiSlider {
                        Layout.fillWidth: true
                        label: "字号比例"
                        from: 0.5; to: 2; step: 0.05; decimals: 2
                        value: Number(settings.current.transcriptSizeRatio || 1)
                        onEdited: v => page.setSlider("transcriptSizeRatio", v)
                    }
                }

                // ---------------- Shortcuts ----------------
                PanelCard {
                    Layout.fillWidth: true
                    visible: page.section === "shortcuts"
                    title: "快捷键提示"
                    resetKeys: ["showShortcuts", "shortcutsSizeRatio", "showShortcutsWithSingleLetters"]
                    UiToggle { Layout.fillWidth: true; label: "显示快捷键"; checked: !!settings.current.showShortcuts; onToggled: v => settings.setCurrent("showShortcuts", v) }
                    UiSlider {
                        Layout.fillWidth: true
                        label: "大小比例"
                        from: 0.5; to: 2; step: 0.05; decimals: 2
                        value: Number(settings.current.shortcutsSizeRatio || 1)
                        onEdited: v => page.setSlider("shortcutsSizeRatio", v)
                    }
                    UiToggle { Layout.fillWidth: true; label: "显示单字母按键"; checked: !!settings.current.showShortcutsWithSingleLetters; onToggled: v => settings.setCurrent("showShortcutsWithSingleLetters", v) }
                }

                // ---------------- Speed ----------------
                PanelCard {
                    Layout.fillWidth: true
                    visible: page.section === "speed"
                    title: "播放速度"
                    resetKeys: ["playbackSpeed"]
                    UiSlider {
                        Layout.fillWidth: true
                        label: "速度"
                        from: 0.25; to: 4; step: 0.05; decimals: 2; suffix: "×"
                        value: Number(settings.current.playbackSpeed || 1)
                        onEdited: v => page.setSlider("playbackSpeed", v)
                    }
                }

                // ---------------- Layout ----------------
                PanelCard {
                    Layout.fillWidth: true
                    visible: page.section === "layout"
                    title: "布局"
                    resetKeys: ["cameraSize", "cameraRoundness"]
                    UiSegmented {
                        Layout.fillWidth: true
                        options: ["画中画", "两者"]
                        currentIndex: 0
                        onActivated: i => {}
                    }
                    Text {
                        Layout.fillWidth: true
                        text: "来源与摄像头的排布跟随画布设置；完整编辑在后续版本接入。"
                        color: Theme.textFaint
                        font.pixelSize: 10
                        wrapMode: Text.Wrap
                    }
                }

                // ---------------- Recording ----------------
                PanelCard {
                    Layout.fillWidth: true
                    visible: page.section === "record"
                    title: "录制与保存"
                    resetKeys: ["recordingDirectory"]

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        Text {
                            Layout.fillWidth: true
                            text: String(settings.current.recordingDirectory)
                            color: Theme.textDim
                            font.pixelSize: 11
                            elide: Text.ElideMiddle
                        }
                        UiButton { implicitWidth: 64; text: "选取"; tone: "quiet"; onClicked: recordingFolderPicker.open() }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        UiButton {
                            Layout.fillWidth: true
                            text: "打开录制目录"
                            tone: "quiet"
                            onClicked: capture.openRecordingDirectory()
                        }
                        UiButton {
                            Layout.fillWidth: true
                            text: "打开最近工程"
                            tone: "quiet"
                            enabled: capture.lastProjectPath.length > 0
                            onClicked: capture.openLastProject()
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        text: "每次录制会在该目录下新建一个 .jianku 工程包，里面保存原始视频、麦克风音轨、指针事件与时间轴。"
                        color: Theme.textFaint
                        font.pixelSize: 10
                        wrapMode: Text.Wrap
                    }
                }

                // ---------------- Screenshot ----------------
                PanelCard {
                    Layout.fillWidth: true
                    visible: page.section === "screenshot"
                    title: "快捷截图"
                    resetKeys: ["screenshotHotkey", "screenshotDirectory"]
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        Text { text: "全局快捷键"; color: Theme.text; font.pixelSize: 12 }
                        Item { Layout.fillWidth: true }
                        UiField {
                            Layout.preferredWidth: 150
                            text: String(settings.current.screenshotHotkey)
                            onEditingFinished: settings.setCurrent("screenshotHotkey", text)
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        Text {
                            Layout.fillWidth: true
                            text: String(settings.current.screenshotDirectory)
                            color: Theme.textDim
                            font.pixelSize: 11
                            elide: Text.ElideMiddle
                        }
                        UiButton { implicitWidth: 64; text: "选取"; tone: "quiet"; onClicked: outputFolderPicker.open() }
                    }
                    UiButton {
                        Layout.fillWidth: true
                        text: "立即截图"
                        tone: "primary"
                        onClicked: screenshot.capture(settings.current)
                    }
                    Text {
                        Layout.fillWidth: true
                        text: hotkey.status
                        color: Theme.textFaint
                        font.pixelSize: 10
                    }
                }

                Item { Layout.preferredHeight: 8 }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 46
            color: "transparent"
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                spacing: 8
                UiButton {
                    implicitWidth: 120
                    text: "保存为默认"
                    tone: "quiet"
                    onClicked: {
                        const keys = Object.keys(settings.factory)
                        for (let i = 0; i < keys.length; ++i)
                            settings.setAsDefault(keys[i])
                    }
                }
                UiButton {
                    implicitWidth: 110
                    text: "恢复参考基线"
                    tone: "ghost"
                    onClicked: settings.restoreAllFactory()
                }
                Item { Layout.fillWidth: true }
                Text { text: "修改即时生效"; color: Theme.textFaint; font.pixelSize: 10 }
            }
        }
    }
}
