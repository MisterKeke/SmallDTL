import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQml.Models
import "../components"

Item {
    id: root
    property string selectedSequenceId: ""
    property int sequenceEpoch: 0
    property int profileEpoch: 0
    property int layoutEpoch: 0

    ListModel {
        id: selectedStepModel
    }

    function selectedInfo() {
        sequenceEpoch
        profileEpoch
        return actions.sequenceInfo(selectedSequenceId)
    }

    function hasSelectedSequence() {
        sequenceEpoch
        return selectedSequenceId.length > 0 && actions.contains(selectedSequenceId)
    }

    function createSequence() {
        const id = actions.createSequence("New sequence")
        if (id.length > 0) {
            selectedSequenceId = id
        }
    }

    function reloadSteps() {
        const steps = actions.steps(selectedSequenceId)
        for (let i = 0; i < steps.length; ++i) {
            const step = steps[i]
            const values = {
                stepId: step.stepId,
                stepType: step.stepType,
                stepEnabled: step.enabled,
                stepPath: step.path,
                stepArguments: step.arguments,
                stepUrl: step.url,
                stepQrText: step.qrText,
                stepQrPixelsPerModule: step.qrPixelsPerModule,
                stepQrMarginModules: step.qrMarginModules,
                stepQrErrorCorrection: step.qrErrorCorrection,
                stepTargetProfileId: step.targetProfileId,
                stepLayoutId: step.layoutId,
                stepDelayMs: step.delayMs
            }
            // Keep existing delegates alive while parameters are edited.
            let existing = i
            while (existing < selectedStepModel.count && selectedStepModel.get(existing).stepId !== step.stepId)
                ++existing
            if (existing === selectedStepModel.count)
                selectedStepModel.insert(i, values)
            else {
                if (existing !== i)
                    selectedStepModel.move(existing, i, 1)
                selectedStepModel.set(i, values)
            }
        }
        if (selectedStepModel.count > steps.length)
            selectedStepModel.remove(steps.length, selectedStepModel.count - steps.length)
    }

    function stepTitle(type) {
        if (type === "launchApplication") return "Launch application"
        if (type === "openFolder") return "Open folder"
        if (type === "openFile") return "Open file"
        if (type === "openUrl") return "Open URL"
        if (type === "saveScreenshot") return "Save clipboard screenshot"
        if (type === "generateQr") return "Generate QR and copy image"
        if (type === "decodeQrImage") return "Decode QR image"
        if (type === "switchProfile") return "Switch profile"
        if (type === "applyWindowLayout") return "Apply window layout"
        if (type === "wait") return "Wait"
        return "Action"
    }

    Connections {
        target: actions
        function onSequencesChanged() {
            root.sequenceEpoch += 1
            if (!actions.contains(root.selectedSequenceId))
                root.selectedSequenceId = actions.sequenceOptions().length > 0 ? actions.sequenceOptions()[0].id : ""
            Qt.callLater(root.reloadSteps)
        }
    }
    Connections {
        target: profiles
        function onProfilesChanged() { root.profileEpoch += 1 }
    }
    Connections { target: windowArrangements; function onLayoutsChanged() { root.layoutEpoch += 1 } }

    onSelectedSequenceIdChanged: reloadSteps()
    Component.onCompleted: {
        if (actions.sequenceOptions().length > 0)
            selectedSequenceId = actions.sequenceOptions()[0].id
        reloadSteps()
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 12

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 75
            radius: settings.retroArcade ? settings.palette["panelRadius"] : 14
            color: settings.palette["#191a20"]
            border.color: settings.palette["#2d2e37"]
            RowLayout {
                anchors.fill: parent
                anchors.margins: 14
                spacing: 12
                Rectangle { width: 38; height: 38; radius: settings.retroArcade ? settings.palette["panelRadius"] : 12; color: settings.palette["#29232d"]; Label { anchors.centerIn: parent; text: "ϟ"; color: settings.palette["#e7a0b1"]; font.pixelSize: 19 } }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 3
                    Label { text: "Build reusable action sequences"; color: settings.palette["#efedf1"]; font.pixelSize: 13; font.weight: Font.DemiBold; Layout.fillWidth: true; elide: Text.ElideRight }
                    Label { text: actions.globalScope ? "These sequences can be assigned to application-wide shortcuts." : "This profile owns its sequences. Assign them from Hotkeys."; color: settings.palette["#898a95"]; font.pixelSize: 10; Layout.fillWidth: true; elide: Text.ElideRight }
                }
                Label { text: actions.count + " sequences"; color: settings.palette["#aaaab4"]; font.pixelSize: 10 }
                ComboBox {
                    objectName: "sequenceScopePicker"
                    implicitContentWidthPolicy: ComboBox.WidestText
                    Layout.minimumWidth: implicitWidth + (contentItem.leftPadding || 0) + (contentItem.rightPadding || 0)
                    model: ["Active profile", "Application-wide"]
                    currentIndex: actions.globalScope ? 1 : 0
                    onActivated: app.setHotkeyScope(currentIndex === 1)
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 12

            Rectangle {
                Layout.preferredWidth: 255
                Layout.fillHeight: true
                radius: settings.retroArcade ? settings.palette["panelRadius"] : 13
                color: settings.palette["#191a20"]
                border.color: settings.palette["#2d2e37"]
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 9
                    RowLayout {
                        Layout.fillWidth: true
                        Label { text: "Sequences"; color: settings.palette["#f1eff2"]; font.pixelSize: 13; font.weight: Font.DemiBold; Layout.fillWidth: true }
                        Button {
                            Layout.preferredWidth: 28
                            Layout.preferredHeight: 28
                            Layout.minimumWidth: 28
                            Layout.minimumHeight: 28
                            padding: 0
                            leftPadding: 0
                            rightPadding: 0
                            topPadding: 0
                            bottomPadding: 0
                            onClicked: root.createSequence()
                            background: Rectangle {
                                radius: width / 2
                                color: parent.down ? settings.palette["#4a2c37"] : settings.palette["#29232d"]
                                border.color: settings.palette["#50303a"]
                            }
                            contentItem: Item {
                                Label {
                                    anchors.centerIn: parent
                                    text: "+"
                                    color: settings.palette["#f1eff2"]
                                    font.pixelSize: 17
                                    font.weight: Font.Medium
                                }
                            }
                            ToolTip.visible: hovered
                            ToolTip.text: "Create sequence"
                        }
                    }
                    ScrollView {
                        id: sequenceListScroll
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        contentWidth: availableWidth
                        clip: true
                        ColumnLayout {
                            width: sequenceListScroll.availableWidth
                            spacing: 7
                            Repeater {
                                model: actions
                                delegate: Rectangle {
                                    objectName: "sequence-" + sequenceId
                                    required property string sequenceId
                                    required property string name
                                    required property bool sequenceEnabled
                                    required property int stepCount
                                    activeFocusOnTab: true
                                    Keys.onReturnPressed: root.selectedSequenceId = sequenceId
                                    Keys.onSpacePressed: root.selectedSequenceId = sequenceId
                                    Accessible.role: Accessible.ListItem
                                    Accessible.name: name
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 63
                                    radius: settings.retroArcade ? settings.palette["panelRadius"] : 10
                                    color: root.selectedSequenceId === sequenceId ? settings.palette["#39222b"] : settings.palette["#202127"]
                                    border.color: activeFocus ? settings.palette["#e96782"] : root.selectedSequenceId === sequenceId ? settings.palette["#6a3645"] : settings.palette["#30313a"]
                                    ColumnLayout {
                                        anchors.fill: parent
                                        anchors.margins: 10
                                        spacing: 3
                                        Label { text: name; color: sequenceEnabled ? settings.palette["#efedf1"] : settings.palette["#898a95"]; font.pixelSize: 11; font.weight: Font.DemiBold; elide: Text.ElideRight; Layout.fillWidth: true }
                                        Label { text: stepCount + (stepCount === 1 ? " step" : " steps") + (sequenceEnabled ? "" : " · disabled"); color: settings.palette["#8b8c97"]; font.pixelSize: 9 }
                                    }
                                    MouseArea { anchors.fill: parent; onClicked: { parent.forceActiveFocus(); root.selectedSequenceId = sequenceId } }
                                }
                            }
                            Label {
                                visible: actions.count === 0
                                Layout.fillWidth: true
                                text: "Create a sequence to start. Add steps, then assign it to a shortcut from Hotkeys."
                                color: settings.palette["#898a95"]
                                font.pixelSize: 10
                                wrapMode: Text.WordWrap
                                padding: 8
                            }
                        }
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                radius: settings.retroArcade ? settings.palette["panelRadius"] : 13
                color: settings.palette["#191a20"]
                border.color: settings.palette["#2d2e37"]

                ColumnLayout {
                    objectName: "sequenceEditor"
                    anchors.fill: parent
                    anchors.margins: 14
                    spacing: 10
                    visible: root.hasSelectedSequence()

                    RowLayout {
                        Layout.fillWidth: true
                        TextField {
                            objectName: "sequenceNameEditor"
                            Layout.fillWidth: true
                            text: root.selectedInfo().name || ""
                            placeholderText: "Sequence name"
                            onEditingFinished: { if (text.trim().length > 0) actions.renameSequence(root.selectedSequenceId, text); text = Qt.binding(function() { return root.selectedInfo().name || "" }) }
                        }
                        Switch {
                            objectName: "sequenceEnabledSwitch"
                            text: "Enabled"
                            checked: !!root.selectedInfo().enabled
                            onToggled: { actions.setSequenceEnabled(root.selectedSequenceId, checked); checked = Qt.binding(function() { return !!root.selectedInfo().enabled }) }
                        }
                        Button {
                            text: actionRunner.running ? "Running…" : "Run"
                            enabled: !actionRunner.running && !!root.selectedInfo().enabled
                            onClicked: app.runActionSequence(root.selectedSequenceId)
                        }
                        Button { text: "Stop"; visible: actionRunner.running; onClicked: app.cancelActionSequence() }
                        ToolButton { text: "×"; onClicked: actions.deleteSequence(root.selectedSequenceId); ToolTip.visible: hovered; ToolTip.text: "Delete sequence" }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 34
                        radius: settings.retroArcade ? settings.palette["panelRadius"] : 8
                        color: actionRunner.running ? settings.palette["#2a2630"] : settings.palette["#202127"]
                        Label {
                            anchors.fill: parent
                            anchors.leftMargin: 10
                            anchors.rightMargin: 10
                            verticalAlignment: Text.AlignVCenter
                            text: actionRunner.running ? actionRunner.status : (root.selectedInfo().validationError || "Ready")
                            color: root.selectedInfo().validationError ? settings.palette["#e7b879"] : settings.palette["#a5a5ae"]
                            font.pixelSize: 10
                            elide: Text.ElideRight
                        }
                    }

                    Label {
                        text: "Ordered steps"
                        color: settings.palette["#f1eff2"]
                        font.pixelSize: 12
                        font.weight: Font.DemiBold
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        ComboBox {
                            id: stepTypePicker
                            objectName: "stepTypePicker"
                            implicitContentWidthPolicy: ComboBox.WidestText
                            Layout.minimumWidth: implicitWidth
                            Layout.fillWidth: true
                            model: ["Launch application", "Open folder", "Open file", "Open URL", "Save clipboard screenshot", "Generate QR and copy image", "Decode QR image", "Switch profile", "Wait", "Apply window layout"]
                        }
                        Button {
                            objectName: "addStepButton"
                            text: "Add step"
                            enabled: root.hasSelectedSequence()
                            onClicked: {
                                if (!root.hasSelectedSequence())
                                    return
                                const types = ["launchApplication", "openFolder", "openFile", "openUrl", "saveScreenshot", "generateQr", "decodeQrImage", "switchProfile", "wait", "applyWindowLayout"]
                                const stepId = actions.addStep(root.selectedSequenceId, types[stepTypePicker.currentIndex])
                                if (stepId.length > 0)
                                    app.dismissMessage()
                            }
                        }
                    }

                    ScrollView {
                        id: stepsScroll
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        contentWidth: availableWidth
                        clip: true
                        ScrollBar.vertical.policy: ScrollBar.AsNeeded
                        ColumnLayout {
                            width: stepsScroll.availableWidth
                            spacing: 8
                            Repeater {
                                model: selectedStepModel
                                delegate: Rectangle {
                                    required property int index
                                    objectName: "step-" + stepId
                                    required property string stepId
                                    required property string stepType
                                    required property bool stepEnabled
                                    required property string stepPath
                                    required property string stepArguments
                                    required property string stepUrl
                                    required property string stepQrText
                                    required property int stepQrPixelsPerModule
                                    required property int stepQrMarginModules
                                    required property string stepQrErrorCorrection
                                    required property string stepTargetProfileId
                                    required property string stepLayoutId
                                    required property int stepDelayMs
                                    Layout.fillWidth: true
                                    implicitHeight: stepContent.implicitHeight + 20
                                    radius: settings.retroArcade ? settings.palette["panelRadius"] : 10
                                    color: settings.palette["#202127"]
                                    border.color: stepEnabled ? settings.palette["#34353e"] : settings.palette["#282930"]

                                    ColumnLayout {
                                        id: stepContent
                                        anchors.left: parent.left
                                        anchors.right: parent.right
                                        anchors.top: parent.top
                                        anchors.margins: 10
                                        spacing: 7
                                        RowLayout {
                                            Layout.fillWidth: true
                                            Label { text: (index + 1) + ".  " + root.stepTitle(stepType); color: settings.palette["#efedf1"]; font.pixelSize: 10; font.weight: Font.DemiBold; Layout.fillWidth: true }
                                            Switch { objectName: "stepEnabledSwitch"; checked: stepEnabled; onToggled: { actions.setStepEnabled(root.selectedSequenceId, stepId, checked); checked = Qt.binding(function() { return stepEnabled }) } }
                                            ToolButton { objectName: "moveStepUpButton"; text: "↑"; enabled: index > 0; onClicked: actions.moveStep(root.selectedSequenceId, stepId, -1) }
                                            ToolButton { objectName: "moveStepDownButton"; text: "↓"; enabled: index < selectedStepModel.count - 1; onClicked: actions.moveStep(root.selectedSequenceId, stepId, 1) }
                                            ToolButton { objectName: "removeStepButton"; text: "×"; onClicked: actions.removeStep(root.selectedSequenceId, stepId) }
                                        }
                                        RowLayout {
                                            Layout.fillWidth: true
                                            visible: stepType === "launchApplication" || stepType === "openFolder" || stepType === "openFile" || stepType === "decodeQrImage"
                                            Label { text: stepType === "launchApplication" ? "Executable" : stepType === "openFolder" ? "Folder" : stepType === "decodeQrImage" ? "Image" : "File"; color: settings.palette["#999aa4"]; font.pixelSize: 9; Layout.preferredWidth: 55 }
                                            TextField {
                                                id: pathEditor
                                                objectName: "stepPathEditor"
                                                Layout.fillWidth: true
                                                text: stepPath
                                                placeholderText: "Enter a full path"
                                                onEditingFinished: { actions.updateStep(root.selectedSequenceId, stepId, { "path": text }); text = Qt.binding(function() { return stepPath }) }
                                            }
                                            Button {
                                                text: "Browse"
                                                onClicked: {
                                                    const selected = app.chooseActionPath(stepType, stepPath)
                                                    if (selected.length > 0) actions.updateStep(root.selectedSequenceId, stepId, { "path": selected })
                                                }
                                            }
                                        }
                                        RowLayout {
                                            Layout.fillWidth: true
                                            visible: stepType === "generateQr"
                                            Label { text: "Text / URL"; color: settings.palette["#999aa4"]; font.pixelSize: 9; Layout.preferredWidth: 55 }
                                            TextField {
                                                Layout.fillWidth: true
                                                text: stepQrText
                                                objectName: "stepQrTextEditor"
                                                placeholderText: "Text or URL to encode"
                                                onEditingFinished: { actions.updateStep(root.selectedSequenceId, stepId, { "qrText": text }); text = Qt.binding(function() { return stepQrText }) }
                                            }
                                        }
                                        RowLayout {
                                            Layout.fillWidth: true
                                            visible: stepType === "generateQr"
                                            Label { text: "QR image"; color: settings.palette["#999aa4"]; font.pixelSize: 9; Layout.preferredWidth: 55 }
                                            Label { text: "px/module"; color: settings.palette["#999aa4"]; font.pixelSize: 9 }
                                            SpinBox {
                                                from: 1; to: 20; value: stepQrPixelsPerModule
                                                onValueModified: { actions.updateStep(root.selectedSequenceId, stepId, { "qrPixelsPerModule": value }); value = Qt.binding(function() { return stepQrPixelsPerModule }) }
                                            }
                                            Label { text: "Margin"; color: settings.palette["#999aa4"]; font.pixelSize: 9 }
                                            SpinBox {
                                                from: 0; to: 20; value: stepQrMarginModules
                                                onValueModified: { actions.updateStep(root.selectedSequenceId, stepId, { "qrMarginModules": value }); value = Qt.binding(function() { return stepQrMarginModules }) }
                                            }
                                            ValueComboBox {
                                                model: [{code: "L", label: "L"}, {code: "M", label: "M"}, {code: "Q", label: "Q"}, {code: "H", label: "H"}]
                                                textRole: "label"
                                                valueRole: "code"
                                                selectedValue: stepQrErrorCorrection
                                                Layout.preferredWidth: 66
                                                onActivated: actions.updateStep(root.selectedSequenceId, stepId, { "qrErrorCorrection": currentValue })
                                            }
                                        }
                                        RowLayout {
                                            Layout.fillWidth: true
                                            visible: stepType === "launchApplication"
                                            Label { text: "Arguments"; color: settings.palette["#999aa4"]; font.pixelSize: 9; Layout.preferredWidth: 55 }
                                            TextField {
                                                Layout.fillWidth: true
                                                text: stepArguments
                                                objectName: "stepArgumentsEditor"
                                                placeholderText: "Optional arguments, for example --new-window"
                                                onEditingFinished: { actions.updateStep(root.selectedSequenceId, stepId, { "arguments": text }); text = Qt.binding(function() { return stepArguments }) }
                                            }
                                        }
                                        RowLayout {
                                            Layout.fillWidth: true
                                            visible: stepType === "openUrl"
                                            Label { text: "URL"; color: settings.palette["#999aa4"]; font.pixelSize: 9; Layout.preferredWidth: 55 }
                                            TextField {
                                                Layout.fillWidth: true
                                                text: stepUrl
                                                objectName: "stepUrlEditor"
                                                placeholderText: "https://example.com"
                                                onEditingFinished: { actions.updateStep(root.selectedSequenceId, stepId, { "url": text }); text = Qt.binding(function() { return stepUrl }) }
                                            }
                                        }
                                        RowLayout {
                                            Layout.fillWidth: true
                                            visible: stepType === "applyWindowLayout"
                                            Label { text: "Saved layout"; color: settings.palette["#999aa4"]; font.pixelSize: 9; Layout.preferredWidth: 65 }
                                            ValueComboBox {
                                                Layout.preferredWidth: 300
                                                model: { root.layoutEpoch; return windowArrangements.layoutOptions() }
                                                textRole: "name"; valueRole: "id"; selectedValue: stepLayoutId
                                                onActivated: actions.updateStep(root.selectedSequenceId, stepId, { "layoutId": currentValue })
                                            }
                                            Label { text: "Only this explicit step applies the layout."; color: settings.palette["#898a95"]; font.pixelSize: 9 }
                                        }
                                        RowLayout {
                                            Layout.fillWidth: true
                                            visible: stepType === "switchProfile"
                                            Label { text: "Profile"; color: settings.palette["#999aa4"]; font.pixelSize: 9; Layout.preferredWidth: 55 }
                                            ValueComboBox {
                                                Layout.preferredWidth: 260
                                                model: { root.profileEpoch; return profiles.profileOptions() }
                                                textRole: "name"
                                                valueRole: "id"
                                                selectedValue: stepTargetProfileId
                                                onActivated: actions.updateStep(root.selectedSequenceId, stepId, { "targetProfileId": currentValue })
                                            }
                                        }
                                        RowLayout {
                                            Layout.fillWidth: true
                                            visible: stepType === "wait"
                                            Label { text: "Duration"; color: settings.palette["#999aa4"]; font.pixelSize: 9; Layout.preferredWidth: 55 }
                                            SpinBox {
                                                from: 0; to: 600000; stepSize: 250
                                                value: stepDelayMs
                                                onValueModified: { actions.updateStep(root.selectedSequenceId, stepId, { "delayMs": value }); value = Qt.binding(function() { return stepDelayMs }) }
                                            }
                                            Label { text: "milliseconds"; color: settings.palette["#999aa4"]; font.pixelSize: 9 }
                                        }
                                    }
                                }
                            }
                            Label {
                                visible: root.hasSelectedSequence() && selectedStepModel.count === 0
                                Layout.fillWidth: true
                                text: "No steps yet. Add the first action above."
                                color: settings.palette["#898a95"]
                                font.pixelSize: 10
                                padding: 14
                            }
                        }
                    }
                }

                ColumnLayout {
                    anchors.centerIn: parent
                    visible: !root.hasSelectedSequence()
                    Label { text: actions.count > 0 ? "Select a sequence" : "Create a sequence with +"; color: settings.palette["#efedf1"]; font.pixelSize: 14; font.weight: Font.DemiBold; Layout.alignment: Qt.AlignHCenter }
                    Label { text: actions.count > 0 ? "Choose one from the list to edit its actions." : "Then choose an action type and add it to the sequence."; color: settings.palette["#898a95"]; font.pixelSize: 10; Layout.alignment: Qt.AlignHCenter }
                    Button {
                        visible: actions.count === 0
                        text: "Create sequence"
                        Layout.alignment: Qt.AlignHCenter
                        onClicked: root.createSequence()
                    }
                }
            }
        }
    }
}
