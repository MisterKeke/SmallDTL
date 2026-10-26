import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import QtQuick.Window
import "."

Window {
    id: popup
    objectName: "commandPalettePopup"
    visible: app.commandPalettePopupVisible
    width: 680
    height: 460
    minimumWidth: 580
    minimumHeight: 360
    title: "Command Palette"
    color: settings.palette["window"]
    transientParent: null
    flags: Qt.Tool | Qt.WindowStaysOnTopHint
    Material.theme: settings.darkTheme ? Material.Dark : Material.Light
    Material.accent: settings.palette["accent"]
    Material.primary: settings.palette["selection"]
    Material.background: settings.palette["surface"]
    Material.foreground: settings.palette["text"]

    function executeSelected() {
        if (app.commandPaletteExecutionPending)
            return
        const row = resultList.currentIndex
        if (row < 0 || row >= commandPalette.count)
            return
        const result = commandPalette.resultAt(row)
        if (!result.available)
            return
        if (app.executeCommandPaletteResult(result.type, result.id))
            app.setCommandPalettePopupVisible(false)
    }

    onVisibleChanged: if (visible) {
        x = Math.max(0, (Screen.width - width) / 2)
        y = Math.max(0, (Screen.height - height) / 3)
        commandPalette.setMessage("", false)
        searchField.text = ""
        commandPalette.setQuery("")
        commandPalette.refresh()
        searchField.forceActiveFocus()
    }
    onClosing: app.setCommandPalettePopupVisible(false)

    Connections {
        target: commandPalette
        function onResultsChanged() {
            resultList.currentIndex = commandPalette.count > 0 ? 0 : -1
        }
    }

    Shortcut {
        sequence: "Escape"
        context: Qt.WindowShortcut
        onActivated: app.setCommandPalettePopupVisible(false)
    }

    RetroFontScope {
        anchors.fill: parent
        anchors.margins: 1
        background: Rectangle {
            radius: settings.retroArcade ? settings.palette["panelRadius"] : 14
            color: settings.palette["#17181d"]
            border.color: settings.palette["#383942"]
        }

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 16
            spacing: 10

            RowLayout {
                Layout.fillWidth: true
                Label { text: "Command Palette"; font.pixelSize: 17; font.weight: Font.DemiBold; Layout.fillWidth: true }
                Label { text: hotkeys.commandPaletteStatus; color: settings.palette["#9697a3"]; font.pixelSize: 9 }
                ToolButton { text: "×"; Accessible.name: "Close command palette"; onClicked: app.setCommandPalettePopupVisible(false) }
            }

            TextField {
                id: searchField
                objectName: "commandPaletteSearch"
                Layout.fillWidth: true
                selectByMouse: true
                background: Rectangle {
                    radius: settings.retroArcade ? settings.palette["panelRadius"] : 8
                    color: settings.palette["#191a20"]
                    border.color: searchField.activeFocus ? settings.palette["#e96782"] : settings.palette["#2d2e37"]
                    Label {
                        anchors.left: parent.left
                        anchors.leftMargin: 14
                        anchors.verticalCenter: parent.verticalCenter
                        visible: searchField.text.length === 0
                        text: "Search commands"
                        color: settings.palette["#777984"]
                        font.pixelSize: 10
                    }
                }
                onTextEdited: commandPalette.setQuery(text)
                Keys.priority: Keys.BeforeItem
                Keys.onPressed: function(event) {
                    if (event.key === Qt.Key_Down || event.key === Qt.Key_Up) {
                        const delta = event.key === Qt.Key_Down ? 1 : -1
                        const count = resultList.count
                        if (count > 0) {
                            const current = resultList.currentIndex < 0 ? 0 : resultList.currentIndex
                            resultList.currentIndex = Math.max(0, Math.min(count - 1, current + delta))
                        }
                        event.accepted = true
                    } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                        popup.executeSelected()
                        event.accepted = true
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Label { text: commandPalette.busy ? "Searching…" : commandPalette.count + " results"; color: settings.palette["#9697a3"]; font.pixelSize: 10; Layout.fillWidth: true }
                Label { text: "↑ ↓ select    Enter run    Esc close"; color: settings.palette["#777984"]; font.pixelSize: 9 }
            }

            ListView {
                id: resultList
                objectName: "commandPaletteResults"
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: 5
                model: commandPalette
                currentIndex: commandPalette.count > 0 ? 0 : -1
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                delegate: Rectangle {
                    id: resultRow
                    required property int index
                    required property string type
                    required property string itemId
                    required property string name
                    required property string detail
                    required property string action
                    required property bool available
                    required property bool missing
                    required property string reason
                    width: ListView.view.width
                    height: 66
                    radius: settings.retroArcade ? settings.palette["panelRadius"] : 9
                    color: ListView.isCurrentItem ? settings.palette["#29232d"] : mouseArea.containsMouse ? settings.palette["#22232a"] : settings.palette["#191a20"]
                    border.color: ListView.isCurrentItem ? settings.palette["#e96782"] : settings.palette["#2d2e37"]

                    MouseArea {
                        id: mouseArea
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: resultList.currentIndex = resultRow.index
                    }
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 12
                        anchors.rightMargin: 12
                        spacing: 12
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 4
                            RowLayout {
                                Layout.fillWidth: true
                                Label { text: resultRow.type === "profile" ? "PROFILE" : resultRow.type === "sequence" ? "SEQUENCE" : resultRow.type === "layout" ? "WINDOW LAYOUT" : resultRow.type === "screenshot" ? "SCREENSHOT" : "FILE SHELF"; color: settings.palette["#e7a0b1"]; font.pixelSize: 8; font.weight: Font.DemiBold }
                                Label { text: resultRow.name; color: resultRow.available ? settings.palette["#eeedf0"] : settings.palette["#a5a6b0"]; font.pixelSize: 11; font.weight: Font.DemiBold; elide: Text.ElideRight; Layout.fillWidth: true }
                                Label { text: resultRow.action; color: resultRow.available ? settings.palette["#74d3aa"] : settings.palette["#e7b879"]; font.pixelSize: 9 }
                            }
                            Label { Layout.fillWidth: true; text: resultRow.detail; color: settings.palette["#9697a3"]; font.pixelSize: 9; elide: Text.ElideMiddle }
                            Label {
                                Layout.fillWidth: true
                                visible: resultRow.missing || !resultRow.available
                                text: resultRow.missing ? "Missing · " + resultRow.reason : resultRow.reason
                                color: resultRow.missing ? settings.palette["#ff8d9c"] : settings.palette["#e7b879"]
                                font.pixelSize: 8
                                elide: Text.ElideRight
                            }
                        }
                    }
                }

                Label {
                    anchors.centerIn: parent
                    visible: !commandPalette.busy && commandPalette.count === 0
                    text: searchField.text.length ? "No matching commands" : "Start typing to search, or use a recent command"
                    color: settings.palette["#9697a3"]
                    font.pixelSize: 11
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Label {
                    Layout.fillWidth: true
                    visible: commandPalette.message.length > 0
                    text: commandPalette.message
                    color: commandPalette.messageIsError ? settings.palette["#ff8d9c"] : settings.palette["#74d3aa"]
                    font.pixelSize: 9
                    elide: Text.ElideRight
                }
                Button {
                    objectName: "commandPaletteExecuteButton"
                    text: "Run selected"
                    enabled: !app.commandPaletteExecutionPending && resultList.currentIndex >= 0 && resultList.currentIndex < commandPalette.count && commandPalette.resultAt(resultList.currentIndex).available
                    onClicked: popup.executeSelected()
                    background: Rectangle {
                        radius: settings.retroArcade ? settings.palette["panelRadius"] : 8
                        color: parent.enabled ? (parent.down ? settings.palette["#d45270"] : settings.palette["#e96782"]) : settings.palette["#383942"]
                    }
                    contentItem: Text {
                        text: parent.text
                        color: parent.enabled ? "white" : settings.palette["#777984"]
                        font.pixelSize: 10
                        font.weight: Font.DemiBold
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        leftPadding: 9
                        rightPadding: 9
                    }
                }
            }
        }
    }
}
