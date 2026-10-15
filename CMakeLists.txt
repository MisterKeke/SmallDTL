cmake_minimum_required(VERSION 3.24)

project(SmallDTL VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Quick QuickControls2 Widgets Concurrent Network Sql)
qt_standard_project_setup(REQUIRES 6.8)
include(CTest)

qt_add_executable(ScreenshotHotkeyManager WIN32
    src/main.cpp
    src/app/AppDataPaths.h
    src/app/AppDataPaths.cpp
    src/actions/ActionManager.h
    src/actions/ActionDefinition.h
    src/app/JsonValidation.h
    src/screenshots/ThumbnailProvider.h
    src/actions/ActionManager.cpp
    src/actions/ActionSequenceRunner.h
    src/actions/ActionSequenceRunner.cpp
    src/app/ApplicationController.h
    src/app/ApplicationController.cpp
    src/settings/SettingsManager.h
    src/settings/SettingsManager.cpp
    src/profiles/ProfileManager.h
    src/profiles/ProfileManager.cpp
    src/screenshots/ScreenshotManager.h
    src/screenshots/ScreenshotManager.cpp
    src/screenshots/ScreenshotDatabase.h
    src/screenshots/ScreenshotDatabase.cpp
    src/screenshots/ScreenshotEditorImage.h
    src/screenshots/ScreenshotEditorImage.cpp
    src/hotkeys/HotkeyManager.h
    src/hotkeys/HotkeyManager.cpp
)

set_target_properties(ScreenshotHotkeyManager PROPERTIES OUTPUT_NAME SmallDTL)
if(WIN32)
    enable_language(RC)
    configure_file(src/app/SmallDTL.rc.in "${CMAKE_CURRENT_BINARY_DIR}/SmallDTL.rc" @ONLY)
    target_sources(ScreenshotHotkeyManager PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/SmallDTL.rc")
endif()

qt_add_qml_module(ScreenshotHotkeyManager
    URI ScreenshotHotkeyManager
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/components/Sidebar.qml
        qml/components/HotkeyRecorder.qml
        qml/components/ScreenshotEditor.qml
        qml/pages/DashboardPage.qml
        qml/pages/ScreenshotsPage.qml
        qml/pages/ProfilesPage.qml
        qml/pages/HotkeysPage.qml
        qml/pages/ActionsPage.qml
        qml/pages/SettingsPage.qml
)

target_link_libraries(ScreenshotHotkeyManager PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Widgets
    Qt6::Concurrent
    Qt6::Network
    Qt6::Sql
)

target_compile_definitions(ScreenshotHotkeyManager PRIVATE
    QT_NO_CAST_FROM_ASCII
    QT_NO_CAST_TO_ASCII
)

if(WIN32)
    target_link_libraries(ScreenshotHotkeyManager PRIVATE user32 shell32 psapi)
endif()

if(BUILD_TESTING)
    find_package(Qt6 6.8 REQUIRED COMPONENTS Test)
    qt_add_executable(ScreenshotHotkeyManagerTests
        tests/UnitTests.cpp
        src/app/ApplicationController.h
        src/app/ApplicationController.cpp
        src/hotkeys/HotkeyManager.h
        src/hotkeys/HotkeyManager.cpp
        src/app/AppDataPaths.h
        src/app/AppDataPaths.cpp
        src/actions/ActionManager.h
        src/actions/ActionManager.cpp
        src/actions/ActionSequenceRunner.h
        src/actions/ActionSequenceRunner.cpp
        src/settings/SettingsManager.h
        src/settings/SettingsManager.cpp
        src/profiles/ProfileManager.h
        src/profiles/ProfileManager.cpp
        src/screenshots/ScreenshotManager.h
        src/screenshots/ScreenshotManager.cpp
        src/screenshots/ScreenshotDatabase.h
        src/screenshots/ScreenshotDatabase.cpp
        src/screenshots/ScreenshotEditorImage.h
        src/screenshots/ScreenshotEditorImage.cpp
    )
    target_link_libraries(ScreenshotHotkeyManagerTests PRIVATE
        Qt6::Core Qt6::Gui Qt6::Quick Qt6::QuickControls2 Qt6::Widgets Qt6::Concurrent Qt6::Sql Qt6::Test
    )
    qt_add_qml_module(ScreenshotHotkeyManagerTests
        URI ScreenshotHotkeyManagerTest
        VERSION 1.0
        OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/test-qml/ScreenshotHotkeyManagerTest"
        QML_FILES
            qml/Main.qml
            qml/components/Sidebar.qml
            qml/components/HotkeyRecorder.qml
            qml/pages/DashboardPage.qml
            qml/pages/ScreenshotsPage.qml
            qml/components/ScreenshotEditor.qml
            qml/pages/ProfilesPage.qml
            qml/pages/HotkeysPage.qml
            qml/pages/ActionsPage.qml
            qml/pages/SettingsPage.qml
    )
    target_compile_definitions(ScreenshotHotkeyManagerTests PRIVATE
        QT_NO_CAST_FROM_ASCII
        QT_NO_CAST_TO_ASCII
        TEST_QML_DIR="${CMAKE_CURRENT_SOURCE_DIR}/qml"
    )
    if(WIN32)
        target_link_libraries(ScreenshotHotkeyManagerTests PRIVATE shell32 user32 psapi)
    endif()
    add_test(NAME ScreenshotHotkeyManager.UnitTests COMMAND ScreenshotHotkeyManagerTests)
    set_tests_properties(ScreenshotHotkeyManager.UnitTests PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    )
endif()

