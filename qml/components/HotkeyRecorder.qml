import QtQuick
import QtQuick.Controls
import QtQuick.Window

Rectangle {
    id: root
    property int key: 0
    property int modifiers: 0
    property string placeholder: "Click and press a shortcut"
    property bool recording: false
    signal recorded(int key, int modifiers)
    signal cleared()

    onRecordingChanged: app.setHotkeyRecording(recording)
    activeFocusOnTab: true
    onActiveFocusChanged: if (!activeFocus) recording = false
    onVisibleChanged: if (!visible) recording = false
    Component.onDestruction: if (recording) app.setHotkeyRecording(false)
    Connections {
        target: root.Window.window
        function onActiveChanged() { if (root.Window.window && !root.Window.window.active) root.recording = false }
    }

    implicitWidth: 220
    implicitHeight: 42
    radius: settings.retroArcade ? settings.palette["panelRadius"] : 9
    color: recording ? settings.palette["#39222b"] : recorderMouse.containsMouse ? settings.palette["#292a32"] : settings.palette["#202127"]
    border.color: recording || activeFocus ? settings.palette["#e96782"] : settings.palette["#383942"]
    border.width: recording ? 1.5 : 1

    Text {
        anchors.centerIn: parent
        text: root.recording ? "Press the desired key combination…"
              : root.key === 0 ? root.placeholder : app.formatShortcut(root.key, root.modifiers)
        color: root.recording ? settings.palette["#ffb8c7"] : root.key === 0 ? settings.palette["#a0a1ac"] : settings.palette["#f2f1f4"]
        font.pixelSize: 12
        elide: Text.ElideRight
        width: parent.width - 24
        horizontalAlignment: Text.AlignHCenter
    }

    MouseArea {
        id: recorderMouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: {
            root.forceActiveFocus()
            root.recording = true
        }
    }

    Keys.onPressed: function(event) {
        if (!root.recording) {
            if ((event.key === Qt.Key_Return || event.key === Qt.Key_Space) && event.modifiers === Qt.NoModifier) {
                root.recording = true
                event.accepted = true
            }
            return
        }
        if (event.isAutoRepeat) { event.accepted = true; return }
        if ((event.key === Qt.Key_Escape || event.key === Qt.Key_Tab) && event.modifiers === Qt.NoModifier) {
            root.recording = false
            event.accepted = event.key !== Qt.Key_Tab
            return
        }
        if ((event.key === Qt.Key_Backspace || event.key === Qt.Key_Delete) && event.modifiers === Qt.NoModifier) {
            root.recording = false
            root.cleared()
            event.accepted = true
            return
        }
        if (event.key === Qt.Key_Control || event.key === Qt.Key_Alt || event.key === Qt.Key_Shift || event.key === Qt.Key_Meta) {
            event.accepted = true
            return
        }
        const selectedKey = event.key
        const selectedModifiers = event.modifiers & (Qt.ControlModifier | Qt.AltModifier | Qt.ShiftModifier | Qt.MetaModifier | Qt.KeypadModifier)
        root.recording = false
        root.recorded(selectedKey, selectedModifiers)
        event.accepted = true
    }

    Accessible.role: Accessible.Button
    Accessible.name: root.key === 0 ? root.placeholder : app.formatShortcut(root.key, root.modifiers)
    Accessible.onPressAction: {
        root.forceActiveFocus()
        root.recording = true
    }
}
