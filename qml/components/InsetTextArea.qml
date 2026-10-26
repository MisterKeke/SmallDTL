import QtQuick
import QtQuick.Controls
import QtQuick.Templates as T

// A plain, inset placeholder rather than Material's floating field label.
T.TextArea {
    id: control
    implicitWidth: 280
    implicitHeight: Math.max(56, contentHeight + topPadding + bottomPadding)
    padding: 12
    clip: true
    selectByMouse: true
    color: settings.palette["#f0eef2"]
    placeholderTextColor: settings.palette["#9697a3"]
    selectedTextColor: "white"
    selectionColor: settings.palette["#75404e"]
    background: Rectangle {
        implicitWidth: 120
        implicitHeight: 56
        radius: settings.retroArcade ? settings.palette["panelRadius"] : 9
        color: settings.palette["#17181d"]
        border.color: settings.palette["#34353e"]
    }
    Label {
        objectName: "insetPlaceholder"
        x: control.leftPadding
        y: control.topPadding
        width: Math.max(0, control.width - control.leftPadding - control.rightPadding)
        text: control.placeholderText
        textFormat: Text.PlainText
        font: control.font
        color: control.placeholderTextColor
        wrapMode: Text.WordWrap
        visible: control.length === 0 && control.preeditText.length === 0
    }
}
