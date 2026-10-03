cmake_minimum_required(VERSION 3.24)
project(JiankuScreen VERSION 0.1.0 LANGUAGES CXX OBJCXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "14.0" CACHE STRING "Minimum macOS version")

find_package(Qt6 6.7 REQUIRED COMPONENTS Core Gui Widgets Quick QuickControls2)
qt_standard_project_setup(REQUIRES 6.7)
qt_policy(SET QTP0004 NEW)

if(NOT APPLE)
    message(FATAL_ERROR "The first capture adapter currently targets macOS. The shared core remains platform independent.")
endif()

qt_add_executable(jianku-screen MACOSX_BUNDLE
    src/main.cpp
    src/animation/AutomaticZoom.cpp
    src/animation/SpringSolver.cpp
    src/animation/CursorEngine.cpp
    src/animation/InputEvent.h
    src/animation/AutoFocusEngine.cpp
    src/animation/AnimationDriver.cpp
    src/animation/AnimationDriver.h
    src/animation/AnimationSettings.cpp
    src/animation/AnimationSettings.h
    src/project/ProjectTimeline.cpp
    src/capture/VideoFrameStore.cpp
    src/capture/VideoFrameStore.h
    src/settings/SettingsStore.cpp
    src/settings/SettingsStore.h
    src/settings/BackgroundLibrary.cpp
    src/settings/BackgroundLibrary.h
    src/settings/ScreenList.cpp
    src/settings/ScreenList.h
    src/mac/QuickScreenshot.mm
    src/mac/QuickScreenshot.h
    src/mac/MousePoll.mm
    src/mac/MousePoll.h
    src/mac/GlobalHotkey.mm
    src/mac/GlobalHotkey.h
    src/mac/MacWindowStyle.mm
    src/mac/MacWindowStyle.h
    src/mac/MacCapture.mm
    src/mac/MacCapture.h
    src/mac/MicrophoneRecorder.mm
    src/mac/MicrophoneRecorder.h
    src/mac/PointerEventRecorder.mm
    src/mac/PointerEventRecorder.h
    src/render/VideoSurface.mm
    src/render/VideoSurface.h
)

include(CTest)
if(BUILD_TESTING)
    add_executable(timeline-tests tests/TimelineTests.cpp
        src/animation/AutomaticZoom.cpp src/project/ProjectTimeline.cpp)
    target_link_libraries(timeline-tests PRIVATE Qt6::Core)
    add_test(NAME timeline-boundaries COMMAND timeline-tests)

    add_executable(spring-tests tests/SpringTests.cpp src/animation/SpringSolver.cpp)
    target_link_libraries(spring-tests PRIVATE Qt6::Core)
    add_test(NAME spring-solver COMMAND spring-tests)

    add_executable(cursor-tests tests/CursorTests.cpp
        src/animation/CursorEngine.cpp src/animation/SpringSolver.cpp
        src/animation/AnimationSettings.cpp)
    target_link_libraries(cursor-tests PRIVATE Qt6::Core)
    add_test(NAME cursor-engine COMMAND cursor-tests)

    add_executable(autofocus-tests tests/AutoFocusTests.cpp
        src/animation/AutoFocusEngine.cpp src/animation/AutomaticZoom.cpp)
    target_link_libraries(autofocus-tests PRIVATE Qt6::Core)
    add_test(NAME auto-focus COMMAND autofocus-tests)

    add_executable(media-clock-tests tests/MediaClockTests.cpp src/capture/MediaClock.h)
    target_link_libraries(media-clock-tests PRIVATE Qt6::Core)
    add_test(NAME media-clock COMMAND media-clock-tests)

    add_executable(canvas-layout-tests tests/CanvasLayoutTests.cpp src/render/CanvasLayout.cpp)
    target_link_libraries(canvas-layout-tests PRIVATE Qt6::Core)
    add_test(NAME canvas-layout COMMAND canvas-layout-tests)
endif()

set_source_files_properties(src/mac/MacCapture.mm src/mac/QuickScreenshot.mm
    src/mac/PointerEventRecorder.mm src/mac/MicrophoneRecorder.mm
    src/mac/MousePoll.mm
    src/mac/MacWindowStyle.mm
    src/mac/GlobalHotkey.mm src/render/VideoSurface.mm
    PROPERTIES COMPILE_FLAGS "-fobjc-arc")

