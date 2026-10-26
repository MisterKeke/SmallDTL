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
        objectName: "settingsScrollView"
        anchors.fill: parent
        contentWidth: availableWidth
        clip: true
        ColumnLayout {
            width: scroll.availableWidth
            spacing: 14
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: behavior.implicitHeight + 36
                radius: settings.retroArcade ? settings.palette["panelRadius"] : 14
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
                    Preference { title: "Enable clipboard history"; description: "Remember this preference across restarts. Sensitive clipboard markers are always respected."; value: settings.clipboardHistoryEnabled; apply: function(value) { settings.setClipboardHistoryEnabled(value) } }
                    Preference { title: "Clear history on exit"; description: "Includes pinned entries. History is stored locally during this run, cleared on normal exit, and cleared at next startup after an interruption."; value: settings.clipboardHistorySessionOnly; apply: function(value) { settings.setClipboardHistorySessionOnly(value) } }
                    Label { text: "Exclude applications from history (one executable name or full path per line)"; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                    TextArea {
                        objectName: "historyExcludedApplications"
                        Layout.fillWidth: true
                        Layout.preferredHeight: 72
                        text: settings.clipboardHistoryExcludedApplications.join("\n")
                        placeholderText: "Example: passwordmanager.exe"
                        wrapMode: TextEdit.WrapAnywhere
                        onActiveFocusChanged: if (!activeFocus) {
                            settings.setClipboardHistoryExcludedApplications(text.split(/\r?\n/))
                            text = Qt.binding(function() { return settings.clipboardHistoryExcludedApplications.join("\n") })
                        }
                    }
                    Label { text: "Exclusions use the clipboard owner reported by Windows. Apps without an identifiable owner cannot be matched. These exclusions apply to history; automatic image saving has its own switch."; wrapMode: Text.WordWrap; Layout.fillWidth: true; font.pixelSize: 10 }
                    Label { text: "Clipboard history"; font.pixelSize: 14; font.weight: Font.DemiBold; Layout.topMargin: 5 }
                    Label { text: "History keeps text, image copies, and file references locally. Its shortcut opens a compact popup even when the main window is hidden."; color: settings.palette["#9697a3"]; font.pixelSize: 11; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                    RowLayout {
                        Layout.fillWidth: true
                        Label { text: "Command palette"; Layout.preferredWidth: 120 }
                        HotkeyRecorder {
                            objectName: "commandPaletteShortcutRecorder"
                            key: settings.commandPaletteShortcutKey
                            modifiers: settings.commandPaletteShortcutModifiers
                            onRecorded: function(keyValue, modifierValue) { app.setCommandPaletteShortcut(keyValue, modifierValue) }
                            onCleared: app.setCommandPaletteShortcut(0, 0)
                        }
                        Label { text: hotkeys.commandPaletteStatus; color: hotkeys.commandPaletteStatus === "Registered globally" ? settings.palette["#74d3aa"] : settings.palette["#e7b879"]; font.pixelSize: 10 }
                    }
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
                        Label { text: "Quick-add shortcut"; Layout.preferredWidth: 120 }
                        HotkeyRecorder {
                            objectName: "fileShelfQuickAddShortcutRecorder"
                            key: settings.fileShelfQuickAddShortcutKey
                            modifiers: settings.fileShelfQuickAddShortcutModifiers
                            onRecorded: function(keyValue, modifierValue) { app.setFileShelfQuickAddShortcut(keyValue, modifierValue) }
                            onCleared: app.setFileShelfQuickAddShortcut(0, 0)
                        }
                        Label { text: hotkeys.fileShelfQuickAddStatus; color: hotkeys.fileShelfQuickAddStatus === "Registered globally" ? settings.palette["#74d3aa"] : settings.palette["#e7b879"]; font.pixelSize: 10 }
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
                implicitHeight: storageOptions.implicitHeight + 36
                objectName: "storageSettingsPanel"
                radius: settings.retroArcade ? settings.palette["panelRadius"] : 14; color: settings.palette["#191a20"]; border.color: settings.palette["#2d2e37"]
                ColumnLayout {
                    id: storageOptions
                    anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 18
                    spacing: 10
                    Label { text: "Local storage"; font.pixelSize: 17; font.weight: Font.DemiBold }
                    Label {
                        Layout.fillWidth: true; wrapMode: Text.WordWrap
                        text: "History images: " + ((clipboardHistory.storageUsage.imageBytes || 0) / 1048576).toFixed(1) + " MiB · Shared database: " + ((clipboardHistory.storageUsage.databaseBytes || 0) / 1048576).toFixed(1) + " MiB · Thumbnails: " + ((clipboardHistory.storageUsage.thumbnailBytes || 0) / 1048576).toFixed(1) + " MiB · Cleanup failures: " + (clipboardHistory.storageUsage.failedFiles || 0)
                    }
                    RowLayout {
                        Label { text: "Thumbnail cache limit" }
                        SpinBox { from: 16; to: 4096; value: settings.thumbnailCacheLimitMB; onValueModified: settings.setThumbnailCacheLimitMB(value) }
                        Label { text: "MiB (also expires after 30 days)" }
                    }
                    Flow {
                        Layout.fillWidth: true; spacing: 8
                        Button { text: "Refresh and retry cleanup"; enabled: !clipboardHistory.maintaining; onClicked: clipboardHistory.maintainStorage() }
                        Button { text: "Compact database"; enabled: !clipboardHistory.maintaining; onClicked: clipboardHistory.maintainStorage(true) }
                        Button { text: "Clear thumbnails"; enabled: !clipboardHistory.maintaining; onClicked: clipboardHistory.maintainStorage(false, true) }
                    }
                    Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; text: "Maintenance removes orphaned history images and retries failed deletions. Compaction reclaims database space. These are ordinary deletion operations, not secure erasure."; font.pixelSize: 10 }
                    BusyIndicator { running: clipboardHistory.maintaining; visible: running; implicitWidth: 24; implicitHeight: 24 }
                }
            }
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: appearanceOptions.implicitHeight + 36
                radius: settings.retroArcade ? settings.palette["panelRadius"] : 14
                color: settings.palette["#191a20"]
                border.color: settings.palette["#2d2e37"]
                ColumnLayout {
                    id: appearanceOptions
                    anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 18
                    spacing: 12
                    Label { text: "Appearance"; font.pixelSize: 17; font.weight: Font.DemiBold }
                    RowLayout {
                        Layout.fillWidth: true
                        Label { text: "Visual style"; Layout.preferredWidth: 95 }
                        ComboBox {
                            objectName: "visualStylePicker"
                            model: ["Standard", "Retro Arcade"]
                            currentIndex: settings.visualStyle === "retro-arcade" ? 1 : 0
                            onActivated: settings.setVisualStyle(currentIndex === 1 ? "retro-arcade" : "standard")
                        }
                        Label {
                            Layout.fillWidth: true
                            text: settings.retroArcade ? "Retro Arcade uses its own dark palette." : "Choose the standard appearance or the Retro Arcade theme."
                            color: settings.palette["secondaryText"]
                            font.pixelSize: 11
                            wrapMode: Text.WordWrap
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Label { text: "Theme"; Layout.preferredWidth: 95 }
                        ComboBox { objectName: "appearancePicker"; model: ["Dark", "Light", "System"]; currentIndex: settings.appearance === "light" ? 1 : settings.appearance === "system" ? 2 : 0; enabled: !settings.retroArcade; onActivated: settings.setAppearance(["dark", "light", "system"][currentIndex]) }
                        Label { text: "Accent" }
                        TextField { objectName: "accentEditor"; text: settings.accentColor; placeholderText: "#e96782"; maximumLength: 30; Layout.preferredWidth: 140; enabled: !settings.retroArcade; onEditingFinished: { settings.setAccentColor(text); text = Qt.binding(function() { return settings.accentColor }) } }
                        Rectangle { width: 28; height: 28; radius: settings.retroArcade ? settings.palette["panelRadius"] : 8; color: settings.accentColor; opacity: settings.retroArcade ? 0.5 : 1.0 }
                    }
                }
            }
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: screenshotOptions.implicitHeight + 36
                radius: settings.retroArcade ? settings.palette["panelRadius"] : 14
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
                radius: settings.retroArcade ? settings.palette["panelRadius"] : 14
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
