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

# ZXing-C++ is fetched at a fixed release and linked statically for the
# portable build. Enable only QR encode/decode support, which keeps unrelated
# barcode formats and runtime DLL dependencies out of SmallDTL.
include(FetchContent)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build static libraries for portable SmallDTL" FORCE)
set(ZXING_READERS ON CACHE BOOL "Enable QR decoding" FORCE)
set(ZXING_WRITERS OLD CACHE STRING "Enable the QR writer with explicit margin/ECC controls" FORCE)
set(ZXING_ENABLE_1D OFF CACHE BOOL "Disable unrelated barcode formats" FORCE)
set(ZXING_ENABLE_AZTEC OFF CACHE BOOL "Disable unrelated barcode formats" FORCE)
set(ZXING_ENABLE_DATAMATRIX OFF CACHE BOOL "Disable unrelated barcode formats" FORCE)
set(ZXING_ENABLE_MAXICODE OFF CACHE BOOL "Disable unrelated barcode formats" FORCE)
set(ZXING_ENABLE_PDF417 OFF CACHE BOOL "Disable unrelated barcode formats" FORCE)
set(ZXING_ENABLE_QRCODE ON CACHE BOOL "Enable QR encode/decode" FORCE)
set(ZXING_C_API OFF CACHE BOOL "Do not build the unused C wrapper" FORCE)
set(ZXING_EXAMPLES OFF CACHE BOOL "Do not build ZXing examples" FORCE)
set(ZXING_BLACKBOX_TESTS OFF CACHE BOOL "Do not build ZXing tests" FORCE)
set(ZXING_UNIT_TESTS OFF CACHE BOOL "Do not build ZXing tests" FORCE)
set(ZXING_DISABLE_PCH ON CACHE BOOL "Avoid MinGW precompiled-header relocation failures" FORCE)
FetchContent_Declare(zxing_cpp
    GIT_REPOSITORY https://github.com/zxing-cpp/zxing-cpp.git
    GIT_TAG v3.1.1
    GIT_SHALLOW TRUE
    GIT_SUBMODULES ""
)
FetchContent_MakeAvailable(zxing_cpp)
# SmallDTL enables Qt's automatic MOC globally; ZXing is a plain C++ target,
# and its similarly named ZXingQt.h is an optional adapter, not a Qt QObject.
set_target_properties(ZXing PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
set(SMALLDTL_THIRD_PARTY_LICENSE_DIR "${CMAKE_CURRENT_BINARY_DIR}/third-party-licenses")
file(MAKE_DIRECTORY "${SMALLDTL_THIRD_PARTY_LICENSE_DIR}")
configure_file("${zxing_cpp_SOURCE_DIR}/LICENSE"
               "${SMALLDTL_THIRD_PARTY_LICENSE_DIR}/ZXing-C++-Apache-2.0.txt" COPYONLY)

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
    src/clipboard/ClipboardService.h
    src/clipboard/ClipboardService.cpp
    src/clipboard/ClipboardHistoryManager.h
    src/clipboard/ClipboardHistoryManager.cpp
    src/fileshelf/FileShelfManager.h
    src/fileshelf/FileShelfManager.cpp
    src/settings/SettingsManager.h
    src/settings/SettingsManager.cpp
    src/profiles/ProfileManager.h
    src/profiles/ProfileManager.cpp
    src/screenshots/ScreenshotManager.h
    src/screenshots/ScreenshotManager.cpp
    src/storage/ApplicationDatabase.h
    src/storage/ApplicationDatabase.cpp
    src/storage/StorageMaintenance.cpp
    src/storage/ClipboardRepository.cpp
    src/storage/FileShelfRepository.cpp
    src/storage/ThumbnailCache.cpp
    src/storage/ThumbnailCache.h
    src/screenshots/ScreenshotEditorImage.h
    src/screenshots/ScreenshotEditorImage.cpp
    src/qrcode/QrCodeManager.h
    src/qrcode/QrCodeManager.cpp
    src/windows/WindowArrangementGeometry.h
    src/windows/WindowArrangementManager.h
    src/windows/WindowArrangementManager.cpp
    src/hotkeys/HotkeyManager.h
    src/hotkeys/HotkeyManager.cpp
)

