import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    ColumnLayout {
        anchors.fill: parent
        spacing: 20

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 185
            radius: 18
            color: settings.palette["#201a20"]
            border.color: settings.palette["#382830"]
            RowLayout {
                anchors.fill: parent
                anchors.margins: 27
                spacing: 20
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 10
                    Label { text: "READY WHEN YOU ARE"; color: settings.palette["#e88ba0"]; font.pixelSize: 10; font.weight: Font.DemiBold; font.letterSpacing: 1.3 }
                    Label { text: "Keep your captures\nand shortcuts in flow."; color: settings.palette["#f6f1f3"]; font.pixelSize: 25; font.weight: Font.DemiBold; lineHeight: 1.05 }
                    Label { text: "Clipboard screenshots are saved as a separate PNG copy."; color: settings.palette["#aaa0a7"]; font.pixelSize: 12 }
                }
                Rectangle {
                    Layout.preferredWidth: 190
                    Layout.preferredHeight: 126
                    radius: 16
                    color: settings.palette["#30222a"]
                    ColumnLayout {
                        anchors.centerIn: parent
                        spacing: 8
                        Label { text: "ACTIVE PROFILE"; color: settings.palette["#bd8b99"]; font.pixelSize: 9; font.letterSpacing: 1.1; Layout.alignment: Qt.AlignHCenter }
                        Label { text: profiles.activeProfileName; color: settings.palette["#fff0f3"]; font.pixelSize: 19; font.weight: Font.DemiBold; Layout.alignment: Qt.AlignHCenter }
                        Label { text: hotkeys.registeredCount + " registered shortcuts"; color: settings.palette["#aa9da3"]; font.pixelSize: 11; Layout.alignment: Qt.AlignHCenter }
                    }
                }
                Button {
                    id: saveClipboardButton
                    text: "Save clipboard image"
                    Layout.alignment: Qt.AlignVCenter
                    onClicked: app.saveClipboardScreenshot()
                    background: Rectangle { radius: 10; color: saveClipboardButton.down ? settings.palette["#d45270"] : settings.palette["#e96782"] }
                    contentItem: Text { text: saveClipboardButton.text; color: "white"; font.pixelSize: 12; font.weight: Font.DemiBold; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; leftPadding: 12; rightPadding: 12 }
                }
            }
        }

        GridLayout {
            Layout.fillWidth: true
            columns: 3
            columnSpacing: 14
            rowSpacing: 14
            Repeater {
                model: [
                    { title: "PROFILES", value: profiles.count, note: "Manual profile switching" },
                    { title: "ACTIVE HOTKEYS", value: hotkeys.registeredCount, note: "Successfully registered" },
                    { title: "SCREENSHOTS", value: screenshots.gallery.count, note: "In the current destination" }
                ]
                delegate: Rectangle {
                    required property var modelData
                    Layout.fillWidth: true
                    Layout.preferredHeight: 115
                    radius: 15
                    color: settings.palette["#191a20"]
                    border.color: settings.palette["#2d2e37"]
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 17
                        spacing: 6
                        Label { text: modelData.title; color: settings.palette["#858692"]; font.pixelSize: 9; font.weight: Font.DemiBold; font.letterSpacing: 1 }
                        Label { text: modelData.value; color: settings.palette["#f1eff2"]; font.pixelSize: 27; font.weight: Font.DemiBold }
                        Label { text: modelData.note; color: settings.palette["#9697a3"]; font.pixelSize: 10 }
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: 16
            color: settings.palette["#191a20"]
            border.color: settings.palette["#2d2e37"]
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 20
                spacing: 13
                RowLayout {
                    Layout.fillWidth: true
                    Label { text: "Screenshot destination"; color: settings.palette["#f1eff2"]; font.pixelSize: 15; font.weight: Font.DemiBold; Layout.fillWidth: true }
                    Button { text: "Open folder"; onClicked: app.openScreenshotDestination() }
                    Button { text: "Choose for this session"; onClicked: app.chooseScreenshotDestination() }
                }
                Label { text: screenshots.destination; color: settings.palette["#aaaab4"]; font.pixelSize: 12; elide: Text.ElideMiddle; Layout.fillWidth: true }
                Rectangle { Layout.fillWidth: true; height: 1; color: settings.palette["#2d2e37"] }
                Label { text: "Recently saved"; color: settings.palette["#f1eff2"]; font.pixelSize: 14; font.weight: Font.DemiBold }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10
                    Repeater {
                        model: screenshots.gallery.recent
                        delegate: Rectangle {
                            required property int index
                            required property url imageUrl
                            required property url thumbnailUrl
                            required property string fileName
                            required property string path
                            required property bool missing
                            activeFocusOnTab: true
                            border.color: activeFocus ? settings.palette["#e96782"] : "transparent"
                            Keys.onReturnPressed: screenshots.openImage(path)
                            Keys.onSpacePressed: screenshots.openImage(path)
                            Accessible.role: Accessible.Button
                            Accessible.name: "Open " + fileName
                            Layout.preferredWidth: 128
                            Layout.preferredHeight: 100
                            radius: 9
                            color: settings.palette["#23242a"]
                            clip: true
                            Image { anchors.fill: parent; source: missing ? "" : thumbnailUrl; sourceSize: Qt.size(128, 100); fillMode: Image.PreserveAspectCrop; asynchronous: true }
                            Label { anchors.centerIn: parent; visible: missing; text: "File missing"; font.pixelSize: 10; color: settings.palette["#9697a3"] }
                            MouseArea { anchors.fill: parent; onClicked: screenshots.openImage(path) }
                            Label { anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; padding: 7; text: fileName; color: "white"; font.pixelSize: 9; elide: Text.ElideMiddle; background: Rectangle { color: settings.palette["#aa111216"] } }
                        }
                    }
                    Label {
                        visible: screenshots.gallery.count === 0
                        text: "No screenshots saved here yet. Copy an image to the clipboard, then save it from Screenshots."
                        color: settings.palette["#858692"]
                        font.pixelSize: 12
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                    }
                }
                Item { Layout.fillHeight: true }
            }
        }
    }
}
