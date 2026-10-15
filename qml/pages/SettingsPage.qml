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
                    Preference { title: "Automatically save clipboard images"; description: "Save images copied from other apps without opening SmallDTL. Pause temporarily from the tray menu."; value: settings.automaticClipboardSavingEnabled; apply: function(value) { settings.setAutomaticClipboardSavingEnabled(value) } }
                    Label { text: "Clipboard history"; font.pixelSize: 14; font.weight: Font.DemiBold; Layout.topMargin: 5 }
                    Label { text: "History keeps text, image copies, and file references locally. Its shortcut opens a compact popup even when the main window is hidden."; color: settings.palette["#9697a3"]; font.pixelSize: 11; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                    RowLayout {
                        Layout.fillWidth: true
                        Label { text: "Popup shortcut"; Layout.preferredWidth: 120 }
                        HotkeyRecorder {
                            objectName: "clipboardHistoryShortcutRecorder"
                            key: settings.clipboardHistoryShortcutKey
                            modifiers: settings.clipboardHistoryShortcutModifiers
                            onRecorded: function(keyValue, modifierValue) {
                                app.setClipboardHistoryShortcut(keyValue, modifierValue)
                            }
                            onCleared: app.setClipboardHistoryShortcut(0, 0)
                        }
                        Label { text: hotkeys.clipboardHistoryStatus; color: hotkeys.clipboardHistoryStatus === "Registered globally" ? settings.palette["#74d3aa"] : settings.palette["#e7b879"]; font.pixelSize: 10 }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Label { text: "File shelf shortcut"; Layout.preferredWidth: 120 }
                        HotkeyRecorder {
                            objectName: "fileShelfShortcutRecorder"
                            key: settings.fileShelfShortcutKey
                            modifiers: settings.fileShelfShortcutModifiers
                            onRecorded: function(keyValue, modifierValue) { app.setFileShelfShortcut(keyValue, modifierValue) }
                            onCleared: app.setFileShelfShortcut(0, 0)
                        }
                        Label { text: hotkeys.fileShelfStatus; color: hotkeys.fileShelfStatus === "Registered globally" ? settings.palette["#74d3aa"] : settings.palette["#e7b879"]; font.pixelSize: 10 }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Label { text: "Keep unpinned entries"; Layout.preferredWidth: 170 }
                        SpinBox { objectName: "historyRetentionDays"; from: 1; to: 3650; value: settings.clipboardHistoryRetentionDays; onValueModified: settings.setClipboardHistoryRetentionDays(value) }
                        Label { text: "days" }
                        SpinBox { objectName: "historyRetentionCount"; from: 1; to: 100000; value: settings.clipboardHistoryRetentionCount; onValueModified: settings.setClipboardHistoryRetentionCount(value) }
                        Label { text: "items" }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Label { text: "History image storage"; Layout.preferredWidth: 170 }
                        SpinBox { objectName: "historyStorageLimit"; from: 16; to: 65536; value: settings.clipboardHistoryStorageLimitMB; onValueModified: settings.setClipboardHistoryStorageLimitMB(value) }
                        Label { text: "MiB" }
                    }
                    Label { text: "Pinned entries are kept during ordinary cleanup. File references point to their original locations; SmallDTL never copies or deletes those files."; color: settings.palette["#9697a3"]; font.pixelSize: 10; wrapMode: Text.WordWrap; Layout.fillWidth: true }
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
                        Layout.fillWidth: true
                        Label { text: "Tag automatic saves"; Layout.fillWidth: true }
                        ComboBox {
                            objectName: "automaticClipboardTagPicker"
                            model: [{ tagId: "", name: "No tag" }].concat(screenshots.tags)
                            textRole: "name"
                            valueRole: "tagId"
                            currentIndex: Math.max(0, indexOfValue(settings.automaticClipboardSaveTagId))
                            enabled: settings.automaticClipboardSavingEnabled
                            onActivated: settings.setAutomaticClipboardSaveTagId(currentValue)
                        }
                    }
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
