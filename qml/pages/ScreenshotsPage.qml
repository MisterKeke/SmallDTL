import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window

Item {
    id: root
    property string selectedPath: ""
    property url selectedImage: ""
    function textEditorHasFocus() {
        const window = root.Window.window
        for (let item = window ? window.activeFocusItem : null; item; item = item.parent)
            if (item instanceof TextInput || item instanceof TextEdit) return true
        return false
    }
    function preview(path, image) {
        root.selectedPath = path
        root.selectedImage = image
        previewDialog.open()
    }

    Shortcut {
        sequence: "Ctrl+V"
        context: Qt.WindowShortcut
        enabled: root.visible && app.currentPage === "Screenshots" && !renameDialog.visible && !previewDialog.visible && !removeDialog.visible && !deleteDialog.visible && !root.textEditorHasFocus()
        onActivated: app.saveClipboardScreenshot()
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 15

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 93
            radius: 15
            color: settings.palette["#191a20"]
            border.color: settings.palette["#2d2e37"]
            RowLayout {
                anchors.fill: parent
                anchors.margins: 16
                spacing: 13
                Rectangle {
                    width: 43; height: 43; radius: 13
                    color: screenshots.clipboardHasImage ? settings.palette["#1f352c"] : settings.palette["#282932"]
                    Label { anchors.centerIn: parent; text: screenshots.clipboardHasImage ? "✓" : "▧"; color: screenshots.clipboardHasImage ? settings.palette["#74d3aa"] : settings.palette["#b0b1bb"]; font.pixelSize: 18 }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 4
                    Label { text: screenshots.clipboardHasImage ? "An image is ready to save" : "Copy a screenshot to the clipboard"; color: settings.palette["#f1eff2"]; font.pixelSize: 13; font.weight: Font.DemiBold }
                    Label { text: screenshots.destination; color: settings.palette["#9697a3"]; font.pixelSize: 10; elide: Text.ElideMiddle; Layout.fillWidth: true }
                }
                Button {
                    id: pasteScreenshotButton
                    text: "Paste Screenshot"
                    enabled: screenshots.clipboardHasImage
                    onClicked: app.saveClipboardScreenshot()
                    background: Rectangle { radius: 9; color: pasteScreenshotButton.enabled ? (pasteScreenshotButton.down ? settings.palette["#d45270"] : settings.palette["#e96782"]) : settings.palette["#383942"] }
                    contentItem: Text { text: pasteScreenshotButton.text; color: pasteScreenshotButton.enabled ? "white" : settings.palette["#777984"]; font.pixelSize: 11; font.weight: Font.DemiBold; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; leftPadding: 12; rightPadding: 12 }
                }
                Button { text: "Choose folder"; onClicked: app.chooseScreenshotDestination() }
                Button { text: "Use profile default"; visible: screenshots.destinationOverridden; onClicked: app.useProfileScreenshotDestination() }
                ToolButton { text: "↗"; onClicked: app.openScreenshotDestination(); ToolTip.visible: hovered; ToolTip.text: "Open destination" }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Label { text: "Saved screenshots"; color: settings.palette["#f1eff2"]; font.pixelSize: 16; font.weight: Font.DemiBold; Layout.fillWidth: true }
            Label { text: screenshots.gallery.count + " items"; color: settings.palette["#8f909b"]; font.pixelSize: 11 }
            ToolButton { text: "↻"; onClicked: screenshots.gallery.refresh(); ToolTip.visible: hovered; ToolTip.text: "Refresh gallery" }
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            GridView {
                id: galleryGrid
                anchors.fill: parent
                clip: true
                cellWidth: 280
                cellHeight: 306
                model: screenshots.gallery
                activeFocusOnTab: true
                keyNavigationEnabled: true
                Keys.onReturnPressed: if (currentItem && !currentItem.missing) root.preview(currentItem.path, currentItem.imageUrl)
                Keys.onSpacePressed: if (currentItem && !currentItem.missing) root.preview(currentItem.path, currentItem.imageUrl)
                delegate: Rectangle {
                    required property int index
                    required property string fileName
                    required property string path
                    required property url imageUrl
                    required property url thumbnailUrl
                    required property string modified
                    required property int pixelWidth
                    required property int pixelHeight
                    required property double sizeBytes
                    required property bool missing
                    width: 264
                    height: 292
                    radius: 13
                    color: settings.palette["#191a20"]
                    border.color: GridView.isCurrentItem && galleryGrid.activeFocus ? settings.palette["#e96782"] : imageMouse.containsMouse ? settings.palette["#555660"] : settings.palette["#2d2e37"]
                    clip: true
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 9
                        spacing: 7
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 128
                            radius: 9
                            color: settings.palette["#24252c"]
                            clip: true
                            Image {
                                anchors.fill: parent
                                source: missing ? "" : thumbnailUrl
                                sourceSize: Qt.size(200, 128)
                                fillMode: Image.PreserveAspectFit
                                asynchronous: true
                                cache: true
                            }
                            Rectangle {
                                anchors.fill: parent
                                visible: missing
                                color: settings.palette["#24252c"]
                                Label { anchors.centerIn: parent; text: "File is missing"; color: settings.palette["#a5a6b0"]; font.pixelSize: 10 }
                            }
                            MouseArea {
                                id: imageMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: { galleryGrid.currentIndex = index; galleryGrid.forceActiveFocus() }
                                onDoubleClicked: {
                                    if (!missing) {
                                        root.preview(path, imageUrl)
                                    }
                                }
                            }
                        }
                        Label { text: fileName; color: settings.palette["#eeedf0"]; font.pixelSize: 10; font.weight: Font.Medium; elide: Text.ElideMiddle; Layout.fillWidth: true }
                        Label { text: modified + "  ·  " + pixelWidth + " × " + pixelHeight + "  ·  " + (sizeBytes < 1048576 ? Math.round(sizeBytes / 1024) + " KB" : (sizeBytes / 1048576).toFixed(1) + " MB"); color: settings.palette["#898a95"]; font.pixelSize: 9; elide: Text.ElideRight; Layout.fillWidth: true }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 3
                            ToolButton { text: "Preview"; enabled: !missing; onClicked: root.preview(path, imageUrl); font.pixelSize: 9; Layout.fillWidth: true }
                            ToolButton { text: "Open"; enabled: !missing; onClicked: screenshots.openImage(path); font.pixelSize: 9; Layout.fillWidth: true }
                            ToolButton { text: "Folder"; enabled: !missing; onClicked: screenshots.openContainingFolder(path); font.pixelSize: 9; Layout.fillWidth: true }
                            ToolButton { text: "Image"; enabled: !missing; onClicked: screenshots.copyImageToClipboard(path); font.pixelSize: 9; Layout.fillWidth: true }
                            ToolButton { text: "Path"; onClicked: screenshots.copyPath(path); font.pixelSize: 9; Layout.fillWidth: true }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 3
                            ToolButton {
                                text: "Rename"
                                enabled: !missing
                                font.pixelSize: 9
                                Layout.fillWidth: true
                                onClicked: { root.selectedPath = path; renameField.text = fileName.toLowerCase().endsWith(".png") ? fileName.slice(0, -4) : fileName; renameDialog.open() }
                            }
                            ToolButton {
                                text: "Remove"
                                font.pixelSize: 9
                                Layout.fillWidth: true
                                onClicked: { root.selectedPath = path; removeDialog.open() }
                            }
                            ToolButton {
                                text: "Delete"
                                enabled: !missing
                                font.pixelSize: 9
                                Layout.fillWidth: true
                                onClicked: { root.selectedPath = path; deleteDialog.open() }
                            }
                        }
                    }
                }
            }

            Column {
                anchors.centerIn: parent
                spacing: 8
                visible: screenshots.gallery.count === 0
                Label { anchors.horizontalCenter: parent.horizontalCenter; text: "▧"; color: settings.palette["#555660"]; font.pixelSize: 34 }
                Label { anchors.horizontalCenter: parent.horizontalCenter; text: "Nothing saved in this destination"; color: settings.palette["#d7d6dc"]; font.pixelSize: 14; font.weight: Font.DemiBold }
                Label { anchors.horizontalCenter: parent.horizontalCenter; text: "Use Paste Screenshot or press Ctrl + V while this page is open."; color: settings.palette["#858692"]; font.pixelSize: 11 }
            }
        }
    }

    Dialog {
        id: previewDialog
        modal: true
        title: "Screenshot preview"
        width: 900
        height: 620
        anchors.centerIn: Overlay.overlay
        standardButtons: Dialog.Close
        background: Rectangle { color: settings.palette["#17181d"]; border.color: settings.palette["#383942"]; radius: 14 }
        contentItem: Item {
            Image { id: previewImage; anchors.fill: parent; source: root.selectedImage; sourceSize: Qt.size(1920, 1080); fillMode: Image.PreserveAspectFit; asynchronous: true; cache: false }
            Label { anchors.centerIn: parent; visible: previewImage.status === Image.Error; text: "Preview unavailable. The image may be missing or exceed the image limit."; wrapMode: Text.WordWrap }
        }
    }

    Dialog {
        id: renameDialog
        width: 390
        objectName: "screenshotRenameDialog"
        title: "Rename screenshot"
        modal: true
        standardButtons: Dialog.Ok | Dialog.Cancel
        anchors.centerIn: Overlay.overlay
        onAccepted: screenshots.renameScreenshot(root.selectedPath, renameField.text)
        onOpened: renameField.forceActiveFocus()
        contentItem: TextField { id: renameField; objectName: "screenshotRenameField"; width: 340; selectByMouse: true; placeholderText: "Screenshot name"; onAccepted: renameDialog.accept() }
    }
    Dialog {
        id: removeDialog
        width: 400
        title: "Remove from gallery?"
        modal: true
        standardButtons: Dialog.Yes | Dialog.No
        anchors.centerIn: Overlay.overlay
        onAccepted: screenshots.removeFromCollection(root.selectedPath)
        contentItem: Label { text: "This removes the screenshot from the application gallery. The file stays on disk."; wrapMode: Text.WordWrap; color: settings.palette["#c8c7cf"]; width: 350; padding: 12 }
    }
    Dialog {
        id: deleteDialog
        width: 400
        title: "Delete screenshot file?"
        modal: true
        standardButtons: Dialog.Yes | Dialog.No
        anchors.centerIn: Overlay.overlay
        onAccepted: screenshots.deleteScreenshotFile(root.selectedPath)
        contentItem: Label { text: "Permanently delete this screenshot file? This cannot be undone."; wrapMode: Text.WordWrap; color: settings.palette["#f1c1c8"]; width: 350; padding: 12 }
    }
}