set_source_files_properties(ui/Theme.qml PROPERTIES QT_RESOURCE_ALIAS Theme.qml QT_QML_SINGLETON_TYPE TRUE)
set_source_files_properties(ui/Main.qml PROPERTIES QT_RESOURCE_ALIAS Main.qml)
set_source_files_properties(ui/OutputWindow.qml PROPERTIES QT_RESOURCE_ALIAS OutputWindow.qml)
set_source_files_properties(ui/SettingsPage.qml PROPERTIES QT_RESOURCE_ALIAS SettingsPage.qml)
set_source_files_properties(ui/IconRail.qml PROPERTIES QT_RESOURCE_ALIAS IconRail.qml)
set_source_files_properties(ui/PreviewCanvas.qml PROPERTIES QT_RESOURCE_ALIAS PreviewCanvas.qml)
set_source_files_properties(ui/PanelCard.qml PROPERTIES QT_RESOURCE_ALIAS PanelCard.qml)
set_source_files_properties(ui/PermissionGuide.qml PROPERTIES QT_RESOURCE_ALIAS PermissionGuide.qml)
set_source_files_properties(ui/UiButton.qml PROPERTIES QT_RESOURCE_ALIAS UiButton.qml)
set_source_files_properties(ui/UiField.qml PROPERTIES QT_RESOURCE_ALIAS UiField.qml)
set_source_files_properties(ui/UiComboBox.qml PROPERTIES QT_RESOURCE_ALIAS UiComboBox.qml)
set_source_files_properties(ui/UiSlider.qml PROPERTIES QT_RESOURCE_ALIAS UiSlider.qml)
set_source_files_properties(ui/UiToggle.qml PROPERTIES QT_RESOURCE_ALIAS UiToggle.qml)
set_source_files_properties(ui/UiSegmented.qml PROPERTIES QT_RESOURCE_ALIAS UiSegmented.qml)
set_source_files_properties(ui/UiColorField.qml PROPERTIES QT_RESOURCE_ALIAS UiColorField.qml)

qt_add_qml_module(jianku-screen
    URI Jianku.Screen
    VERSION 1.0
    QML_FILES
        ui/Theme.qml
        ui/Main.qml
        ui/OutputWindow.qml
        ui/SettingsPage.qml
        ui/IconRail.qml
        ui/PreviewCanvas.qml
        ui/PanelCard.qml
        ui/PermissionGuide.qml
        ui/UiButton.qml
        ui/UiField.qml
        ui/UiComboBox.qml
        ui/UiSlider.qml
        ui/UiToggle.qml
        ui/UiSegmented.qml
        ui/UiColorField.qml
)

target_link_libraries(jianku-screen PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Quick Qt6::QuickControls2
    "-framework ScreenCaptureKit"
    "-framework CoreMedia"
    "-framework CoreVideo"
    "-framework CoreGraphics"
    "-framework Metal"
    "-framework Foundation"
    "-framework Carbon"
    "-framework AppKit"
    "-framework AVFoundation"
)

set_target_properties(jianku-screen PROPERTIES
    OUTPUT_NAME "Jianku Screen"
    MACOSX_BUNDLE_BUNDLE_NAME "Jianku Screen"
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.jianku.screen"
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/src/mac/Info.plist.in"
)

target_compile_definitions(jianku-screen PRIVATE
    JIANKU_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")

add_custom_command(TARGET jianku-screen POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/backgrounds"
        "$<TARGET_BUNDLE_CONTENT_DIR:jianku-screen>/Resources/backgrounds"
    COMMENT "Bundling background assets")

# Re-sign with a stable identity so TCC permissions survive rebuilds.
add_custom_command(TARGET jianku-screen POST_BUILD
    COMMAND /bin/sh "${CMAKE_CURRENT_SOURCE_DIR}/scripts/sign.sh" "$<TARGET_BUNDLE_DIR:jianku-screen>"
    COMMENT "Signing app with stable JiankuDev identity")
