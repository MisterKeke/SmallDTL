import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

Item {
    id: root
    objectName: "fileShelfPage"
    property bool compact: false
    property var transferPlan: []
    property string transferDestination: ""
    property bool transferMove: false
    property string operationDetails: ""
    property string selectedCollectionForMembership: ""
    property string editingCollectionId: ""

    function handleDroppedUrls(urls) {
        return fileShelf.addFileUrls(urls)
    }

    Connections {
        target: app
        function onCurrentPageChanged() {
            if (app.currentPage === "File Shelf") fileShelf.refresh()
        }
    }
    Connections {
        target: fileShelf
        function onOperationFinished(succeeded, skipped, failed, details) {
            if (failed > 0 || details.length > 0) {
                root.operationDetails = details.join("\n")
                operationResultDialog.open()
            }
        }
    }

    function formatSize(bytes) {
        if (bytes < 1024) return bytes + " B"
        if (bytes < 1024 * 1024) return (bytes / 1024).toFixed(1) + " KiB"
        if (bytes < 1024 * 1024 * 1024) return (bytes / (1024 * 1024)).toFixed(1) + " MiB"
        return (bytes / (1024 * 1024 * 1024)).toFixed(1) + " GiB"
    }

    function startOperation(move) {
        if (fileShelf.selectedCount === 0 || fileShelf.busy)
            return
        var destination = fileShelf.chooseTransferDestination()
        if (!destination)
            return
        transferDestination = destination
        transferMove = move
        transferPlan = fileShelf.previewTransfer(destination, move)
        if (!transferPlan || transferPlan.length === 0)
            return
        var hasConflicts = false
        for (var i = 0; i < transferPlan.length; ++i)
            if (transferPlan[i].conflict) hasConflicts = true
        if (hasConflicts)
            conflictDialog.open()
        else
            fileShelf.startTransfer(transferPlan, transferDestination, transferMove)
    }

    DropArea {
        anchors.fill: parent
        z: 5
        keys: ["text/uri-list"]
        onDropped: function(drop) {
            if (drop.hasUrls && root.handleDroppedUrls(drop.urls))
                drop.acceptProposedAction()
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 12

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Button { text: "Add files"; onClicked: fileShelf.addFilesDialog() }
            Button { text: "Add folder"; onClicked: fileShelf.addFolderDialog(); visible: !root.compact }
            ToolButton { text: "Refresh"; onClicked: fileShelf.refresh() }
            Item { Layout.fillWidth: true }
            ToolButton {
                text: "Select all"
                enabled: fileShelf.count > 0
                onClicked: fileShelf.selectAll()
            }
            ToolButton {
                text: "Clear selection"
                enabled: fileShelf.selectedCount > 0
                onClicked: fileShelf.clearSelection()
            }
            ToolButton {
                text: "Clear shelf"
                enabled: fileShelf.count > 0 && !fileShelf.busy
                onClicked: clearDialog.open()
            }
        }

        RowLayout {
            Layout.fillWidth: true
            TextField {
                objectName: "fileShelfSearch"
                Layout.fillWidth: true
                placeholderText: "Search file name or path"
                text: fileShelf.searchText
                onTextEdited: fileShelf.setSearchText(text)
            }
            Button { text: "All items"; checkable: true; checked: !fileShelf.favoritesOnly && !fileShelf.collectionFilter; onClicked: fileShelf.setCollectionFilter("") }
            Button { text: "Favorites"; checkable: true; checked: fileShelf.favoritesOnly; onClicked: fileShelf.setFavoritesOnly(!fileShelf.favoritesOnly) }
            Button { text: "New collection"; onClicked: { root.editingCollectionId = ""; collectionNameField.text = ""; collectionNameDialog.open() } }
        }

        Flow {
            Layout.fillWidth: true
            spacing: 5
            Repeater {
                model: fileShelf.collections
                delegate: RowLayout {
                    required property var modelData
                    spacing: 0
                    Button {
                        text: modelData.name
                        checkable: true
                        checked: fileShelf.collectionFilter === modelData.id
                        onClicked: { fileShelf.setCollectionFilter(modelData.id); root.selectedCollectionForMembership = modelData.id }
                    }
                    ToolButton {
                        text: modelData.favorite ? "★" : "☆"
                        Accessible.name: modelData.favorite ? "Unfavorite collection" : "Favorite collection"
                        onClicked: fileShelf.setCollectionFavorite(modelData.id, !modelData.favorite)
                    }
                    ToolButton {
                        text: "✎"
                        Accessible.name: "Rename " + modelData.name
                        onClicked: { root.editingCollectionId = modelData.id; collectionNameField.text = modelData.name; collectionNameDialog.open() }
                    }
                    ToolButton {
                        text: "×"
                        Accessible.name: "Delete " + modelData.name
                        onClicked: { root.editingCollectionId = modelData.id; deleteCollectionDialog.open() }
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            ComboBox {
                id: membershipPicker
                objectName: "fileShelfMembershipPicker"
                Layout.preferredWidth: 220
                model: fileShelf.collections
                textRole: "name"
                valueRole: "id"
                currentIndex: {
                    for (var i = 0; i < fileShelf.collections.length; ++i)
                        if (fileShelf.collections[i].id === root.selectedCollectionForMembership) return i
                    return fileShelf.collections.length > 0 ? 0 : -1
                }
                onActivated: root.selectedCollectionForMembership = currentValue
            }
            Button { text: "Add selected to collection"; enabled: fileShelf.selectedCount > 0 && membershipPicker.currentValue; onClicked: fileShelf.setSelectedCollectionMembership(membershipPicker.currentValue, true) }
            Button { text: "Remove selected"; visible: fileShelf.collectionFilter !== ""; enabled: fileShelf.selectedCount > 0 && fileShelf.collectionFilter !== ""; onClicked: fileShelf.setSelectedCollectionMembership(fileShelf.collectionFilter, false) }
            Item { Layout.fillWidth: true }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 7
            Button { text: "Copy to folder…"; visible: !root.compact; enabled: fileShelf.selectedCount > 0 && !fileShelf.busy; onClicked: root.startOperation(false) }
            Button { text: "Move to folder…"; visible: !root.compact; enabled: fileShelf.selectedCount > 0 && !fileShelf.busy; onClicked: root.startOperation(true) }
            Button { text: "Copy paths"; enabled: fileShelf.selectedCount > 0; onClicked: fileShelf.copySelectedPaths() }
            Button { text: "Drag selected"; enabled: fileShelf.selectedCount > 0; onClicked: fileShelf.beginExternalDrag(fileShelf.selectedIds) }
            Button { text: "Remove references"; enabled: fileShelf.selectedCount > 0 && !fileShelf.busy; onClicked: removeSelectedDialog.open() }
            Item { Layout.fillWidth: true }
            Label { text: fileShelf.selectedCount + " selected"; color: settings.palette["#9697a3"]; font.pixelSize: 11 }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: fileShelf.message.length > 0 ? (root.compact ? 44 : 48) : 0
            visible: fileShelf.message.length > 0
            radius: 9
            color: fileShelf.messageIsError ? settings.palette["#3b2229"] : settings.palette["#1e332b"]
            border.color: fileShelf.messageIsError ? settings.palette["#6b3642"] : settings.palette["#315542"]
            Label {
                anchors.fill: parent
                anchors.margins: 8
                text: fileShelf.message
                color: fileShelf.messageIsError ? settings.palette["#ff8d9c"] : settings.palette["#74d3aa"]
                font.pixelSize: 10
                wrapMode: Text.WrapAnywhere
                elide: Text.ElideRight
                verticalAlignment: Text.AlignVCenter
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: 14
            color: settings.palette["#191a20"]
            border.color: settings.palette["#2d2e37"]
            DropArea {
                anchors.fill: parent
                z: 5
                keys: ["text/uri-list"]
                onDropped: function(drop) {
                    if (drop.hasUrls && root.handleDroppedUrls(drop.urls))
                        drop.acceptProposedAction()
                }
            }
            ListView {
                id: shelfList
                objectName: "fileShelfList"
                anchors.fill: parent
                anchors.margins: 10
                clip: true
                model: fileShelf
                spacing: 6
                delegate: Rectangle {
                    id: shelfRow
                    required property string entryId
                    required property string path
                    required property string displayName
                    required property string sourceFolder
                    required property string itemType
                    required property string fileType
                    required property double sizeBytes
                    required property bool available
                    required property bool favorite
                    required property string collectionNames
                    width: ListView.view.width
                    height: root.compact ? 82 : 92
                    radius: 10
                    color: fileShelf.selectedIds.indexOf(entryId) >= 0 ? settings.palette["#39222b"] : settings.palette["#202127"]
                    border.color: fileShelf.selectedIds.indexOf(entryId) >= 0 ? settings.palette["#8e4359"] : settings.palette["#30313a"]

                    MouseArea {
                        id: rowMouse
                        anchors.fill: parent
                        z: 0
                        preventStealing: true
                        property point pressPoint
                        property bool dragStarted: false
                        onPressed: function(mouse) {
                            pressPoint = Qt.point(mouse.x, mouse.y)
                            dragStarted = false
                            fileShelf.clickItem(shelfRow.entryId,
                                                (mouse.modifiers & Qt.ControlModifier) !== 0,
                                                (mouse.modifiers & Qt.ShiftModifier) !== 0)
                        }
                        onPositionChanged: function(mouse) {
                            if (!pressed || dragStarted) return
                            if (Math.abs(mouse.x - pressPoint.x) + Math.abs(mouse.y - pressPoint.y) < 14) return
                            dragStarted = true
                            var ids = fileShelf.selectedIds.indexOf(shelfRow.entryId) >= 0
                                      ? fileShelf.selectedIds : [shelfRow.entryId]
                            fileShelf.beginExternalDrag(ids)
                        }
                        cursorShape: Qt.OpenHandCursor
                    }

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 10
                        anchors.rightMargin: 7
                        spacing: 8
                        z: 1
                        CheckBox {
                            checked: fileShelf.selectedIds.indexOf(shelfRow.entryId) >= 0
                            Accessible.name: "Select " + shelfRow.displayName
                            onClicked: fileShelf.toggleSelection(shelfRow.entryId)
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            Label {
                                Layout.fillWidth: true
                                text: shelfRow.displayName
                                color: shelfRow.available ? settings.palette["#f2f1f4"] : settings.palette["#ff8d9c"]
                                font.pixelSize: 12
                                font.weight: Font.DemiBold
                                elide: Text.ElideMiddle
                            }
                            Label {
                                Layout.fillWidth: true
                                text: shelfRow.fileType
                                      + "  ·  " + (shelfRow.itemType === "folder" ? "Size unavailable" : root.formatSize(shelfRow.sizeBytes))
                                      + "  ·  " + (shelfRow.available ? "Available" : "Missing")
                                color: shelfRow.available ? settings.palette["#a5a6b0"] : settings.palette["#ff8d9c"]
                                font.pixelSize: 9
                                elide: Text.ElideRight
                            }
                            Label {
                                Layout.fillWidth: true
                                text: shelfRow.sourceFolder
                                color: settings.palette["#777984"]
                                font.pixelSize: 9
                                elide: Text.ElideMiddle
                            }
                            Label {
                                Layout.fillWidth: true
                                text: shelfRow.collectionNames.length > 0 ? shelfRow.collectionNames : "No collection"
                                color: settings.palette["#e7a0b1"]
                                font.pixelSize: 9
                                elide: Text.ElideRight
                            }
                        }
                        ToolButton { text: shelfRow.favorite ? "★" : "☆"; Accessible.name: shelfRow.favorite ? "Remove from favorites" : "Add to favorites"; onClicked: fileShelf.setEntryFavorite(shelfRow.entryId, !shelfRow.favorite) }
                        ToolButton { text: "Open"; enabled: shelfRow.available; onClicked: fileShelf.openEntry(shelfRow.entryId) }
                        ToolButton { text: "Folder"; onClicked: fileShelf.openLocation(shelfRow.entryId) }
                        ToolButton { text: "Locate"; visible: !shelfRow.available; Accessible.name: "Locate moved file or folder"; onClicked: fileShelf.locateEntryReplacement(shelfRow.entryId) }
                        ToolButton { text: "Path"; onClicked: fileShelf.copyPath(shelfRow.entryId) }
                        ToolButton { text: "×"; Accessible.name: "Remove reference"; onClicked: fileShelf.removeEntry(shelfRow.entryId) }
                    }
                }
                Label {
                    anchors.centerIn: parent
                    visible: fileShelf.count === 0
                    text: fileShelf.searchText.length > 0 || fileShelf.collectionFilter.length > 0 || fileShelf.favoritesOnly
                          ? "No items match this search or filter"
                          : "Drop files or folders here, or add references above"
                    color: settings.palette["#9697a3"]
                    font.pixelSize: 12
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            visible: fileShelf.busy
            ProgressBar {
                Layout.fillWidth: true
                from: 0; to: Math.max(1, fileShelf.totalBytes)
                value: fileShelf.bytesCopied
                indeterminate: fileShelf.totalBytes <= 0
            }
            Label { text: root.formatSize(fileShelf.bytesCopied) + " / " + root.formatSize(fileShelf.totalBytes) + " · " + fileShelf.completedItems + "/" + fileShelf.totalItems }
            Button { text: fileShelf.cancellationRequested ? "Cancelling…" : "Cancel"; enabled: !fileShelf.cancellationRequested; onClicked: fileShelf.cancelTransfer() }
        }
    }

    Dialog {
        id: collectionNameDialog
        title: root.editingCollectionId.length ? "Rename collection" : "Create collection"
        modal: true
        anchors.centerIn: parent
        standardButtons: Dialog.Cancel | Dialog.Ok
        contentItem: TextField {
            id: collectionNameField
            objectName: "fileShelfCollectionName"
            placeholderText: "Collection name"
            onAccepted: collectionNameDialog.accept()
        }
        onAccepted: {
            if (root.editingCollectionId.length)
                fileShelf.renameCollection(root.editingCollectionId, collectionNameField.text)
            else {
                var id = fileShelf.createCollection(collectionNameField.text)
                if (id) { root.selectedCollectionForMembership = id; fileShelf.setCollectionFilter(id) }
            }
        }
    }

    Dialog {
        id: deleteCollectionDialog
        title: "Delete collection?"
        modal: true
        anchors.centerIn: parent
        standardButtons: Dialog.Cancel | Dialog.Ok
        Label { text: "Delete this collection? Shelf references, favorites, and source files remain."; wrapMode: Text.WordWrap }
        onAccepted: fileShelf.deleteCollection(root.editingCollectionId)
    }

    Dialog {
        id: conflictDialog
        title: root.transferMove ? "Resolve move conflicts" : "Resolve copy conflicts"
        modal: true
        anchors.centerIn: parent
        width: Math.min(parent.width - 24, 650)
        standardButtons: Dialog.Cancel | Dialog.Ok
        onAccepted: fileShelf.startTransfer(root.transferPlan, root.transferDestination, root.transferMove)
        contentItem: ColumnLayout {
            spacing: 8
            Label { Layout.fillWidth: true; text: "These destination names already exist. Choose what to do for each item."; wrapMode: Text.WordWrap }
            Repeater {
                model: root.transferPlan
                delegate: RowLayout {
                    required property var modelData
                    required property int index
                    visible: modelData.conflict
                    Layout.fillWidth: true
                    Label { Layout.fillWidth: true; text: modelData.name + "  →  " + root.transferDestination; elide: Text.ElideMiddle }
                    ComboBox {
                        id: resolutionPicker
                        model: ["Skip", "Rename", "Replace"]
                        currentIndex: modelData.resolution === "rename" ? 1 : modelData.resolution === "replace" ? 2 : 0
                        onActivated: function(choiceIndex) {
                            var updated = root.transferPlan.slice()
                            var entry = Object.assign({}, updated[index])
                            entry.resolution = ["skip", "rename", "replace"][choiceIndex]
                            updated[index] = entry
                            root.transferPlan = updated
                        }
                    }
                }
            }
            Label { Layout.fillWidth: true; text: "Replacement is explicit and applies only to destination contents. Shelf references and source files stay independent."; color: settings.palette["#9697a3"]; font.pixelSize: 10; wrapMode: Text.WordWrap }
        }
    }

    Dialog {
        id: operationResultDialog
        title: "File operation details"
        modal: true
        anchors.centerIn: parent
        width: Math.max(360, Math.min(root.width - 32, 700))
        height: Math.max(260, Math.min(root.height - 60, 440))
        standardButtons: Dialog.Close
        contentItem: TextArea {
            width: operationResultDialog.width - 48
            height: operationResultDialog.height - 110
            text: root.operationDetails
            readOnly: true
            wrapMode: TextEdit.WrapAnywhere
            selectByMouse: true
            background: Rectangle { color: settings.palette["#191a20"]; border.color: settings.palette["#2d2e37"] }
        }
    }

    Dialog {
        id: removeSelectedDialog
        title: "Remove selected references?"
        modal: true
        anchors.centerIn: parent
        standardButtons: Dialog.Cancel | Dialog.Ok
        Label { text: "This removes " + fileShelf.selectedCount + " shelf entries. The referenced source files will stay in place."; wrapMode: Text.WordWrap }
        onAccepted: fileShelf.removeSelected()
    }

    Dialog {
        id: clearDialog
        title: "Clear file shelf?"
        modal: true
        anchors.centerIn: parent
        standardButtons: Dialog.Cancel | Dialog.Ok
        Label { text: "Remove every shelf reference? Source files and folders will stay in place."; wrapMode: Text.WordWrap }
        onAccepted: fileShelf.clearShelf()
    }
}
