import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"

Item {
    id: root
    property string selectedTagId: ""
    HistoryClearDialog { id: clearHistoryDialog; objectName: "pageHistoryClearDialog"; parent: Overlay.overlay }

    ColumnLayout {
        anchors.fill: parent
        spacing: 12

        RowLayout {
            Layout.fillWidth: true
            TextField {
                objectName: "clipboardHistorySearch"
                Layout.fillWidth: true
                placeholderText: "Search text and file names"
                text: clipboardHistory.search
                onTextEdited: clipboardHistory.setSearch(text)
            }
            Button { text: clipboardHistory.paused ? "Resume history" : "Pause history"; onClicked: clipboardHistory.setPaused(!clipboardHistory.paused) }
            Button { objectName: "clearHistoryButton"; text: "Clear history"; enabled: clipboardHistory.totalCount > 0; onClicked: clearHistoryDialog.open() }
        }

        Label {
            Layout.fillWidth: true
            text: "Clipboard items stay local. Restoring copies content to the clipboard; it does not type or paste into another app."
            color: settings.palette["#9697a3"]
            font.pixelSize: 11
            wrapMode: Text.WordWrap
        }

        ListView {
            objectName: "clipboardHistoryList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 8
            model: clipboardHistory.model
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
            delegate: Rectangle {
                id: entry
                objectName: "clipboardHistoryEntry"
                required property string entryId
                required property string contentType
                required property string capturedAt
                required property string preview
                required property url imageUrl
                required property int fileCount
                required property bool pinned
                required property bool missing
                required property double sizeBytes
                width: ListView.view.width
                implicitHeight: Math.max(112, historyActions.implicitHeight + 22,
                                         historyDetails.implicitHeight + 22,
                                         historySaveActions.visible ? historySaveActions.implicitHeight + 22 : 0,
                                         contentType === "image" ? 130 : 0)
                radius: settings.retroArcade ? settings.palette["panelRadius"] : 12
                color: rowMouse.containsMouse ? settings.palette["#22232a"] : settings.palette["#191a20"]
                border.color: settings.palette["#2d2e37"]

                MouseArea {
                    id: rowMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: clipboardHistory.restoreEntry(entry.entryId)
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 11
                    spacing: 12

                    Image {
                        visible: entry.contentType === "image"
                        Layout.preferredWidth: 116
                        Layout.preferredHeight: 108
                        fillMode: Image.PreserveAspectFit
                        source: entry.imageUrl
                        sourceSize: Qt.size(232, 216)
                        asynchronous: true
                        cache: false
                        Rectangle { anchors.fill: parent; color: "transparent"; border.color: settings.palette["#383942"]; visible: entry.contentType === "image" }
                        Label { anchors.centerIn: parent; visible: entry.missing; text: "Missing"; color: settings.palette["#ff8d9c"]; font.pixelSize: 10 }
                    }

                    ColumnLayout {
                        id: historyDetails
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        spacing: 6
                        RowLayout {
                            Layout.fillWidth: true
                            Label { text: entry.contentType === "files" ? "Files · " + entry.fileCount : entry.contentType === "image" ? "Image" : "Text"; color: settings.palette["#e7a0b1"]; font.pixelSize: 11; font.weight: Font.DemiBold }
                            Label { Layout.fillWidth: true; text: entry.pinned ? "Pinned" : ""; color: settings.palette["#e7b879"]; font.pixelSize: 10; horizontalAlignment: Text.AlignRight }
                        }
                        Label {
                            objectName: "clipboardHistoryPreview"
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            text: entry.missing ? (entry.contentType === "files" ? "Some referenced files are missing" : "History image payload is missing") : entry.preview
                            color: entry.missing ? settings.palette["#ff8d9c"] : settings.palette["#dedde3"]
                            font.pixelSize: 11
                            wrapMode: Text.WrapAnywhere
                            maximumLineCount: 3
                            elide: Text.ElideRight
                        }
                        Label { text: entry.capturedAt; color: settings.palette["#898a95"]; font.pixelSize: 9 }
                    }

                    ColumnLayout {
                        id: historyActions
                        objectName: "clipboardHistoryActions"
                        spacing: 2
                        Button { text: entry.pinned ? "Unpin" : "Pin"; onClicked: clipboardHistory.setPinned(entry.entryId, !entry.pinned) }
                        Button { text: "Copy"; onClicked: clipboardHistory.restoreEntry(entry.entryId) }
                        Button { text: "Delete"; onClicked: clipboardHistory.deleteEntry(entry.entryId) }
                    }

                    ColumnLayout {
                        id: historySaveActions
                        visible: entry.contentType === "image"
                        spacing: 4
                        ComboBox {
                            objectName: "historySaveTagPicker"
                            Layout.preferredWidth: 145
                            model: [{ tagId: "", name: "No tag" }].concat(screenshots.tags)
                            textRole: "name"
                            valueRole: "tagId"
                            currentIndex: Math.max(0, indexOfValue(root.selectedTagId))
                            onActivated: root.selectedTagId = currentValue
                        }
                        Button { text: "Save to gallery"; onClicked: clipboardHistory.saveToGallery(entry.entryId, root.selectedTagId) }
                    }
                }
            }
            Label {
                anchors.centerIn: parent
                visible: clipboardHistory.count === 0
                text: clipboardHistory.paused ? "History is paused" : "No clipboard items yet"
                color: settings.palette["#9697a3"]
                font.pixelSize: 12
            }
        }
        RowLayout {
            Layout.fillWidth: true
            BusyIndicator { running: clipboardHistory.loading; visible: running; implicitWidth: 24; implicitHeight: 24 }
            Label { text: clipboardHistory.count + " shown · " + clipboardHistory.totalCount + " total" }
            Button { text: "Load more"; visible: clipboardHistory.hasMore; enabled: !clipboardHistory.loading; onClicked: clipboardHistory.loadMore() }
        }
    }
}
