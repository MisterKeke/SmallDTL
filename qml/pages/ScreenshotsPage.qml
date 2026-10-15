import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window

Item {
    id: root
    property string selectedPath: ""
    property url selectedImage: ""
    property var selectedIds: []
    property int lastClickedIndex: -1
    function isSelected(id) { return selectedIds.indexOf(id) >= 0 }
    function toggleSelected(id) {
        const next = selectedIds.slice()
        const found = next.indexOf(id)
        if (found >= 0) next.splice(found, 1)
        else next.push(id)
        selectedIds = next
    }
    function clearSelection() { selectedIds = []; lastClickedIndex = -1 }
    function selectAll() {
        const ids = []
        for (let i = 0; i < screenshots.gallery.count; ++i) ids.push(screenshots.gallery.idAt(i))
        selectedIds = ids.filter(id => id.length > 0)
    }
    function selectFromClick(id, index, modifiers) {
        const control = (modifiers & Qt.ControlModifier) !== 0
        const shift = (modifiers & Qt.ShiftModifier) !== 0
        let next = selectedIds.slice()
        if (shift) {
            const start = lastClickedIndex < 0 ? index : lastClickedIndex
            const low = Math.min(start, index), high = Math.max(start, index)
            if (!control) next = []
            for (let i = low; i <= high; ++i) {
                const rangeId = screenshots.gallery.idAt(i)
                if (rangeId.length && next.indexOf(rangeId) < 0) next.push(rangeId)
            }
        } else if (control) {
            const found = next.indexOf(id)
            if (found >= 0) next.splice(found, 1)
            else next.push(id)
        } else {
            next = [id]
        }
        selectedIds = next
        lastClickedIndex = index
    }
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

    Connections {
        target: screenshots.gallery
        function onModelReset() {
            const visibleIds = []
            for (let i = 0; i < screenshots.gallery.count; ++i)
                visibleIds.push(screenshots.gallery.idAt(i))
            root.selectedIds = root.selectedIds.filter(id => visibleIds.indexOf(id) >= 0)
        }
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

        RowLayout {
            Layout.fillWidth: true
            ComboBox {
                id: tagFilter
                Layout.preferredWidth: 205
                textRole: "name"
                valueRole: "tagId"
                model: [{ tagId: "", name: "All screenshots" }].concat(screenshots.tags)
                currentIndex: {
                    for (let i = 0; i < model.length; ++i)
                        if (model[i].tagId === screenshots.galleryTagFilter) return i
                    return 0
                }
                onActivated: {
                    root.clearSelection()
                    screenshots.setGalleryTagFilter(currentValue)
                }
            }
            Button { text: "Add tag"; onClicked: { createTagField.text = ""; tagCreateDialog.open() } }
            Button { text: "Rename tag"; enabled: tagFilter.currentValue !== ""; onClicked: { renameTagField.text = tagFilter.currentText; tagRenameDialog.open() } }
            Button { text: "Delete tag"; enabled: tagFilter.currentValue !== ""; onClicked: tagDeleteDialog.open() }
            Item { Layout.fillWidth: true }
            Label { text: root.selectedIds.length + " selected"; color: settings.palette["#b0b1bb"]; font.pixelSize: 10 }
            Button { text: "Select all"; onClicked: root.selectAll() }
            Button { text: "Clear"; enabled: root.selectedIds.length > 0; onClicked: root.clearSelection() }
            Button { id: bulkTagButton; text: "Tag selected"; enabled: root.selectedIds.length > 0; onClicked: bulkTagPopup.open() }
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
                    required property string screenshotId
                    required property string fileName
                    required property string path
                    required property url imageUrl
                    required property url thumbnailUrl
                    required property string modified
                    required property int pixelWidth
                    required property int pixelHeight
                    required property double sizeBytes
                    required property bool missing
                    required property var tags
                    width: 264
                    height: 292
                    radius: 13
                    color: settings.palette["#191a20"]
                    border.color: root.isSelected(screenshotId) ? settings.palette["#e96782"] : GridView.isCurrentItem && galleryGrid.activeFocus ? settings.palette["#e96782"] : imageMouse.containsMouse ? settings.palette["#555660"] : settings.palette["#2d2e37"]
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
                                onClicked: function(mouse) { galleryGrid.currentIndex = index; root.selectFromClick(screenshotId, index, mouse.modifiers); galleryGrid.forceActiveFocus() }
                                onDoubleClicked: {
                                    if (!missing) {
                                        root.preview(path, imageUrl)
                                    }
                                }
                            }
                            CheckBox {
                                anchors.left: parent.left
                                anchors.top: parent.top
                                checked: root.isSelected(screenshotId)
                                onClicked: { root.toggleSelected(screenshotId); root.lastClickedIndex = index }
                                Accessible.name: "Select " + fileName
                                background: Rectangle { radius: 5; color: "#aa191a20" }
                            }
                        }
                        Label { text: fileName; color: settings.palette["#eeedf0"]; font.pixelSize: 10; font.weight: Font.Medium; elide: Text.ElideMiddle; Layout.fillWidth: true }
                        Label { text: modified + "  ·  " + pixelWidth + " × " + pixelHeight + "  ·  " + (sizeBytes < 1048576 ? Math.round(sizeBytes / 1024) + " KB" : (sizeBytes / 1048576).toFixed(1) + " MB"); color: settings.palette["#898a95"]; font.pixelSize: 9; elide: Text.ElideRight; Layout.fillWidth: true }
                        Label { text: tags.join("  ·  "); visible: tags.length > 0; color: settings.palette["#e9a7b4"]; font.pixelSize: 9; elide: Text.ElideRight; Layout.fillWidth: true }
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

    Popup {
        id: bulkTagPopup
        parent: Overlay.overlay
        x: bulkTagButton.mapToItem(Overlay.overlay, 0, bulkTagButton.height).x
        y: bulkTagButton.mapToItem(Overlay.overlay, 0, bulkTagButton.height).y
        width: 260
        height: Math.min(340, tagOptions.implicitHeight + 20)
        padding: 10
        background: Rectangle { color: settings.palette["#191a20"]; border.color: settings.palette["#383942"]; radius: 10 }
        contentItem: Flickable {
            contentWidth: width
            contentHeight: tagOptions.implicitHeight
            clip: true
            Column {
                id: tagOptions
                width: parent.width
                Repeater {
                    model: screenshots.tags
                    delegate: RowLayout {
                        required property string tagId
                        required property string name
                        width: tagOptions.width
                        CheckBox {
                            Layout.fillWidth: true
                            text: name
                            checkState: {
                                const state = screenshots.tagAssignmentState(tagId, root.selectedIds)
                                return state === 2 ? Qt.PartiallyChecked : state === 1 ? Qt.Checked : Qt.Unchecked
                            }
                            onClicked: {
                                const previousState = screenshots.tagAssignmentState(tagId, root.selectedIds)
                                screenshots.setTagForScreenshots(root.selectedIds, tagId, previousState !== 1)
                            }
                        }
                        ToolButton {
                            text: "Remove"
                            enabled: screenshots.tagAssignmentState(tagId, root.selectedIds) > 0
                            onClicked: screenshots.setTagForScreenshots(root.selectedIds, tagId, false)
                        }
                    }
                }
                Label { visible: screenshots.tags.length === 0; text: "Create a tag first"; color: settings.palette["#9b9ca7"]; padding: 8 }
            }
        }
    }

    Dialog {
        id: tagCreateDialog
        width: 390
        title: "Create tag"
        modal: true
        standardButtons: Dialog.Ok | Dialog.Cancel
        anchors.centerIn: Overlay.overlay
        onAccepted: screenshots.createTag(createTagField.text)
        contentItem: TextField { id: createTagField; width: 320; placeholderText: "Tag name"; selectByMouse: true; onAccepted: tagCreateDialog.accept() }
    }
    Dialog {
        id: tagRenameDialog
        width: 390
        title: "Rename tag"
        modal: true
        standardButtons: Dialog.Ok | Dialog.Cancel
        anchors.centerIn: Overlay.overlay
        onAccepted: screenshots.renameTag(tagFilter.currentValue, renameTagField.text)
        contentItem: TextField { id: renameTagField; width: 320; placeholderText: "Tag name"; selectByMouse: true; onAccepted: tagRenameDialog.accept() }
    }
    Dialog {
        id: tagDeleteDialog
        width: 400
        title: "Delete tag?"
        modal: true
        standardButtons: Dialog.Yes | Dialog.No
        anchors.centerIn: Overlay.overlay
        onAccepted: screenshots.deleteTag(tagFilter.currentValue)
        contentItem: Label { text: "Delete this tag and its screenshot assignments? Screenshot files will stay on disk."; wrapMode: Text.WordWrap; color: settings.palette["#c8c7cf"]; width: 350; padding: 12 }
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
