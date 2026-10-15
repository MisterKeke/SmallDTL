import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import QtQuick.Window

ApplicationWindow {
    id: window
    width: 1360
    height: 850
    minimumWidth: 1040
    minimumHeight: 680
    visible: false
    title: "SmallDTL"
    color: appTheme.window
    Material.theme: settings.darkTheme ? Material.Dark : Material.Light
    Material.accent: settings.palette["#e96782"]
    Material.primary: settings.palette["#39222b"]
    Material.background: settings.palette["#191a20"]
    Material.foreground: settings.palette["#f2f1f4"]

    QtObject {
        id: themeObject
        readonly property color window: settings.palette["#111216"]
        readonly property color panel: settings.palette["#191a20"]
        readonly property color panelRaised: settings.palette["#202127"]
        readonly property color panelHover: settings.palette["#292a32"]
        readonly property color border: settings.palette["#2d2e37"]
        readonly property color text: settings.palette["#f2f1f4"]
        readonly property color muted: settings.palette["#9697a3"]
        readonly property color accent: settings.palette["#e96782"]
        readonly property color accentSoft: settings.palette["#39222b"]
        readonly property color green: settings.palette["#74d3aa"]
        readonly property color red: settings.palette["#ff8d9c"]
    }
    property QtObject appTheme: themeObject

    onClosing: function(close) {
        if (app.closeToTray && app.trayAvailable) {
            close.accepted = false
            app.hideToTray()
        }
    }
    onVisibleChanged: if (!visible) app.setFileShelfPopupVisible(false)

    RowLayout {
        anchors.fill: parent
        spacing: 0

        Sidebar {
            Layout.fillHeight: true
            Layout.preferredWidth: 242
            selectedPage: app.currentPage
            onPageSelected: function(page) { app.setCurrentPage(page) }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
    color: appTheme.window

            ColumnLayout {
                anchors.fill: parent
                anchors.leftMargin: 34
                anchors.rightMargin: 34
                anchors.topMargin: 26
                anchors.bottomMargin: 26
                spacing: 20

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 16

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 3
                        Label {
                            text: app.currentPage
                            color: appTheme.text
                            font.pixelSize: 27
                            font.weight: Font.DemiBold
                        }
                        Label {
                            text: app.currentPage === "Screenshots" ? "Capture and organize clipboard images"
                                  : app.currentPage === "Clipboard History" ? "Find and restore copied text, images, and file references"
                                  : app.currentPage === "File Shelf" ? "Keep reusable file and folder references together"
                                  : app.currentPage === "QR Codes" ? "Generate QR codes and inspect decoded content locally"
                                  : app.currentPage === "Window Arrangements" ? "Arrange open windows and save reusable profile layouts"
                                  : app.currentPage === "Profiles" ? "Keep shortcuts and destinations organized"
                                  : app.currentPage === "Hotkeys" ? (hotkeys.globalScope ? "Shortcuts available across every profile" : "Global shortcuts for the active profile")
                                  : app.currentPage === "Actions" ? "Compose ordered actions and run them from a shortcut"
                                  : app.currentPage === "Settings" ? "Tune how the application behaves"
                                  : "Your local workspace at a glance"
                            color: appTheme.muted
                            font.pixelSize: 13
                        }
                    }

                    Rectangle {
                        Layout.preferredWidth: 245
                        Layout.preferredHeight: 48
                        radius: 12
                        color: appTheme.panel
                        border.color: appTheme.border
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 12
                            anchors.rightMargin: 10
                            spacing: 8
                            Rectangle {
                                Layout.preferredWidth: 28
                                Layout.preferredHeight: 28
                                radius: 9
                                color: appTheme.accentSoft
                                Label { anchors.centerIn: parent; text: "P"; color: appTheme.accent; font.bold: true }
                            }
                            ComboBox {
                                id: activeProfilePicker
                                Layout.fillWidth: true
                                model: profiles
                                textRole: "name"
                                valueRole: "profileId"
                                currentValue: profiles.activeProfileId
                                onActivated: { profiles.setActiveProfile(currentValue); currentIndex = indexOfValue(profiles.activeProfileId) }
                                background: Item {}
                                contentItem: Text {
                                    text: activeProfilePicker.displayText
                                    color: appTheme.text
                                    font.pixelSize: 13
                                    font.weight: Font.Medium
                                    verticalAlignment: Text.AlignVCenter
                                    elide: Text.ElideRight
                                }
                                indicator: Label { text: "⌄"; color: appTheme.muted; anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter }
                            }
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 42
                    radius: 10
                    color: app.statusMessage.length > 0 ? (app.statusIsError ? settings.palette["#3b2229"] : settings.palette["#1e332b"]) : "transparent"
                    visible: app.statusMessage.length > 0
                    border.color: app.statusIsError ? settings.palette["#6b3642"] : settings.palette["#315542"]
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 14
                        anchors.rightMargin: 8
                        Label {
                            Layout.fillWidth: true
                            text: app.statusMessage
                            color: app.statusIsError ? appTheme.red : appTheme.green
                            font.pixelSize: 12
                            elide: Text.ElideRight
                        }
                        ToolButton {
                            id: dismissStatusButton
                            text: "×"
                            onClicked: app.dismissMessage()
                            contentItem: Text { text: dismissStatusButton.text; color: window.appTheme.muted; font.pixelSize: 19; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                        }
                    }
                }

                StackLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    currentIndex: app.currentPage === "Dashboard" ? 0 : app.currentPage === "Screenshots" ? 1
                                  : app.currentPage === "Profiles" ? 2 : app.currentPage === "Hotkeys" ? 3
                                  : app.currentPage === "Actions" ? 4 : app.currentPage === "Settings" ? 5
                                  : app.currentPage === "Clipboard History" ? 6 : app.currentPage === "File Shelf" ? 7 : app.currentPage === "QR Codes" ? 8 : 9
                    DashboardPage {}
                    ScreenshotsPage {}
                    ProfilesPage {}
                    HotkeysPage {}
                    ActionsPage {}
                    SettingsPage {}
                    ClipboardHistoryPage {}
                    FileShelfPage {}
                    QrCodePage {}
                    WindowArrangementsPage {}
                }
            }
        }
    }

    Window {
        id: clipboardHistoryPopup
        objectName: "clipboardHistoryPopup"
        visible: app.clipboardHistoryPopupVisible
        width: 510
        height: 650
        minimumWidth: 440
        minimumHeight: 420
        title: "Clipboard History"
        color: settings.palette["#111216"]
        transientParent: null
        Material.theme: settings.darkTheme ? Material.Dark : Material.Light
        Material.accent: settings.palette["#e96782"]
        Material.primary: settings.palette["#39222b"]
        Material.background: settings.palette["#191a20"]
        Material.foreground: settings.palette["#f2f1f4"]
        flags: Qt.Tool | Qt.WindowStaysOnTopHint
        onVisibleChanged: if (visible) { x = Math.max(0, (Screen.width - width) / 2); y = Math.max(0, (Screen.height - height) / 3); popupSearch.forceActiveFocus() }
        onClosing: app.setClipboardHistoryPopupVisible(false)

        Shortcut { sequence: "Escape"; context: Qt.WindowShortcut; onActivated: app.setClipboardHistoryPopupVisible(false) }

        ColumnLayout {
            x: 14
            y: 14
            width: clipboardHistoryPopup.width - 28
            height: clipboardHistoryPopup.height - 28
            spacing: 10
            RowLayout {
                Layout.fillWidth: true
                Label { text: "Clipboard History"; font.pixelSize: 18; font.weight: Font.DemiBold; Layout.fillWidth: true }
                ToolButton { text: clipboardHistory.paused ? "▶" : "Ⅱ"; Accessible.name: clipboardHistory.paused ? "Resume clipboard history" : "Pause clipboard history"; onClicked: clipboardHistory.setPaused(!clipboardHistory.paused) }
                ToolButton { text: "×"; Accessible.name: "Clear clipboard history"; enabled: clipboardHistory.count > 0; onClicked: clipboardHistory.clearHistory() }
            }
            TextField {
                id: popupSearch
                objectName: "clipboardHistoryPopupSearch"
                Layout.fillWidth: true
                placeholderText: "Search clipboard history"
                text: clipboardHistory.search
                onTextEdited: clipboardHistory.setSearch(text)
            }
            Label { Layout.fillWidth: true; text: clipboardHistory.paused ? "Capture paused" : "Select an item to copy it to the clipboard"; color: settings.palette["#9697a3"]; font.pixelSize: 10 }
            ListView {
                objectName: "clipboardHistoryPopupList"
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: 6
                model: clipboardHistory.model
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                delegate: Rectangle {
                    id: popupEntry
                    required property string entryId
                    required property string contentType
                    required property string capturedAt
                    required property string preview
                    required property url imageUrl
                    required property int fileCount
                    required property bool pinned
                    required property bool missing
                    width: ListView.view.width
                    height: contentType === "image" ? 104 : 78
                    radius: 9
                    color: popupMouse.containsMouse ? settings.palette["#22232a"] : settings.palette["#191a20"]
                    border.color: settings.palette["#2d2e37"]
                    MouseArea { id: popupMouse; anchors.fill: parent; hoverEnabled: true; onClicked: clipboardHistory.restoreEntry(popupEntry.entryId) }
                    RowLayout {
                        anchors.fill: parent; anchors.margins: 8; spacing: 9
                        Image { visible: popupEntry.contentType === "image"; Layout.preferredWidth: 88; Layout.preferredHeight: 86; source: popupEntry.imageUrl; fillMode: Image.PreserveAspectFit; asynchronous: true; cache: false }
                        ColumnLayout {
                            Layout.fillWidth: true; Layout.fillHeight: true; spacing: 3
                            Label { text: popupEntry.contentType === "files" ? "Files · " + popupEntry.fileCount : popupEntry.contentType === "image" ? "Image" : "Text"; color: settings.palette["#e7a0b1"]; font.pixelSize: 10; font.weight: Font.DemiBold }
                            Label { Layout.fillWidth: true; Layout.fillHeight: true; text: popupEntry.missing ? "Missing content or file reference" : popupEntry.preview; color: popupEntry.missing ? settings.palette["#ff8d9c"] : settings.palette["#dedde2"]; font.pixelSize: 10; wrapMode: Text.WrapAnywhere; maximumLineCount: 2; elide: Text.ElideRight }
                            Label { text: popupEntry.capturedAt + (popupEntry.pinned ? "  ·  Pinned" : ""); color: settings.palette["#898a95"]; font.pixelSize: 8 }
                        }
                        ColumnLayout {
                            spacing: 0
                            ToolButton { text: popupEntry.pinned ? "◆" : "◇"; Accessible.name: popupEntry.pinned ? "Unpin entry" : "Pin entry"; onClicked: clipboardHistory.setPinned(popupEntry.entryId, !popupEntry.pinned) }
                            ToolButton { text: "×"; Accessible.name: "Delete entry"; onClicked: clipboardHistory.deleteEntry(popupEntry.entryId) }
                        }
                    }
                }
                Label { anchors.centerIn: parent; visible: clipboardHistory.count === 0; text: clipboardHistory.paused ? "History capture is paused" : "No clipboard items"; color: settings.palette["#9697a3"]; font.pixelSize: 11 }
            }
        }
    }

    Window {
        id: fileShelfPopup
        objectName: "fileShelfPopup"
        visible: app.fileShelfPopupVisible
        width: 700
        height: 620
        minimumWidth: 540
        minimumHeight: 420
        title: "File Shelf"
        color: settings.palette["#111216"]
        transientParent: null
        Material.theme: settings.darkTheme ? Material.Dark : Material.Light
        Material.accent: settings.palette["#e96782"]
        Material.primary: settings.palette["#39222b"]
        Material.background: settings.palette["#191a20"]
        Material.foreground: settings.palette["#f2f1f4"]
        flags: Qt.Tool | Qt.WindowStaysOnTopHint
        onVisibleChanged: if (visible) { x = Math.max(0, (Screen.width - width) / 2); y = Math.max(0, (Screen.height - height) / 3); fileShelf.refresh() }
        onClosing: app.setFileShelfPopupVisible(false)

        Shortcut { sequence: "Escape"; context: Qt.WindowShortcut; onActivated: app.setFileShelfPopupVisible(false) }
        FileShelfPage { anchors.fill: parent; anchors.margins: 12; compact: true }
    }
}
