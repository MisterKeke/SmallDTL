import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import QtQuick.Window

Popup {
    id: editor
    objectName: "screenshotEditor"
    modal: true
    dim: true
    closePolicy: Popup.NoAutoClose
    anchors.centerIn: Overlay.overlay
    width: 1060
    height: 735
    padding: 14
    Material.theme: Material.Dark
    Material.accent: settings.palette["#e96782"]

    property string sourcePath: ""
    property string sourceScreenshotId: ""
    property var ownerWindow: null
    property url sourceImage: ""
    property int imageWidth: 0
    property int imageHeight: 0
    property string tool: "crop"
    property real zoom: 1.0
    property color strokeColor: "#ff4f68"
    property var crop: null
    property var draftCrop: null
    property var strokes: []
    property var draftStroke: null
    property var undoStack: []
    property var redoStack: []
    property bool saving: false
    property bool windowClosePending: false
    property string pendingSavePath: ""
    property string errorText: ""
    readonly property bool hasUnsavedEdits: crop !== null || draftCrop !== null || strokes.length > 0 || draftStroke !== null
    readonly property real fitScale: imageWidth > 0 && imageHeight > 0
                                      ? Math.min(Math.max(1, viewport.width - 36) / imageWidth,
                                                 Math.max(1, viewport.height - 36) / imageHeight)
                                      : 1
    readonly property real displayScale: fitScale * zoom
    readonly property real overlayPixelScale: imageWidth > 0 && imageHeight > 0
                                                ? Math.min(1, 2048 / Math.max(imageWidth, imageHeight)) : 1
    readonly property int overlayWidth: Math.max(1, Math.ceil(imageWidth * overlayPixelScale))
    readonly property int overlayHeight: Math.max(1, Math.ceil(imageHeight * overlayPixelScale))

    background: Rectangle {
        color: settings.palette["#17181d"]
        border.color: settings.palette["#383942"]
        radius: 14
    }

    function openScreenshot(path, imageUrl, screenshotId, width, height) {
        sourcePath = path
        sourceImage = imageUrl
        sourceScreenshotId = screenshotId
        imageWidth = width
        imageHeight = height
        tool = "crop"
        zoom = 1.0
        crop = null
        draftCrop = null
        strokes = []
        draftStroke = null
        undoStack = []
        redoStack = []
        errorText = ""
        saving = false
        pendingSavePath = ""
        inheritTags.checked = true
        open()
    }

    function rememberEdits() {
        var next = undoStack.concat([{ crop: crop, strokes: strokes }])
        if (next.length > 50)
            next = next.slice(next.length - 50)
        undoStack = next
        redoStack = []
    }

    function undo() {
        if (!undoStack.length || saving)
            return
        redoStack = redoStack.concat([{ crop: crop, strokes: strokes }])
        var previous = undoStack[undoStack.length - 1]
        undoStack = undoStack.slice(0, undoStack.length - 1)
        crop = previous.crop
        strokes = previous.strokes
        draftCrop = null
        draftStroke = null
        inkCanvas.requestPaint()
    }

    function redo() {
        if (!redoStack.length || saving)
            return
        undoStack = undoStack.concat([{ crop: crop, strokes: strokes }])
        var next = redoStack[redoStack.length - 1]
        redoStack = redoStack.slice(0, redoStack.length - 1)
        crop = next.crop
        strokes = next.strokes
        draftCrop = null
        draftStroke = null
        inkCanvas.requestPaint()
    }

    function resetEdits() {
        if (!hasUnsavedEdits || saving)
            return
        rememberEdits()
        crop = null
        draftCrop = null
        strokes = []
        draftStroke = null
        zoom = 1.0
        tool = "crop"
        inkCanvas.requestPaint()
    }

    function requestClose() {
        if (saving)
            return
        if (hasUnsavedEdits)
            discardDialog.open()
        else
            close()
    }

    Connections {
        target: editor.ownerWindow
        function onClosing(closeEvent) {
            if (!editor.visible || !editor.hasUnsavedEdits)
                return
            closeEvent.accepted = false
            editor.windowClosePending = true
            if (!discardDialog.visible)
                discardDialog.open()
        }
    }

    function pointInImage(x, y) {
        const point = screenshots.editorPointToImage(x, y, editorCanvas.width, editorCanvas.height,
                                                     imageWidth, imageHeight)
        return { x: point.x, y: point.y }
    }

    function ratioValue() {
        if (aspectRatio.currentIndex === 1) return 1.0
        if (aspectRatio.currentIndex === 2) return 4.0 / 3.0
        if (aspectRatio.currentIndex === 3) return 16.0 / 9.0
        return 0.0
    }

    function requestEditedSave() {
        if (saving)
            return
        errorText = ""
        const path = screenshots.saveEditedScreenshot(sourceScreenshotId, crop || ({}), strokes,
                                                       inheritTags.checked)
        if (!path.length) {
            errorText = screenshots.lastSaveError || "Could not start saving the edited copy."
            return
        }
        pendingSavePath = path
        saving = true
    }

    Shortcut {
        sequence: "Escape"
        context: Qt.WindowShortcut
        enabled: editor.visible
        onActivated: editor.requestClose()
    }
    Shortcut {
        sequence: "Ctrl+Z"
        context: Qt.WindowShortcut
        enabled: editor.visible && editor.undoStack.length > 0
        onActivated: editor.undo()
    }
    Shortcut {
        sequence: "Ctrl+Y"
        context: Qt.WindowShortcut
        enabled: editor.visible && editor.redoStack.length > 0
        onActivated: editor.redo()
    }
    Shortcut {
        sequence: "Ctrl+Shift+Z"
        context: Qt.WindowShortcut
        enabled: editor.visible && editor.redoStack.length > 0
        onActivated: editor.redo()
    }

    Connections {
        target: screenshots
        function onSaveFinished(path, success, message) {
            if (path !== editor.pendingSavePath)
                return
            editor.saving = false
            if (success) {
                editor.crop = null
                editor.strokes = []
                editor.close()
            } else {
                editor.errorText = message
            }
            editor.pendingSavePath = ""
        }
    }

    contentItem: ColumnLayout {
        spacing: 9

        RowLayout {
            Layout.fillWidth: true
            Label {
                Layout.fillWidth: true
                text: "Edit screenshot · Save as copy"
                color: settings.palette["#f1eff2"]
                font.pixelSize: 16
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }
            ToolButton {
                text: "×"
                enabled: !editor.saving
                onClicked: editor.requestClose()
                ToolTip.visible: hovered
                ToolTip.text: "Close editor"
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            ToolButton { text: "Crop"; checkable: true; checked: editor.tool === "crop"; onClicked: editor.tool = "crop" }
            ToolButton { text: "Draw"; checkable: true; checked: editor.tool === "draw"; onClicked: editor.tool = "draw" }
            ToolButton { text: "Pan"; checkable: true; checked: editor.tool === "pan"; onClicked: editor.tool = "pan" }
            Rectangle { Layout.preferredWidth: 1; Layout.preferredHeight: 25; color: settings.palette["#383942"] }
            Label { text: "Ratio"; color: settings.palette["#b0b1bb"]; font.pixelSize: 10 }
            ComboBox {
                id: aspectRatio
                objectName: "editorAspectRatio"
                Layout.preferredWidth: 100
                model: ["Free", "1:1", "4:3", "16:9"]
                currentIndex: 0
            }
            Label { text: "Width"; color: settings.palette["#b0b1bb"]; font.pixelSize: 10 }
            Slider {
                id: strokeWidth
                objectName: "editorStrokeWidth"
                from: 1
                to: 48
                value: 6
                enabled: editor.tool === "draw"
                Layout.preferredWidth: 105
            }
            Label { text: Math.round(strokeWidth.value) + " px"; color: settings.palette["#b0b1bb"]; font.pixelSize: 10 }
            Row {
                spacing: 4
                Repeater {
                    model: ["#ff4f68", "#ffd166", "#ffffff", "#55d6be", "#4688ff", "#17181d"]
                    delegate: Rectangle {
                        required property string modelData
                        width: 20
                        height: 20
                        radius: 5
                        color: modelData
                        border.width: editor.strokeColor === modelData ? 2 : 1
                        border.color: editor.strokeColor === modelData ? "white" : settings.palette["#555660"]
                        MouseArea { anchors.fill: parent; onClicked: editor.strokeColor = modelData }
                    }
                }
            }
            Item { Layout.fillWidth: true }
            Button { text: "−"; enabled: editor.zoom > 0.25; onClicked: editor.zoom = Math.max(0.25, editor.zoom / 1.25) }
            Label { text: Math.round(editor.zoom * 100) + "%"; color: settings.palette["#b0b1bb"]; font.pixelSize: 10; Layout.minimumWidth: 42; horizontalAlignment: Text.AlignHCenter }
            Button { text: "+"; enabled: editor.zoom < 6; onClicked: editor.zoom = Math.min(6, editor.zoom * 1.25) }
            Button { text: "Fit"; onClicked: { editor.zoom = 1.0; viewport.contentX = 0; viewport.contentY = 0 } }
        }

        Rectangle {
            id: viewport
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: settings.palette["#191a20"]
            radius: 9
            clip: true

            Flickable {
                id: imageFlickable
                anchors.fill: parent
                clip: true
                interactive: editor.tool === "pan"
                boundsBehavior: Flickable.StopAtBounds
                contentWidth: Math.max(width, editorCanvas.width)
                contentHeight: Math.max(height, editorCanvas.height)

                Item {
                    id: editorCanvas
                    objectName: "editorCanvas"
                    width: Math.max(1, editor.imageWidth * editor.displayScale)
                    height: Math.max(1, editor.imageHeight * editor.displayScale)
                    x: Math.max(0, (imageFlickable.width - width) / 2)
                    y: Math.max(0, (imageFlickable.height - height) / 2)

                    Image {
                        id: editorImage
                        anchors.fill: parent
                        source: editor.sourceImage
                        sourceSize: Qt.size(2048, 2048)
                        fillMode: Image.PreserveAspectFit
                        asynchronous: true
                        cache: false
                        autoTransform: false
                    }
                    Canvas {
                        id: inkCanvas
                        objectName: "editorInkCanvas"
                        anchors.fill: parent
                        canvasSize: Qt.size(editor.overlayWidth, editor.overlayHeight)
                        renderStrategy: Canvas.Threaded
                        onPaint: {
                            const context = getContext("2d")
                            context.clearRect(0, 0, editor.overlayWidth, editor.overlayHeight)
                            const allStrokes = editor.draftStroke ? editor.strokes.concat([editor.draftStroke]) : editor.strokes
                            for (let i = 0; i < allStrokes.length; ++i) {
                                const stroke = allStrokes[i]
                                if (!stroke.points || stroke.points.length === 0) continue
                                context.beginPath()
                                context.strokeStyle = stroke.color
                                context.fillStyle = stroke.color
                                context.lineWidth = Math.max(0.4, stroke.width * editor.overlayPixelScale)
                                context.lineCap = "round"
                                context.lineJoin = "round"
                                if (stroke.points.length === 1) {
                                    const point = stroke.points[0]
                                    context.arc(point.x * editor.overlayPixelScale, point.y * editor.overlayPixelScale,
                                                Math.max(0.4, context.lineWidth / 2), 0, Math.PI * 2)
                                    context.fill()
                                } else {
                                    context.moveTo(stroke.points[0].x * editor.overlayPixelScale,
                                                   stroke.points[0].y * editor.overlayPixelScale)
                                    for (let j = 1; j < stroke.points.length; ++j)
                                        context.lineTo(stroke.points[j].x * editor.overlayPixelScale,
                                                       stroke.points[j].y * editor.overlayPixelScale)
                                    context.stroke()
                                }
                            }
                        }
                    }
                    Rectangle {
                        visible: editor.crop !== null || editor.draftCrop !== null
                        x: ((editor.draftCrop || editor.crop || ({ x: 0, y: 0, width: 0, height: 0 })).x / Math.max(1, editor.imageWidth)) * editorCanvas.width
                        y: ((editor.draftCrop || editor.crop || ({ x: 0, y: 0, width: 0, height: 0 })).y / Math.max(1, editor.imageHeight)) * editorCanvas.height
                        width: ((editor.draftCrop || editor.crop || ({ x: 0, y: 0, width: 0, height: 0 })).width / Math.max(1, editor.imageWidth)) * editorCanvas.width
                        height: ((editor.draftCrop || editor.crop || ({ x: 0, y: 0, width: 0, height: 0 })).height / Math.max(1, editor.imageHeight)) * editorCanvas.height
                        color: "#182cf2ff"
                        border.color: "#fff2f4"
                        border.width: 2
                    }
                    Label {
                        anchors.centerIn: parent
                        visible: editor.sourceImage.toString().length === 0 || editorImage.status === Image.Error
                        text: editorImage.status === Image.Error ? "Preview could not be loaded." : "Loading screenshot preview…"
                        color: "#e5e4e8"
                    }
                    MouseArea {
                        id: editMouse
                        anchors.fill: parent
                        enabled: editor.visible && editor.tool !== "pan" && !editor.saving
                        acceptedButtons: Qt.LeftButton
                        hoverEnabled: true
                        cursorShape: editor.tool === "draw" ? Qt.CrossCursor : Qt.CrossCursor
                        property point dragStart: Qt.point(0, 0)
                        property point imageStart: Qt.point(0, 0)
                        onPressed: function(mouse) {
                            dragStart = Qt.point(mouse.x, mouse.y)
                            const first = editor.pointInImage(mouse.x, mouse.y)
                            imageStart = Qt.point(first.x, first.y)
                            if (editor.tool === "crop") {
                                editor.draftCrop = { x: first.x, y: first.y, width: 0, height: 0 }
                            } else if (editor.tool === "draw") {
                                editor.draftStroke = { points: [first], color: editor.strokeColor.toString(),
                                                       width: strokeWidth.value }
                                inkCanvas.requestPaint()
                            }
                        }
                        onPositionChanged: function(mouse) {
                            if (!(mouse.buttons & Qt.LeftButton)) return
                            const current = editor.pointInImage(mouse.x, mouse.y)
                            if (editor.tool === "crop") {
                                editor.draftCrop = screenshots.editorCropFromDrag(imageStart.x, imageStart.y,
                                    current.x, current.y, editor.imageWidth, editor.imageHeight, editor.ratioValue())
                            } else if (editor.tool === "draw" && editor.draftStroke) {
                                const points = editor.draftStroke.points.concat([current])
                                editor.draftStroke = { points: points, color: editor.draftStroke.color,
                                                       width: editor.draftStroke.width }
                                inkCanvas.requestPaint()
                            }
                        }
                        onReleased: {
                            if (editor.tool === "crop") {
                                if (editor.draftCrop && editor.draftCrop.width >= 1 && editor.draftCrop.height >= 1) {
                                    editor.rememberEdits()
                                    editor.crop = editor.draftCrop
                                }
                                editor.draftCrop = null
                            } else if (editor.tool === "draw" && editor.draftStroke) {
                                editor.rememberEdits()
                                editor.strokes = editor.strokes.concat([editor.draftStroke])
                                editor.draftStroke = null
                                inkCanvas.requestPaint()
                            }
                        }
                    }
                }
            }

            Label {
                anchors.centerIn: parent
                visible: editor.sourceImage.toString().length > 0 && editor.imageWidth <= 0
                text: "Image dimensions are unavailable."
                color: settings.palette["#c8c7cf"]
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Label {
                Layout.fillWidth: true
                text: editor.errorText.length ? editor.errorText
                                               : editor.imageWidth + " × " + editor.imageHeight + " px  ·  Crop and stroke sizes use image pixels"
                color: editor.errorText.length ? settings.palette["#f08a9b"] : settings.palette["#9697a3"]
                font.pixelSize: 10
                elide: Text.ElideRight
            }
            CheckBox {
                id: inheritTags
                objectName: "editorInheritTags"
                text: "Copy original tags"
                checked: true
                enabled: !editor.saving
            }
            Button { text: "Undo"; enabled: editor.undoStack.length > 0 && !editor.saving; onClicked: editor.undo() }
            Button { text: "Redo"; enabled: editor.redoStack.length > 0 && !editor.saving; onClicked: editor.redo() }
            Button { text: "Reset"; enabled: editor.hasUnsavedEdits && !editor.saving; onClicked: editor.resetEdits() }
            Button {
                objectName: "editorSaveCopy"
                text: editor.saving ? "Saving…" : "Save as copy"
                enabled: !editor.saving && editor.imageWidth > 0 && editor.imageHeight > 0
                highlighted: true
                onClicked: editor.requestEditedSave()
            }
            Button { text: "Cancel"; enabled: !editor.saving; onClicked: editor.requestClose() }
        }
    }

    Dialog {
        id: discardDialog
        objectName: "editorDiscardDialog"
        width: 390
        parent: Overlay.overlay
        anchors.centerIn: Overlay.overlay
        title: "Discard edits?"
        modal: true
        standardButtons: Dialog.Yes | Dialog.No
        onAccepted: {
            editor.close()
            if (editor.windowClosePending) {
                editor.windowClosePending = false
                const mainWindow = editor.ownerWindow
                if (mainWindow)
                    mainWindow.close()
            }
        }
        onRejected: editor.windowClosePending = false
        contentItem: Label {
            text: "Discard the crop and drawing changes that have not been saved?"
            wrapMode: Text.WordWrap
            color: settings.palette["#c8c7cf"]
            width: 350
            padding: 12
        }
    }
}
