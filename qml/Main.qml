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
                                  : app.currentPage === "Actions" ? 4 : 5
                    DashboardPage {}
                    ScreenshotsPage {}
                    ProfilesPage {}
                    HotkeysPage {}
                    ActionsPage {}
                    SettingsPage {}
                }
            }
        }
    }
}