set_target_properties(ScreenshotHotkeyManager PROPERTIES OUTPUT_NAME SmallDTL)
if(WIN32)
    enable_language(RC)
    configure_file(src/app/SmallDTL.manifest "${CMAKE_CURRENT_BINARY_DIR}/SmallDTL.manifest" COPYONLY)
    configure_file(src/app/SmallDTL.rc.in "${CMAKE_CURRENT_BINARY_DIR}/SmallDTL.rc" @ONLY)
    target_sources(ScreenshotHotkeyManager PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/SmallDTL.rc")
endif()

qt_add_qml_module(ScreenshotHotkeyManager
    URI ScreenshotHotkeyManager
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/components/CustomCursor.qml
        qml/components/Sidebar.qml
        qml/components/HotkeyRecorder.qml
        qml/components/HistoryClearDialog.qml
        qml/components/ScreenshotEditor.qml
        qml/components/InsetTextArea.qml
        qml/pages/DashboardPage.qml
        qml/pages/ScreenshotsPage.qml
        qml/pages/ProfilesPage.qml
        qml/pages/HotkeysPage.qml
        qml/pages/ActionsPage.qml
        qml/pages/SettingsPage.qml
        qml/pages/ClipboardHistoryPage.qml
        qml/pages/FileShelfPage.qml
        qml/pages/QrCodePage.qml
        qml/pages/WindowArrangementsPage.qml
    RESOURCES
        qml/assets/SmallDTL.png
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
    ZXing::ZXing
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
    if(WIN32)
        add_executable(WindowArrangementTestWindow WIN32 tests/WindowArrangementTestWindow.cpp)
        target_link_libraries(WindowArrangementTestWindow PRIVATE user32)
    endif()
    qt_add_executable(ScreenshotHotkeyManagerTests
        tests/UnitTests.cpp
        tests/MaintenanceTests.cpp
        tests/UnitTests.h
        tests/TestSupport.h
        tests/TestLifecycle.cpp
        tests/ProfileActionTests.cpp
        tests/WindowArrangementTests.cpp
        tests/FileShelfTests.cpp
        tests/UiTests.cpp
        tests/ScreenshotTests.cpp
        tests/ClipboardTests.cpp
        src/app/ApplicationController.h
        src/app/ApplicationController.cpp
        src/clipboard/ClipboardService.h
        src/clipboard/ClipboardService.cpp
        src/clipboard/ClipboardHistoryManager.h
        src/clipboard/ClipboardHistoryManager.cpp
        src/fileshelf/FileShelfManager.h
        src/fileshelf/FileShelfManager.cpp
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
        src/storage/ApplicationDatabase.h
        src/storage/ApplicationDatabase.cpp
        src/storage/StorageMaintenance.cpp
        src/storage/ClipboardRepository.cpp
        src/storage/FileShelfRepository.cpp
        src/storage/ThumbnailCache.cpp
        src/storage/ThumbnailCache.h
        src/screenshots/ScreenshotEditorImage.h
        src/screenshots/ScreenshotEditorImage.cpp
        src/qrcode/QrCodeManager.h
        src/qrcode/QrCodeManager.cpp
        src/windows/WindowArrangementGeometry.h
        src/windows/WindowArrangementManager.h
        src/windows/WindowArrangementManager.cpp
    )
    target_link_libraries(ScreenshotHotkeyManagerTests PRIVATE
        Qt6::Core Qt6::Gui Qt6::Quick Qt6::QuickControls2 Qt6::Widgets Qt6::Concurrent Qt6::Sql Qt6::Test ZXing::ZXing
    )
    qt_add_qml_module(ScreenshotHotkeyManagerTests
        URI ScreenshotHotkeyManagerTest
        VERSION 1.0
        OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/test-qml/ScreenshotHotkeyManagerTest"
        QML_FILES
            qml/Main.qml
            qml/components/CustomCursor.qml
            qml/components/Sidebar.qml
            qml/components/HotkeyRecorder.qml
        qml/components/HistoryClearDialog.qml
            qml/pages/DashboardPage.qml
            qml/pages/ScreenshotsPage.qml
            qml/components/ScreenshotEditor.qml
            qml/components/InsetTextArea.qml
            qml/pages/ProfilesPage.qml
            qml/pages/HotkeysPage.qml
            qml/pages/ActionsPage.qml
            qml/pages/SettingsPage.qml
            qml/pages/ClipboardHistoryPage.qml
            qml/pages/FileShelfPage.qml
            qml/pages/QrCodePage.qml
            qml/pages/WindowArrangementsPage.qml
        RESOURCES
            qml/assets/SmallDTL.png
    )
    target_compile_definitions(ScreenshotHotkeyManagerTests PRIVATE
        QT_NO_CAST_FROM_ASCII
        QT_NO_CAST_TO_ASCII
        TEST_QML_DIR="${CMAKE_CURRENT_SOURCE_DIR}/qml"
    )
    if(WIN32)
        add_dependencies(ScreenshotHotkeyManagerTests WindowArrangementTestWindow)
        target_compile_definitions(ScreenshotHotkeyManagerTests PRIVATE
            SMALLDTL_TEST_WINDOW_HELPER="$<TARGET_FILE:WindowArrangementTestWindow>"
        )
        target_link_libraries(ScreenshotHotkeyManagerTests PRIVATE shell32 user32 psapi)
    endif()
    add_test(NAME ScreenshotHotkeyManager.UnitTests COMMAND ScreenshotHotkeyManagerTests)
    set_tests_properties(ScreenshotHotkeyManager.UnitTests PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software"
        TIMEOUT 300
    )
endif()

