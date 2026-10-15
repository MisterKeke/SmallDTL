import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    component Preference: RowLayout {
        id: preference
        property string title
        property string description
        property bool value
        property var apply
        Layout.fillWidth: true
        ColumnLayout {
            Layout.fillWidth: true
            Label { text: preference.title; font.pixelSize: 13; font.weight: Font.DemiBold }
            Label { text: preference.description; color: settings.palette["#9697a3"]; font.pixelSize: 11; wrapMode: Text.WordWrap; Layout.fillWidth: true }
        }
        Switch {
            objectName: preference.title
            checked: preference.value
            Accessible.name: preference.title
            onToggled: { preference.apply(checked); checked = Qt.binding(function() { return preference.value }) }
        }
    }
    ScrollView {
        id: scroll
        anchors.fill: parent
        contentWidth: availableWidth
        clip: true
        ColumnLayout {
            width: scroll.availableWidth
            spacing: 14
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: behavior.implicitHeight + 36
                radius: 14
                color: settings.palette["#191a20"]
                border.color: settings.palette["#2d2e37"]
                ColumnLayout {
                    id: behavior
                    anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 18
                    spacing: 10
                    Label { text: "Behavior"; font.pixelSize: 17; font.weight: Font.DemiBold }
                    Preference { title: "Start with Windows"; description: "Launch this application when you sign in."; value: settings.startWithWindows; apply: function(value) { settings.setStartWithWindows(value) } }
                    Preference { title: "Start minimized"; description: "Start hidden when the system tray is available."; value: settings.startMinimized; apply: function(value) { settings.setStartMinimized(value) } }
                    Preference { title: "Close to tray"; description: "Closing the window keeps global shortcuts available."; value: settings.closeToTray; apply: function(value) { settings.setCloseToTray(value) } }
                    Preference { title: "Minimize to tray"; description: "Hide the taskbar window when you minimize it."; value: settings.minimizeToTray; apply: function(value) { settings.setMinimizeToTray(value) } }
                    Preference { title: "Success notifications"; description: "Show nonessential tray notifications. Errors are always reported."; value: settings.notificationsEnabled; apply: function(value) { settings.setNotificationsEnabled(value) } }
                }
            }
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: appearanceOptions.implicitHeight + 36
                radius: 14
                color: settings.palette["#191a20"]
                border.color: settings.palette["#2d2e37"]
                ColumnLayout {
                    id: appearanceOptions
                    anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 18
                    spacing: 12
                    Label { text: "Appearance"; font.pixelSize: 17; font.weight: Font.DemiBold }
                    RowLayout {
                        Layout.fillWidth: true
                        Label { text: "Theme"; Layout.preferredWidth: 95 }
                        ComboBox { objectName: "appearancePicker"; model: ["Dark", "Light", "System"]; currentIndex: settings.appearance === "light" ? 1 : settings.appearance === "system" ? 2 : 0; onActivated: settings.setAppearance(["dark", "light", "system"][currentIndex]) }
                        Label { text: "Accent" }
                        TextField { objectName: "accentEditor"; text: settings.accentColor; placeholderText: "#e96782"; maximumLength: 30; Layout.preferredWidth: 140; onEditingFinished: { settings.setAccentColor(text); text = Qt.binding(function() { return settings.accentColor }) } }
                        Rectangle { width: 28; height: 28; radius: 8; color: settings.accentColor }
                    }
                }
            }
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: screenshotOptions.implicitHeight + 36
                radius: 14
                color: settings.palette["#191a20"]
                border.color: settings.palette["#2d2e37"]
                ColumnLayout {
                    id: screenshotOptions
                    anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 18
                    spacing: 10
                    Label { text: "Screenshot defaults"; font.pixelSize: 17; font.weight: Font.DemiBold }
                    RowLayout {
                        Layout.fillWidth: true
                        Label { text: settings.generalScreenshotDirectory; elide: Text.ElideMiddle; Layout.fillWidth: true }
                        Button { text: "Choose folder"; onClicked: app.chooseGeneralScreenshotDirectory() }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Label { text: "Filename" }
                        TextField { objectName: "filenamePatternEditor"; Layout.fillWidth: true; text: settings.filenamePattern; onEditingFinished: { settings.setFilenamePattern(text); text = Qt.binding(function() { return settings.filenamePattern }) } }
                    }
                    Label { text: "Use {timestamp}, {date}, or {time}. PNG extension and unique suffixes are added automatically."; font.pixelSize: 11; wrapMode: Text.WordWrap; Layout.fillWidth: true; color: settings.palette["#9697a3"] }
                    Label { text: "Images: up to 64 megapixels and 256 MiB; up to two queued saves. Thumbnails load on demand and are cached locally."; font.pixelSize: 11; wrapMode: Text.WordWrap; Layout.fillWidth: true; color: settings.palette["#9697a3"] }
                    RowLayout {
                        Label { text: "Known folders"; Layout.fillWidth: true }
                        Button { text: "Add folder"; onClicked: app.chooseScreenshotDestination() }
                    }
                    Repeater {
                        model: settings.knownScreenshotDirectories
                        delegate: RowLayout {
                            required property string modelData
                            Layout.fillWidth: true
                            Label { text: modelData; elide: Text.ElideMiddle; Layout.fillWidth: true; font.pixelSize: 11 }
                            Button { text: "Use for session"; onClicked: screenshots.selectDestination(modelData) }
                            ToolButton { text: "×"; enabled: modelData !== settings.generalScreenshotDirectory; Accessible.name: "Remove folder from list"; onClicked: settings.removeKnownScreenshotDirectory(modelData) }
                        }
                    }
                }
            }
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: profileOptions.implicitHeight + 36
                radius: 14
                color: settings.palette["#191a20"]
                border.color: settings.palette["#2d2e37"]
                ColumnLayout {
                    id: profileOptions
                    anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 18
                    spacing: 10
                    Label { text: "Profiles and global shortcuts"; font.pixelSize: 17; font.weight: Font.DemiBold }
                    RowLayout {
                        Button { text: "Import profile"; onClicked: app.importProfile() }
                        Button { text: "Export active profile"; onClicked: app.exportProfile(profiles.activeProfileId) }
                        Button { text: "Global shortcuts"; onClicked: { app.setHotkeyScope(true); app.setCurrentPage("Hotkeys") } }
                    }
                    Label { text: "Exports include sequences. Imported shortcuts start disabled and activation shortcuts are cleared for review."; font.pixelSize: 11; color: settings.palette["#9697a3"]; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                }
            }
        }
    }
}
