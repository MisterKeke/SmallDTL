import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    property int anchorRow: -1
    property int optionsEpoch: 0
    property string newLayoutName: ""
    function layoutOptions() { optionsEpoch; return windowArrangements.layouts }
    function chooseRow(row, modifiers) {
        const shift = (modifiers & Qt.ShiftModifier) !== 0
        const control = (modifiers & Qt.ControlModifier) !== 0
        if (shift && anchorRow >= 0)
            windowArrangements.selectRange(anchorRow, row, control, false)
        else if (control) {
            // Ctrl-click toggles the anchor while preserving the rest.
            windowArrangements.selectRange(row, row, true, true)
            anchorRow = row
        } else {
            windowArrangements.selectRange(row, row, false, false)
            anchorRow = row
        }
    }
    Connections { target: windowArrangements; function onLayoutsChanged() { root.optionsEpoch += 1 } }

    ColumnLayout {
        anchors.fill: parent
        spacing: 10
        Rectangle {
            Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumHeight: 220
            radius: 13; color: settings.palette["#191a20"]; border.color: settings.palette["#2d2e37"]
            ColumnLayout {
                anchors.fill: parent; anchors.margins: 12; spacing: 8
                RowLayout {
                    Layout.fillWidth: true
                    Label { text: "Open windows"; color: settings.palette["#f1eff2"]; font.pixelSize: 14; font.weight: Font.DemiBold; Layout.fillWidth: true }
                    Button { text: windowArrangements.busy ? "Refreshing…" : "Refresh"; enabled: !windowArrangements.busy; onClicked: windowArrangements.refreshWindows() }
                    Button { text: "Select All"; enabled: windowArrangements.count > 0; onClicked: windowArrangements.selectAll() }
                    Button { text: "Clear selection"; enabled: windowArrangements.count > 0; onClicked: windowArrangements.clearSelection() }
                    Button { text: "Restore previous"; onClicked: windowArrangements.restorePreviousArrangement() }
                }
                Label { Layout.fillWidth: true; text: "Eligible top-level windows · checkbox, Ctrl-click, or Shift-click to select"; color: settings.palette["#9697a3"]; font.pixelSize: 10 }
                RowLayout {
                    Layout.fillWidth: true
                    Repeater {
                        model: [ ["leftHalf", "Left half"], ["rightHalf", "Right half"], ["topLeft", "Top left"], ["topRight", "Top right"], ["bottomLeft", "Bottom left"], ["bottomRight", "Bottom right"], ["center", "Center"], ["maximize", "Maximize"] ]
                        delegate: Button { required property var modelData; text: modelData[1]; onClicked: windowArrangements.arrangeSelected(modelData[0]) }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    ComboBox {
                        id: monitorPicker; Layout.minimumWidth: 160
                        model: windowArrangements.monitors; textRole: "name"; valueRole: "device"
                        displayText: currentIndex >= 0 ? currentText : "Move to monitor…"
                    }
                    Button { text: "Move selected"; enabled: monitorPicker.currentValue !== undefined && monitorPicker.currentValue !== ""; onClicked: windowArrangements.moveSelectedToMonitor(monitorPicker.currentValue) }
                }
                ListView {
                    id: windowList; Layout.fillWidth: true; Layout.fillHeight: true; clip: true; spacing: 4
                    objectName: "windowArrangementWindowList"
                    model: windowArrangements
                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                    delegate: Rectangle {
                        objectName: "windowArrangementWindowDelegate"
                        width: windowList.width
                        required property int index
                        required property string windowId
                        required property string title
                        required property string executable
                        required property string monitor
                        required property bool selected
                        Layout.fillWidth: true; height: 48; radius: 8
                        color: selected ? settings.palette["#30212a"] : settings.palette["#202127"]
                        RowLayout {
                            anchors.fill: parent; anchors.leftMargin: 7; anchors.rightMargin: 10; spacing: 9
                            CheckBox { objectName: "windowArrangementWindowCheckBox"; checked: selected; onToggled: { windowArrangements.setWindowSelected(windowId, checked); root.anchorRow = index } }
                            ColumnLayout {
                                Layout.fillWidth: true; spacing: 1
                                Label { objectName: "windowArrangementWindowTitle"; text: title; color: settings.palette["#efedf1"]; font.pixelSize: 11; elide: Text.ElideRight; Layout.fillWidth: true }
                                Label { text: executable; color: settings.palette["#858692"]; font.pixelSize: 9; elide: Text.ElideMiddle; Layout.fillWidth: true }
                            }
                            Label { text: monitor; color: settings.palette["#aaaab4"]; font.pixelSize: 9; Layout.preferredWidth: 100; elide: Text.ElideRight }
                        }
                    MouseArea {
                            anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; anchors.bottom: parent.bottom
                            anchors.leftMargin: 48; anchors.rightMargin: 130; z: 1
                            onClicked: function(mouse) { root.chooseRow(index, mouse.modifiers) }
                        }
                    }
                    Label { anchors.centerIn: parent; visible: windowArrangements.count === 0 && !windowArrangements.busy; text: "No eligible windows found. This tool is available on Windows."; color: settings.palette["#8b8c97"]; font.pixelSize: 11 }
                }
            }
        }
        Rectangle {
            Layout.fillWidth: true; Layout.preferredHeight: 205; radius: 13
            color: settings.palette["#191a20"]; border.color: settings.palette["#2d2e37"]
            RowLayout {
                anchors.fill: parent; anchors.margins: 12; spacing: 12
                ColumnLayout {
                    Layout.preferredWidth: 300; Layout.fillHeight: true; spacing: 7
                    RowLayout {
                        Label { text: "Saved layouts"; color: settings.palette["#f1eff2"]; font.pixelSize: 13; font.weight: Font.DemiBold; Layout.fillWidth: true }
                        TextField { id: layoutName; Layout.preferredWidth: 130; placeholderText: "Layout name"; text: root.newLayoutName; onTextEdited: root.newLayoutName = text }
                        Button { text: "Save selected"; enabled: root.newLayoutName.trim().length > 0; onClicked: { const id = windowArrangements.saveSelectedLayout(root.newLayoutName); if (id.length) { root.newLayoutName = ""; layoutName.text = "" } } }
                    }
                    Label { text: "Layouts are saved to the active profile."; color: settings.palette["#898a95"]; font.pixelSize: 9 }
                    ListView {
                        id: savedLayoutsList
                        objectName: "windowArrangementLayouts"
                        Layout.fillWidth: true; Layout.fillHeight: true; clip: true; spacing: 4
                        model: root.layoutOptions()
                        delegate: Rectangle {
                            width: savedLayoutsList.width
                            objectName: "windowArrangementLayoutDelegate"
                            required property string layoutId; required property string name; required property int count
                            Layout.fillWidth: true; height: 38; radius: 7
                            color: windowArrangements.selectedLayoutId === layoutId ? settings.palette["#39222b"] : settings.palette["#202127"]
                            RowLayout {
                                anchors.fill: parent; anchors.leftMargin: 9; anchors.rightMargin: 4
                                Label { text: name + " · " + count; color: settings.palette["#efedf1"]; Layout.fillWidth: true; elide: Text.ElideRight; font.pixelSize: 10 }
                                ToolButton { text: "Preview"; onClicked: windowArrangements.previewLayout(layoutId) }
                                ToolButton { text: "×"; Accessible.name: "Delete layout"; onClicked: windowArrangements.deleteLayout(layoutId) }
                            }
                        }
                        Label { anchors.centerIn: parent; visible: root.layoutOptions().length === 0; text: "No layouts in this profile."; color: settings.palette["#898a95"]; font.pixelSize: 10 }
                    }
                }
                Rectangle { Layout.fillHeight: true; width: 1; color: settings.palette["#30313a"] }
                ColumnLayout {
                    Layout.fillWidth: true; Layout.fillHeight: true; spacing: 6
                    RowLayout {
                        Layout.fillWidth: true
                        Label { text: "Preview · matched, missing, ambiguous"; color: settings.palette["#f1eff2"]; font.pixelSize: 12; font.weight: Font.DemiBold; Layout.fillWidth: true }
                        TextField {
                            visible: windowArrangements.selectedLayoutId.length > 0
                            Layout.preferredWidth: 150; placeholderText: "Rename selected layout"
                            text: { root.optionsEpoch; return windowArrangements.layoutInfo(windowArrangements.selectedLayoutId).name || "" }
                            onEditingFinished: windowArrangements.renameLayout(windowArrangements.selectedLayoutId, text)
                        }
                        Button { text: "Apply layout"; enabled: windowArrangements.selectedLayoutId.length > 0 && windowArrangements.preview.length > 0; onClicked: windowArrangements.applyLayout(windowArrangements.selectedLayoutId) }
                    }
                    ListView {
                        id: previewList
                        objectName: "windowArrangementPreview"
                        Layout.fillWidth: true; Layout.fillHeight: true; clip: true; spacing: 5
                        model: windowArrangements.preview
                        delegate: Rectangle {
                            width: previewList.width
                            objectName: "windowArrangementPreviewDelegate"
                            required property int index; required property string executable; required property string titleRule
                            required property string monitor; required property bool monitorMissing; required property string status; required property string resolvedId
                            property int slotIndex: index
                            required property var candidates
                            Layout.fillWidth: true; height: status === "Ambiguous" || status === "Resolved" ? 70 : 53; radius: 7; color: settings.palette["#202127"]
                            ColumnLayout {
                                anchors.fill: parent; anchors.margins: 7; spacing: 4
                                RowLayout {
                                    Layout.fillWidth: true
                                    Label { text: status + (monitorMissing ? " · monitor missing" : "") + " · " + monitor; color: status === "Missing" ? settings.palette["#e7b879"] : status === "Ambiguous" ? settings.palette["#e7b879"] : settings.palette["#74d3aa"]; font.pixelSize: 9; Layout.fillWidth: true; elide: Text.ElideRight }
                                    TextField { Layout.preferredWidth: 180; placeholderText: "Optional title contains"; text: titleRule; onEditingFinished: windowArrangements.setTitleRule(windowArrangements.selectedLayoutId, slotIndex, text) }
                                }
                                RowLayout {
                                    visible: status === "Ambiguous" || status === "Resolved"; Layout.fillWidth: true
                                    Label { text: "Choose window"; color: settings.palette["#999aa4"]; font.pixelSize: 9 }
                                    ComboBox {
                                        Layout.fillWidth: true; model: candidates; textRole: "title"; valueRole: "id"
                                        displayText: currentIndex >= 0 ? currentText : "Resolve ambiguous match…"
                                        currentIndex: status === "Resolved" ? candidates.findIndex(candidate => candidate.id === resolvedId) : -1
                                        onActivated: windowArrangements.resolveAmbiguous(slotIndex, currentValue)
                                    }
                                }
                            }
                        }
                        Label { anchors.centerIn: parent; visible: windowArrangements.preview.length === 0; text: "Preview a saved layout before applying it."; color: settings.palette["#898a95"]; font.pixelSize: 10 }
                    }
                }
            }
        }
        Label { Layout.fillWidth: true; text: windowArrangements.status; color: windowArrangements.status.indexOf("unavailable") >= 0 || windowArrangements.status.indexOf("failed") >= 0 ? settings.palette["#ff8d9c"] : settings.palette["#9697a3"]; font.pixelSize: 10; elide: Text.ElideRight }
    }
}
