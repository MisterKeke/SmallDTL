import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"

Item {
    id: root
    objectName: "qrCodePage"

    function handleDroppedUrls(urls) {
        return urls && urls.length > 0 ? qrCode.decodeFileUrl(urls[0]) : false
    }

    ScrollView {
        id: qrScroll
        anchors.fill: parent
        clip: true
        contentWidth: availableWidth
        contentHeight: qrSurface.height
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

    Rectangle {
        id: qrSurface
        width: qrScroll.availableWidth
        // Keep the scroll extent independent of the fill-height layouts below.
        height: Math.max(root.height, root.width < 900 ? 1240 : 760)
        radius: 16
        color: settings.palette["#191a20"]
        border.color: settings.palette["#2d2e37"]

        ColumnLayout {
            id: mainColumn
            anchors.fill: parent
            anchors.margins: 20
            spacing: 12

            RowLayout {
                Layout.fillWidth: true
                Label {
                    text: "QR codes stay on this device. Decoded text is shown for review; URLs are never opened automatically."
                    color: settings.palette["#9697a3"]
                    font.pixelSize: 11
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                }
                Label {
                    text: qrCode.busy ? "Working…" : qrCode.status
                    color: qrCode.statusIsError ? settings.palette["#ff8d9c"] : settings.palette["#74d3aa"]
                    font.pixelSize: 10
                    visible: qrCode.status.length > 0
                    horizontalAlignment: Text.AlignRight
                    Layout.maximumWidth: 360
                    wrapMode: Text.WordWrap
                }
            }

            GridLayout {
                columns: root.width < 900 ? 1 : 2
                Layout.fillWidth: true
                Layout.fillHeight: true
                columnSpacing: 14
                rowSpacing: 14

                Rectangle {
                    objectName: "qrGeneratePanel"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.minimumWidth: 280
                    Layout.minimumHeight: 660
                    Layout.preferredWidth: 0.95
                    radius: 13
                    color: settings.palette["#202127"]
                    border.color: settings.palette["#30313a"]

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 16
                        spacing: 10

                        Label { text: "Generate"; color: settings.palette["#f0eef2"]; font.pixelSize: 17; font.weight: Font.DemiBold }
                        InsetTextArea {
                            id: qrTextInput
                            objectName: "qrTextInput"
                            Layout.fillWidth: true
                            Layout.preferredHeight: 100
                            placeholderText: "Enter text or a URL"
                            wrapMode: TextEdit.Wrap
                            color: settings.palette["#f0eef2"]
                            selectedTextColor: "white"
                            selectionColor: settings.palette["#75404e"]
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            Label { text: "Pixels per module"; color: settings.palette["#b0b1bb"]; font.pixelSize: 10 }
                            Slider { id: moduleSize; from: 1; to: 20; stepSize: 1; value: 8; Layout.fillWidth: true }
                            Label { text: Math.round(moduleSize.value); color: settings.palette["#f0eef2"]; font.pixelSize: 10; Layout.preferredWidth: 20 }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            Label { text: "Quiet-zone margin"; color: settings.palette["#b0b1bb"]; font.pixelSize: 10 }
                            Slider { id: qrMargin; from: 0; to: 20; stepSize: 1; value: 4; Layout.fillWidth: true }
                            Label { text: Math.round(qrMargin.value) + " modules"; color: settings.palette["#f0eef2"]; font.pixelSize: 10; Layout.preferredWidth: 66 }
                        }
                        RowLayout {
                            spacing: 8
                            Label { text: "Error correction"; color: settings.palette["#b0b1bb"]; font.pixelSize: 10 }
                            ComboBox {
                                id: errorCorrection
                                objectName: "qrErrorCorrectionPicker"
                                model: ["L · ~7%", "M · ~15%", "Q · ~25%", "H · ~30%"]
                                currentIndex: 1
                                Layout.preferredWidth: 142
                            }
                            Item { Layout.fillWidth: true }
                            Button {
                                objectName: "generateQrButton"
                                text: qrCode.busy ? "Working…" : "Generate QR"
                                enabled: !qrCode.busy
                                onClicked: qrCode.generate(qrTextInput.text, Math.round(moduleSize.value), Math.round(qrMargin.value), ["L", "M", "Q", "H"][errorCorrection.currentIndex])
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            Layout.minimumHeight: 175
                            radius: 10
                            color: "white"
                            border.color: settings.palette["#34353e"]
                            clip: true
                            Image {
                                anchors.fill: parent
                                anchors.margins: 8
                                source: qrCode.previewUrl
                                sourceSize: Qt.size(480, 480)
                                fillMode: Image.PreserveAspectFit
                                smooth: false
                                asynchronous: true
                            }
                            Label {
                                anchors.centerIn: parent
                                text: "Your QR image preview appears here"
                                visible: qrCode.previewUrl.length === 0
                                color: settings.palette["#777984"]
                                font.pixelSize: 11
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            Button { text: "Copy image"; enabled: qrCode.previewUrl.length > 0 && !qrCode.busy; onClicked: qrCode.copyGeneratedImage() }
                            Button { text: "Export PNG…"; enabled: qrCode.previewUrl.length > 0 && !qrCode.busy; onClicked: qrCode.exportGeneratedImage() }
                            Item { Layout.fillWidth: true }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            ComboBox {
                                id: galleryTag
                                objectName: "qrGalleryTagPicker"
                                Layout.fillWidth: true
                                model: [{tagId: "", name: "No gallery tag"}].concat(screenshots.tags)
                                textRole: "name"
                                valueRole: "tagId"
                                currentIndex: 0
                            }
                            Button {
                                text: "Save to gallery"
                                enabled: qrCode.previewUrl.length > 0 && !qrCode.busy
                                onClicked: qrCode.saveGeneratedToGallery(galleryTag.currentValue)
                            }
                        }
                    }
                }

                Rectangle {
                    objectName: "qrDecodePanel"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.minimumWidth: 280
                    Layout.minimumHeight: 480
                    Layout.preferredWidth: 1.05
                    radius: 13
                    color: settings.palette["#202127"]
                    border.color: settings.palette["#30313a"]
                    clip: true

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 16
                        spacing: 10

                        RowLayout {
                            Layout.fillWidth: true
                            Label { text: "Decode"; color: settings.palette["#f0eef2"]; font.pixelSize: 17; font.weight: Font.DemiBold; Layout.fillWidth: true }
                            Button { text: "Clipboard image"; enabled: !qrCode.busy; onClicked: qrCode.decodeClipboardImage() }
                            Button { text: "Choose image…"; enabled: !qrCode.busy; onClicked: qrCode.chooseImageToDecode() }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 68
                            radius: 9
                            color: dropArea.containsDrag ? settings.palette["#39222b"] : settings.palette["#17181d"]
                            border.color: dropArea.containsDrag ? settings.palette["#e96782"] : settings.palette["#34353e"]
                            Label {
                                anchors.centerIn: parent
                                text: dropArea.containsDrag ? "Drop image to decode" : "Or drop an image here"
                                color: settings.palette["#b0b1bb"]
                                font.pixelSize: 11
                            }
                            DropArea {
                                id: dropArea
                                anchors.fill: parent
                                onDropped: function(drop) {
                                    if (root.handleDroppedUrls(drop.urls))
                                        drop.acceptProposedAction()
                                }
                            }
                        }

                        Label {
                            text: qrCode.inputSource.length > 0 ? qrCode.inputSource + " · " + qrCode.results.count + " detected" : "Detected QR codes"
                            color: settings.palette["#9697a3"]
                            font.pixelSize: 10
                        }

                        ListView {
                            id: qrResultsList
                            objectName: "qrResultsList"
                            Layout.fillWidth: true
                            Layout.preferredHeight: 150
                            clip: true
                            model: qrCode.results
                            spacing: 4
                            ScrollBar.vertical: ScrollBar { }
                            delegate: Rectangle {
                                required property int index
                                required property string resultPreview
                                width: qrResultsList.width
                                height: 44
                                radius: 8
                                color: qrCode.selectedResultIndex === index ? settings.palette["#39222b"] : settings.palette["#282930"]
                                border.color: qrCode.selectedResultIndex === index ? settings.palette["#75404e"] : "transparent"
                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: 10
                                    anchors.rightMargin: 10
                                    Label { text: "QR " + (index + 1); color: settings.palette["#e88ba0"]; font.pixelSize: 10; Layout.preferredWidth: 35 }
                                    Label { text: resultPreview; color: settings.palette["#d7d6dc"]; font.pixelSize: 10; elide: Text.ElideRight; Layout.fillWidth: true }
                                }
                                TapHandler { onTapped: qrCode.selectResult(index) }
                            }
                        }

                        Label { text: "Decoded content"; color: settings.palette["#b0b1bb"]; font.pixelSize: 10 }
                        InsetTextArea {
                            id: decodedContent
                            objectName: "qrDecodedContent"
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            readOnly: true
                            selectByMouse: true
                            wrapMode: TextEdit.Wrap
                            text: qrCode.selectedResult
                            placeholderText: "Decode an image to review its QR content here. Nothing is opened or executed automatically."
                            color: settings.palette["#f0eef2"]
                            selectedTextColor: "white"
                            selectionColor: settings.palette["#75404e"]
                        }
                    }
                }
            }
        }
    }
    }
}
