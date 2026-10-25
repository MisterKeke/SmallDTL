import QtQuick
import QtQuick.Controls

ComboBox {
    id: control
    // ComboBox.currentValue is read-only in Qt 6.8. Keep the saved value
    // separate and derive the writable index from the current model.
    property var selectedValue

    function restoreSelection() {
        currentIndex = Qt.binding(function() {
            control.model
            control.count
            return control.indexOfValue(control.selectedValue)
        })
    }

    Component.onCompleted: restoreSelection()
    onSelectedValueChanged: restoreSelection()
    onModelChanged: Qt.callLater(restoreSelection)
    onCountChanged: Qt.callLater(restoreSelection)
    // Let the consumer read the user's currentValue and attempt persistence
    // first. A rejected edit then returns to the unchanged saved value.
    Connections {
        target: control
        function onActivated(index) { Qt.callLater(control.restoreSelection) }
    }
}
