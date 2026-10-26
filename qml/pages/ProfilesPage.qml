import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    property string selectedProfileId: profiles.activeProfileId
    property var selectedProfileInfo: ({})
    property int registrationEpoch: 0

    function refreshSelectedInfo() {
        if (!profiles.profileInfo(selectedProfileId) || Object.keys(profiles.profileInfo(selectedProfileId)).length === 0)
            selectedProfileId = profiles.activeProfileId
        selectedProfileInfo = profiles.profileInfo(selectedProfileId)
    }

    Component.onCompleted: refreshSelectedInfo()
    Connections {
        target: profiles
        function onProfilesChanged() { root.refreshSelectedInfo() }
        function onActiveProfileChanged() { root.refreshSelectedInfo() }
    }
    Connections {
        target: hotkeys
        function onRegistrationStatusesChanged() { root.registrationEpoch += 1 }
    }

    RowLayout {
        anchors.fill: parent
        spacing: 16

        Rectangle {
            Layout.preferredWidth: 330
            Layout.fillHeight: true
            radius: settings.retroArcade ? settings.palette["panelRadius"] : 15
            color: settings.palette["#191a20"]
            border.color: settings.palette["#2d2e37"]
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 15
                spacing: 12
                RowLayout {
                    Layout.fillWidth: true
                    Label { text: "Profiles"; color: settings.palette["#f1eff2"]; font.pixelSize: 15; font.weight: Font.DemiBold; Layout.fillWidth: true }
                    Button {
                        text: "+ New"
                        onClicked: { newName.text = ""; createDialog.open() }
                        background: Rectangle { radius: settings.retroArcade ? settings.palette["panelRadius"] : 8; color: settings.palette["#39222b"]; border.color: settings.palette["#56303d"] }
                        contentItem: Text { text: parent.text; color: settings.palette["#f3c6d0"]; font.pixelSize: 10; font.weight: Font.DemiBold; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; leftPadding: 8; rightPadding: 8 }
                    }
                }

                ListView {
                    id: profileList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    model: profiles
                    spacing: 7
                    clip: true
                    delegate: Rectangle {
                        id: profileCard
                        required property string profileId
                        required property string name
                        required property string screenshotDirectory
                        required property bool isActive
                        required property string icon
                        activeFocusOnTab: true
                        Keys.onReturnPressed: { root.selectedProfileId = profileId; root.refreshSelectedInfo() }
                        Keys.onSpacePressed: { root.selectedProfileId = profileId; root.refreshSelectedInfo() }
                        Accessible.role: Accessible.ListItem
                        Accessible.name: name
                        width: profileList.width
                        height: 76
                        radius: settings.retroArcade ? settings.palette["panelRadius"] : 11
                        color: root.selectedProfileId === profileId ? settings.palette["#2a2229"] : selectMouse.containsMouse ? settings.palette["#22232a"] : settings.palette["#1e1f25"]
                        border.color: activeFocus ? settings.palette["#e96782"] : root.selectedProfileId === profileId ? settings.palette["#75404e"] : settings.palette["#2c2d34"]
                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: 11
                            spacing: 10
                            Rectangle {
                                width: 34; height: 34; radius: settings.retroArcade ? settings.palette["panelRadius"] : 11
                                color: isActive ? settings.palette["#39222b"] : settings.palette["#2b2c34"]
                                Label { anchors.centerIn: parent; text: icon || "◉"; color: isActive ? settings.palette["#e96782"] : settings.palette["#9b9ca7"]; font.pixelSize: 13 }
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 4
                                Label { text: name; color: settings.palette["#f0eef2"]; font.pixelSize: 12; font.weight: Font.DemiBold; elide: Text.ElideRight; Layout.fillWidth: true }
                                Label { text: isActive ? "ACTIVE PROFILE" : "MANUAL PROFILE"; color: isActive ? settings.palette["#de8197"] : settings.palette["#777984"]; font.pixelSize: 8; font.letterSpacing: 0.8 }
                            }
                            Label { text: "›"; color: settings.palette["#747580"]; font.pixelSize: 18 }
                        }
                        MouseArea {
                            id: selectMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: { root.selectedProfileId = profileId; root.refreshSelectedInfo() }
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    Button { text: "Duplicate"; Layout.fillWidth: true; enabled: root.selectedProfileId.length > 0; onClicked: { const id = profiles.duplicateProfile(root.selectedProfileId); if (id) { root.selectedProfileId = id; root.refreshSelectedInfo() } } }
                    Button { text: "Rename"; Layout.fillWidth: true; enabled: root.selectedProfileId.length > 0; onClicked: { renameName.text = root.selectedProfileInfo.name || ""; renameDialog.open() } }
                    Button { text: "Delete"; Layout.fillWidth: true; enabled: profiles.count > 1 && !root.selectedProfileInfo.isDefault; onClicked: deleteDialog.open() }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: settings.retroArcade ? settings.palette["panelRadius"] : 15
            color: settings.palette["#191a20"]
            border.color: settings.palette["#2d2e37"]
            visible: root.selectedProfileInfo.id !== undefined
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 23
                spacing: 18
                RowLayout {
                    Layout.fillWidth: true
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 4
                        Label { text: root.selectedProfileInfo.name || "Profile"; color: settings.palette["#f2f1f4"]; font.pixelSize: 21; font.weight: Font.DemiBold }
                        Label { text: (root.selectedProfileInfo.hotkeyCount || 0) + " shortcut assignments"; color: settings.palette["#8f909b"]; font.pixelSize: 11 }
                    }
                    Button {
                        text: root.selectedProfileInfo.isActive ? "Active" : "Activate profile"
                        enabled: !root.selectedProfileInfo.isActive
                        onClicked: profiles.setActiveProfile(root.selectedProfileId)
                        background: Rectangle { radius: settings.retroArcade ? settings.palette["panelRadius"] : 9; color: parent.enabled ? settings.palette["#e96782"] : settings.palette["#2d2e37"] }
                        contentItem: Text { text: parent.text; color: parent.enabled ? "white" : settings.palette["#a1a2ac"]; font.pixelSize: 11; font.weight: Font.DemiBold; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; leftPadding: 12; rightPadding: 12 }
                    }
                }

                Rectangle { Layout.fillWidth: true; height: 1; color: settings.palette["#2d2e37"] }
                RowLayout {
                    Layout.fillWidth: true
                    Label { text: "Icon"; color: settings.palette["#9697a3"] }
                    TextField { Layout.preferredWidth: 65; maximumLength: 8; text: root.selectedProfileInfo.icon || "◉"; onEditingFinished: { profiles.setDetails(root.selectedProfileId, root.selectedProfileInfo.description || "", text); text = Qt.binding(function() { return root.selectedProfileInfo.icon || "◉" }) } }
                    TextField { Layout.fillWidth: true; maximumLength: 2000; placeholderText: "Profile description"; text: root.selectedProfileInfo.description || ""; onEditingFinished: { profiles.setDetails(root.selectedProfileId, text, root.selectedProfileInfo.icon || "◉"); text = Qt.binding(function() { return root.selectedProfileInfo.description || "" }) } }
                }
                RowLayout {
                    Button { text: "Import profile"; onClicked: app.importProfile() }
                    Button { text: "Export profile"; onClicked: app.exportProfile(root.selectedProfileId) }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    Label { text: "SCREENSHOT DESTINATION"; color: settings.palette["#888994"]; font.pixelSize: 9; font.weight: Font.DemiBold; font.letterSpacing: 1 }
                    RowLayout {
                        Layout.fillWidth: true
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 42
                            radius: settings.retroArcade ? settings.palette["panelRadius"] : 9
                            color: settings.palette["#202127"]
                            border.color: settings.palette["#30313a"]
                            Label { anchors.fill: parent; anchors.leftMargin: 12; anchors.rightMargin: 10; verticalAlignment: Text.AlignVCenter; text: root.selectedProfileInfo.screenshotDirectory || ""; color: settings.palette["#c3c2ca"]; font.pixelSize: 11; elide: Text.ElideMiddle }
                        }
                        Button { text: "Choose folder"; onClicked: app.chooseProfileScreenshotDirectory(root.selectedProfileId) }
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 9
                    RowLayout {
                        Layout.fillWidth: true
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 3
                            Label { text: "PROFILE ACTIVATION SHORTCUT"; color: settings.palette["#888994"]; font.pixelSize: 9; font.weight: Font.DemiBold; font.letterSpacing: 1 }
                            Label { text: "Works globally, regardless of which profile is active."; color: settings.palette["#858692"]; font.pixelSize: 10 }
                        }
                        Label {
                            text: { root.registrationEpoch; hotkeys.activationStatus(root.selectedProfileId) }
                            color: text === "Registered globally" ? settings.palette["#74d3aa"] : text === "Not assigned" ? settings.palette["#777984"] : settings.palette["#f1be79"]
                            font.pixelSize: 10
                        }
                    }
                    HotkeyRecorder {
                        key: root.selectedProfileInfo.activationKey || 0
                        modifiers: root.selectedProfileInfo.activationModifiers || 0
                        placeholder: "Click, then press activation shortcut"
                        onRecorded: function(key, modifiers) { app.setProfileActivationShortcut(root.selectedProfileId, key, modifiers); root.refreshSelectedInfo() }
                        onCleared: { app.setProfileActivationShortcut(root.selectedProfileId, 0, 0); root.refreshSelectedInfo() }
                    }
                }

                Rectangle { Layout.fillWidth: true; height: 1; color: settings.palette["#2d2e37"] }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    radius: settings.retroArcade ? settings.palette["panelRadius"] : 12
                    color: settings.palette["#202127"]
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 17
                        spacing: 9
                        Label { text: "Manual switching"; color: settings.palette["#f0eef2"]; font.pixelSize: 13; font.weight: Font.DemiBold }
                        Label {
                            Layout.fillWidth: true
                            text: "Profiles change only when you select one here, use the system tray menu, or press its dedicated activation shortcut. The application does not monitor foreground apps or switch profiles automatically."
                            color: settings.palette["#9697a3"]
                            font.pixelSize: 11
                            wrapMode: Text.WordWrap
                        }
                        Item { Layout.fillHeight: true }
                    }
                }
            }
        }
    }

    Dialog {
        id: createDialog
        width: 380
        title: "Create profile"
        modal: true
        standardButtons: Dialog.Ok | Dialog.Cancel
        anchors.centerIn: Overlay.overlay
        onAccepted: {
            const id = profiles.createProfile(newName.text)
            if (id) { root.selectedProfileId = id; root.refreshSelectedInfo() }
        }
        contentItem: TextField { id: newName; placeholderText: "Profile name"; selectByMouse: true; width: 320; onAccepted: createDialog.accept() }
    }
    Dialog {
        id: renameDialog
        width: 380
        title: "Rename profile"
        modal: true
        standardButtons: Dialog.Ok | Dialog.Cancel
        anchors.centerIn: Overlay.overlay
        onAccepted: { profiles.renameProfile(root.selectedProfileId, renameName.text); root.refreshSelectedInfo() }
        contentItem: TextField { id: renameName; placeholderText: "Profile name"; selectByMouse: true; width: 320; onAccepted: renameDialog.accept() }
    }
    Dialog {
        id: deleteDialog
        width: 420
        title: "Delete profile?"
        modal: true
        standardButtons: Dialog.Yes | Dialog.No
        anchors.centerIn: Overlay.overlay
        onAccepted: { profiles.deleteProfile(root.selectedProfileId); root.selectedProfileId = profiles.activeProfileId; root.refreshSelectedInfo() }
        contentItem: Label { text: "Delete “" + (root.selectedProfileInfo.name || "") + "”? Its profile file and shortcut settings will be removed. Screenshot files will stay in their folder."; wrapMode: Text.WordWrap; color: settings.palette["#c8c7cf"]; width: 350; padding: 12 }
    }
}
