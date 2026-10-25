import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"

Item {
    id: root
    property int newKey: 0
    property int newModifiers: 0
    property int profileOptionsEpoch: 0
    property int sequenceOptionsEpoch: 0
    property int layoutOptionsEpoch: 0
    property string newSequenceId: ""
    property string newLayoutId: ""

    function refreshSequences() {
        sequenceOptionsEpoch += 1
        const options = actions.sequenceOptions()
        let found = false
        for (let i = 0; i < options.length; ++i) {
            if (options[i].id === newSequenceId) {
                found = true
                break
            }
        }
        if (!found)
            newSequenceId = options.length > 0 ? options[0].id : ""
    }

    Connections {
        target: profiles
        function onProfilesChanged() { root.profileOptionsEpoch += 1 }
    }
    Connections {
        target: actions
        function onSequencesChanged() { root.refreshSequences() }
    }
    Connections { target: windowArrangements; function onLayoutsChanged() { root.layoutOptionsEpoch += 1 } }

    Component.onCompleted: refreshSequences()

    ColumnLayout {
        anchors.fill: parent
        spacing: 15

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 78
            radius: 14
            color: settings.palette["#191a20"]
            border.color: settings.palette["#2d2e37"]
            RowLayout {
                anchors.fill: parent
                anchors.margins: 16
                spacing: 12
                Rectangle { width: 39; height: 39; radius: 12; color: settings.palette["#29232d"]; Label { anchors.centerIn: parent; text: "⌘"; color: settings.palette["#e7a0b1"]; font.pixelSize: 19 } }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 3
                    Label { text: hotkeys.globalScope ? "Application-wide shortcuts" : "Active profile: " + profiles.activeProfileName; color: settings.palette["#efedf1"]; font.pixelSize: 13; font.weight: Font.DemiBold }
                    Label { text: "Choose profile or application-wide shortcuts."; color: settings.palette["#898a95"]; font.pixelSize: 10 }
                }
                Label { text: hotkeys.count + " assignments"; color: settings.palette["#aaaab4"]; font.pixelSize: 10 }
                ComboBox {
                    objectName: "hotkeyScopePicker"
                    implicitContentWidthPolicy: ComboBox.WidestText
                    // Material's text field has its own padding inside the control.
                    Layout.minimumWidth: implicitWidth + (contentItem.leftPadding || 0) + (contentItem.rightPadding || 0)
                    model: ["Active profile", "Application-wide"]
                    currentIndex: hotkeys.globalScope ? 1 : 0
                    onActivated: app.setHotkeyScope(currentIndex === 1)
                }
            }
        }

        Label { text: hotkeys.globalScope ? "Application-wide shortcuts" : "Profile shortcuts"; color: settings.palette["#f1eff2"]; font.pixelSize: 15; font.weight: Font.DemiBold }

        ScrollView {
            id: shortcutScroll
            contentWidth: availableWidth
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            ScrollBar.vertical.policy: ScrollBar.AsNeeded
            ColumnLayout {
                width: shortcutScroll.availableWidth
                spacing: 8

                Repeater {
                    model: hotkeys
                    delegate: Rectangle {
                        id: shortcutCard
                        objectName: "assignment-" + assignmentId
                        required property string assignmentId
                        required property int key
                        required property int modifiers
                        required property string shortcut
                        required property string actionType
                        required property string targetProfileId
                        required property string sequenceId
                        required property string layoutId
                        required property bool assignmentEnabled
                        required property bool registered
                        required property string status
                        property int rowKey: key
                        property int rowModifiers: modifiers
                        property bool editingShortcut: false

                        Layout.fillWidth: true
                        implicitHeight: editingShortcut || actionType === "switchProfile" || actionType === "sequence" || actionType === "applyWindowLayout" ? 144 : 100
                        radius: 12
                        color: settings.palette["#191a20"]
                        border.color: registered ? settings.palette["#315542"] : settings.palette["#2d2e37"]

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 12
                            spacing: 8
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 9
                                Label { text: shortcut; color: settings.palette["#f3f1f4"]; font.pixelSize: 13; font.weight: Font.DemiBold; Layout.preferredWidth: 130; elide: Text.ElideRight }
                                Button { text: editingShortcut ? "Cancel" : "Change key"; onClicked: shortcutCard.editingShortcut = !shortcutCard.editingShortcut }
                                ComboBox {
                                    id: actionTypePicker
                                    objectName: "hotkeyActionTypePicker"
                                    Layout.preferredWidth: 170
                                    model: ["Save clipboard screenshot", "Switch profile", "Run action sequence", "Apply window layout"]
                                    currentIndex: actionType === "sequence" ? 2 : actionType === "switchProfile" ? 1 : actionType === "applyWindowLayout" ? 3 : 0
                                    onActivated: {
                                        const nextType = ["saveScreenshot", "switchProfile", "sequence", "applyWindowLayout"][currentIndex]
                                        const profileList = profiles.profileOptions()
                                        const sequenceList = actions.sequenceOptions()
                                        const layoutList = windowArrangements.layoutOptions()
                                        const nextProfile = nextType === "switchProfile" && targetProfileId.length === 0 && profileList.length > 0
                                                            ? profileList[0].id : targetProfileId
                                        const nextSequence = nextType === "sequence" && sequenceId.length === 0 && sequenceList.length > 0
                                                             ? sequenceList[0].id : sequenceId
                                        const nextLayout = nextType === "applyWindowLayout" && layoutId.length === 0 && layoutList.length > 0 ? layoutList[0].id : layoutId
                                        if (!app.updateHotkey(assignmentId, key, modifiers, nextType, nextProfile, nextSequence, assignmentEnabled, nextLayout))
                                            currentIndex = actionType === "sequence" ? 2 : actionType === "switchProfile" ? 1 : actionType === "applyWindowLayout" ? 3 : 0
                                    }
                                }
                                Item { Layout.fillWidth: true }
                                Label { text: status; color: registered ? settings.palette["#74d3aa"] : assignmentEnabled ? settings.palette["#e7b879"] : settings.palette["#7b7c87"]; font.pixelSize: 9; Layout.preferredWidth: 100; horizontalAlignment: Text.AlignRight; elide: Text.ElideRight }
                                Switch { objectName: "hotkeyEnabledSwitch"; checked: assignmentEnabled; onToggled: if (!app.setHotkeyEnabled(assignmentId, checked)) checked = Qt.binding(function() { return assignmentEnabled }) }
                                ToolButton { text: "×"; onClicked: app.removeHotkey(assignmentId); ToolTip.visible: hovered; ToolTip.text: "Remove shortcut" }
                            }
                            RowLayout {
                        Layout.fillWidth: true
                                visible: editingShortcut || actionType === "switchProfile" || actionType === "sequence" || actionType === "applyWindowLayout"
                                HotkeyRecorder {
                                    visible: editingShortcut
                                    key: rowKey
                                    modifiers: rowModifiers
                                    onRecorded: function(newKeyValue, newModifiersValue) {
                                        shortcutCard.editingShortcut = false
                                        app.updateHotkey(assignmentId, newKeyValue, newModifiersValue, actionType, targetProfileId, sequenceId, assignmentEnabled, layoutId)
                                    }
                                    onCleared: app.updateHotkey(assignmentId, 0, 0, actionType, targetProfileId, sequenceId, assignmentEnabled, layoutId)
                                }
                                Label { visible: actionType === "switchProfile"; text: "Switch target"; color: settings.palette["#999aa4"]; font.pixelSize: 10 }
                                ValueComboBox {
                                    visible: actionType === "switchProfile"
                                    Layout.preferredWidth: 220
                                    model: { root.profileOptionsEpoch; return profiles.profileOptions() }
                                    textRole: "name"
                                    valueRole: "id"
                                    selectedValue: targetProfileId
                                    onActivated: app.updateHotkey(assignmentId, key, modifiers, actionType, currentValue, sequenceId, assignmentEnabled, layoutId)
                                }
                                Label { visible: actionType === "sequence"; text: "Sequence"; color: settings.palette["#999aa4"]; font.pixelSize: 10 }
                                ValueComboBox {
                                    visible: actionType === "sequence"
                                    Layout.preferredWidth: 220
                                    model: { root.sequenceOptionsEpoch; return actions.sequenceOptions() }
                                    textRole: "name"
                                    valueRole: "id"
                                    selectedValue: sequenceId
                                    onActivated: app.updateHotkey(assignmentId, key, modifiers, actionType, targetProfileId, currentValue, assignmentEnabled, layoutId)
                                }
                                Label { visible: actionType === "applyWindowLayout"; text: "Layout"; color: settings.palette["#999aa4"]; font.pixelSize: 10 }
                                ValueComboBox {
                                    visible: actionType === "applyWindowLayout"; Layout.preferredWidth: 240
                                    model: { root.layoutOptionsEpoch; return windowArrangements.layoutOptions() }
                                    textRole: "name"; valueRole: "id"; selectedValue: layoutId
                                    onActivated: app.updateHotkey(assignmentId,key,modifiers,actionType,targetProfileId,sequenceId,assignmentEnabled,currentValue)
                                }
                                Item { Layout.fillWidth: true }
                            }
                        }
                    }
                }

                Label {
                    visible: hotkeys.count === 0
                    Layout.fillWidth: true
                    text: "No profile shortcuts yet. Add one below. Dedicated profile activation shortcuts are managed on the Profiles page."
                    color: settings.palette["#898a95"]
                    font.pixelSize: 11
                    wrapMode: Text.WordWrap
                    padding: 15
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: newShortcutContent.implicitHeight + 28
            radius: 13
            color: settings.palette["#1b1c22"]
            border.color: settings.palette["#37313a"]
            ColumnLayout {
                id: newShortcutContent
                anchors.fill: parent
                anchors.margins: 14
                spacing: 9
                Label { text: "Add a shortcut"; color: settings.palette["#f1eff2"]; font.pixelSize: 13; font.weight: Font.DemiBold }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 9
                    HotkeyRecorder {
                        key: root.newKey
                        modifiers: root.newModifiers
                        placeholder: "Record shortcut"
                        onRecorded: function(keyValue, modifierValue) { root.newKey = keyValue; root.newModifiers = modifierValue }
                        onCleared: { root.newKey = 0; root.newModifiers = 0 }
                    }
                    ComboBox { id: newActionType; Layout.preferredWidth: 205; model: ["Save clipboard screenshot", "Switch profile", "Run action sequence", "Apply window layout"] }


                    Item { Layout.fillWidth: true }
                    Button {
                        text: "Add shortcut"
                        enabled: root.newKey !== 0 && (newActionType.currentIndex === 0 ||
                                 (newActionType.currentIndex === 1 && newTargetProfile.currentValue !== "") ||
                                 (newActionType.currentIndex === 2 && newSequence.count > 0 && root.newSequenceId.length > 0) ||
                                 (newActionType.currentIndex === 3 && newLayout.count > 0 && root.newLayoutId.length > 0))
                        onClicked: {
                            if (app.addHotkey(root.newKey, root.newModifiers,
                                              ["saveScreenshot", "switchProfile", "sequence", "applyWindowLayout"][newActionType.currentIndex],
                                              newActionType.currentIndex === 1 ? newTargetProfile.currentValue : "",
                                              newActionType.currentIndex === 2 ? root.newSequenceId : "",
                                              newActionType.currentIndex === 3 ? root.newLayoutId : "")) {
                                root.newKey = 0
                                root.newModifiers = 0
                            }
                        }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    visible: newActionType.currentIndex !== 0
                    ComboBox {
                        id: newTargetProfile
                        visible: newActionType.currentIndex === 1
                        Layout.preferredWidth: 210
                        model: { root.profileOptionsEpoch; return profiles.profileOptions() }
                        textRole: "name"
                        valueRole: "id"
                        Component.onCompleted: if (count > 0 && currentIndex < 0) currentIndex = 0
                    }
                    ValueComboBox {
                        id: newLayout
                        visible: newActionType.currentIndex === 3
                        Layout.preferredWidth: 240
                        model: { root.layoutOptionsEpoch; return windowArrangements.layoutOptions() }
                        textRole: "name"; valueRole: "id"; selectedValue: root.newLayoutId
                        onActivated: root.newLayoutId = currentValue
                        Component.onCompleted: { const opts = windowArrangements.layoutOptions(); if (root.newLayoutId === "" && opts.length > 0) root.newLayoutId = opts[0].id }
                    }
                    ValueComboBox {
                        id: newSequence
                        visible: newActionType.currentIndex === 2
                        Layout.preferredWidth: 210
                        model: { root.sequenceOptionsEpoch; return actions.sequenceOptions() }
                        textRole: "name"
                        valueRole: "id"
                        selectedValue: root.newSequenceId
                        onActivated: root.newSequenceId = currentValue
                    }
                    Item { Layout.fillWidth: true }
                }
                Label { text: "Windows registration is shown for each assignment. Unavailable shortcuts remain saved but inactive."; color: settings.palette["#858692"]; font.pixelSize: 9 }
            }
        }
    }
}
