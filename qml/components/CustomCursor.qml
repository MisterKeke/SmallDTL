import QtQuick
import QtQuick.Window

Item {
    id: root
    objectName: "customCursor"

    anchors.fill: parent
    // Keep the tracking surface visible even before the first hover event.
    // The popup overlay can be hidden, so this item lives on the window instead.
    z: 2147483647
    clip: true

    readonly property bool cursorVisible: pointer.hovered && ring.status === Image.Ready
                                          && (Window.window ? Window.window.active : false)
    property point pointerPosition: pointer.point.position
    property var trailPoints: []
    property real trailOpacity: 0

    function advanceTrail(seconds) {
        // Follow at the same speed on different refresh rates and mouse polling rates.
        const blend = 1 - Math.exp(-70 * Math.min(seconds, 0.05))
        const previous = trailPoints
        const next = []
        for (let i = 0; i < previous.length; ++i) {
            const target = i === 0 ? pointerPosition : previous[i - 1]
            next.push({ "x": previous[i].x + (target.x - previous[i].x) * blend,
                        "y": previous[i].y + (target.y - previous[i].y) * blend })
        }
        trailPoints = next
    }

    onCursorVisibleChanged: {
        app.setWindowCursorHidden(Window.window, cursorVisible)
        if (!cursorVisible) {
            trailFade.stop()
            trailOpacity = 0
            trailPoints = []
        }
    }

    Component.onDestruction: {
        if (app && Window.window)
            app.setWindowCursorHidden(Window.window, false)
    }

    onPointerPositionChanged: {
        if (!cursorVisible)
            return

        if (trailPoints.length === 0) {
            const samples = []
            for (let i = 0; i < 8; ++i)
                samples.push({ "x": pointerPosition.x, "y": pointerPosition.y })
            trailPoints = samples
        }
        trailFade.stop()
        trailOpacity = 1
        trailFade.start()
    }

    HoverHandler {
        id: pointer
        objectName: "cursorHoverHandler"
        // Observe the window as an ancestor of its controls, rather than covering
        // their hover hit testing with a separate input surface above them.
        parent: root.parent
        target: null
        blocking: false
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
    }

    FrameAnimation {
        running: root.cursorVisible && root.trailPoints.length > 0 && root.trailOpacity > 0
        onTriggered: root.advanceTrail(frameTime)
    }

    SequentialAnimation {
        id: trailFade
        PauseAnimation { duration: 60 }
        NumberAnimation { target: root; property: "trailOpacity"; to: 0; duration: 220; easing.type: Easing.OutQuad }
        onFinished: root.trailPoints = []
    }

    Repeater {
        model: 8

        delegate: Rectangle {
            required property int index
            visible: root.cursorVisible
            readonly property var sample: root.trailPoints.length > index
                                                  ? root.trailPoints[index]
                                                  : ({ "x": root.pointerPosition.x, "y": root.pointerPosition.y })
            readonly property real size: 3.5 - index * 0.25

            x: sample.x - width / 2
            y: sample.y - height / 2
            width: size
            height: size
            radius: width / 2
            color: "white"
            opacity: root.trailPoints.length > 0 ? root.trailOpacity * (0.28 - index * 0.025) : 0
        }
    }

    // Outline the white artwork so the cursor also remains visible on light UI.
    Rectangle {
        visible: root.cursorVisible
        x: root.pointerPosition.x - width / 2
        y: root.pointerPosition.y - height / 2
        width: 25
        height: width
        radius: width / 2
        color: "transparent"
        border.width: 2.5
        border.color: "#99000000"
    }

    Image {
        id: ring
        objectName: "cursorImage"
        visible: root.cursorVisible
        source: "../assets/SmallDTL.png"
        width: 64
        height: width
        x: root.pointerPosition.x - width / 2
        y: root.pointerPosition.y - height / 2
        sourceSize.width: 1254
        sourceSize.height: 1254
        fillMode: Image.PreserveAspectFit
        smooth: true
        asynchronous: true
    }
}
