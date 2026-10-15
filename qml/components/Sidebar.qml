import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root
    color: settings.palette["#17181d"]
    border.color: settings.palette["#24252c"]
    border.width: 1
    property string selectedPage: "Dashboard"
    signal pageSelected(string page)

    ColumnLayout {
        anchors.fill: parent
        anchors.topMargin: 25
        anchors.bottomMargin: 18
        anchors.leftMargin: 16
        anchors.rightMargin: 16
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 4
            Layout.bottomMargin: 34
            spacing: 11
            Rectangle {
                width: 38; height: 38; radius: 12
                color: settings.palette["#e96782"]
                Label { anchors.centerIn: parent; text: "S"; color: "white"; font.pixelSize: 21; font.weight: Font.Bold }
            }
            ColumnLayout {
                spacing: 0
                Label { text: "SmallDTL"; color: settings.palette["#f2f1f4"]; font.pixelSize: 13; font.weight: Font.Bold; font.letterSpacing: 1.3 }
                Label { text: "SCREENSHOT MANAGER"; color: settings.palette["#777984"]; font.pixelSize: 8; font.letterSpacing: 0.8 }
            }
        }

        Label {
            text: "WORKSPACE"
            color: settings.palette["#71727d"]
            font.pixelSize: 10
            font.weight: Font.DemiBold
            font.letterSpacing: 1.4
            Layout.leftMargin: 12
            Layout.bottomMargin: 10
        }

        Repeater {
            model: [
                { name: "Dashboard", glyph: "◫" },
                { name: "Screenshots", glyph: "▧" },
                { name: "Clipboard History", glyph: "▤" },
                { name: "File Shelf", glyph: "▣" },
                { name: "QR Codes", glyph: "▦" },
                { name: "Hotkeys", glyph: "⌘" },
                { name: "Profiles", glyph: "◉" },
                { name: "Actions", glyph: "ϟ" },
                { name: "Window Arrangements", glyph: "▱" }
            ]
            delegate: Rectangle {
                required property var modelData
                activeFocusOnTab: true
                border.color: activeFocus ? settings.palette["#e96782"] : "transparent"
                Keys.onReturnPressed: root.pageSelected(modelData.name)
                Keys.onSpacePressed: root.pageSelected(modelData.name)
                Accessible.role: Accessible.Button
                Accessible.name: modelData.name
                Layout.fillWidth: true
                Layout.preferredHeight: 43
                Layout.bottomMargin: 4
                radius: 10
                color: root.selectedPage === modelData.name ? settings.palette["#39222b"] : navMouse.containsMouse ? settings.palette["#22232a"] : "transparent"
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 12
                    anchors.rightMargin: 12
                    spacing: 12
                    Label {
                        text: modelData.glyph
                        color: root.selectedPage === modelData.name ? settings.palette["#e96782"] : settings.palette["#90919c"]
                        font.pixelSize: 17
                        Layout.preferredWidth: 20
                        horizontalAlignment: Text.AlignHCenter
                    }
                    Label {
                        text: modelData.name
                        color: root.selectedPage === modelData.name ? settings.palette["#fff3f6"] : settings.palette["#b5b6c0"]
                        font.pixelSize: 13
                        font.weight: root.selectedPage === modelData.name ? Font.Medium : Font.Normal
                        Layout.fillWidth: true
                    }
                    Rectangle {
                        visible: root.selectedPage === modelData.name
                        width: 5; height: 5; radius: 3; color: settings.palette["#e96782"]
                    }
                }
                MouseArea {
                    id: navMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.pageSelected(modelData.name)
                }
            }
        }

        Item { Layout.fillHeight: true }

        Rectangle { Layout.fillWidth: true; height: 1; color: settings.palette["#292a31"]; Layout.bottomMargin: 15 }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 43
            activeFocusOnTab: true
            border.color: activeFocus ? settings.palette["#e96782"] : "transparent"
            Keys.onReturnPressed: root.pageSelected("Settings")
            Keys.onSpacePressed: root.pageSelected("Settings")
            Accessible.role: Accessible.Button
            Accessible.name: "Settings"
            radius: 10
            color: selectedPage === "Settings" ? settings.palette["#39222b"] : settingsMouse.containsMouse ? settings.palette["#22232a"] : "transparent"
            RowLayout {
                anchors.fill: parent; anchors.leftMargin: 12; spacing: 12
                Label { text: "⚙"; color: settings.palette["#90919c"]; font.pixelSize: 16; Layout.preferredWidth: 20; horizontalAlignment: Text.AlignHCenter }
                Label { text: "Settings"; color: settings.palette["#b5b6c0"]; font.pixelSize: 13 }
            }
            MouseArea { id: settingsMouse; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: root.pageSelected("Settings") }
        }
        Label {
            text: "LOCAL • OFFLINE"
            color: settings.palette["#60616c"]
            font.pixelSize: 9
            font.letterSpacing: 0.8
            Layout.leftMargin: 12
            Layout.topMargin: 14
        }
    }
}
