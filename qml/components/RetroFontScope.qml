import QtQuick
import QtQuick.Controls

Pane {
    id: root
    padding: 0
    background: Item { visible: false }

    Binding {
        target: root
        property: "font.family"
        value: settings.palette["fontFamily"]
        when: settings.retroArcade
    }
}
