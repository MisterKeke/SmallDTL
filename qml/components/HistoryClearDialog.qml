import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: root
    objectName: "historyClearDialog"
    title: "Clear clipboard history?"
    modal: true
    anchors.centerIn: parent
    width: parent ? Math.min(parent.width - 24, 460) : 460
    background: Rectangle {
        color: settings.palette["#191a20"]
        border.color: settings.palette["#2d2e37"]
        radius: 14
    }
    standardButtons: Dialog.Cancel | Dialog.Ok
    onOpened: includePins.checked = false
    onAccepted: clipboardHistory.clearHistory(includePins.checked)
    contentItem: ColumnLayout {
        spacing: 12
        Label {
            Layout.fillWidth: true
            property int affectedCount: clipboardHistory.totalCount >= 0 ? clipboardHistory.clearCount(includePins.checked) : 0
            text: "Remove " + affectedCount + (affectedCount === 1 ? " entry" : " entries") + " from all history, including items hidden by search?"
            wrapMode: Text.WordWrap
        }
        CheckBox { id: includePins; objectName: "historyClearIncludePins"; text: "Also remove pinned entries" }
        Label {
            Layout.fillWidth: true
            text: "History image copies will be deleted. Gallery images and original referenced files stay in place."
            wrapMode: Text.WordWrap
        }
    }
}
