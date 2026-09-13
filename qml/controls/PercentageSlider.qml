import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

RowLayout {
    id: root

    property alias value: slider.value
    property bool enabledControl: true
    readonly property bool dragging: slider.pressed
    signal previewed(real value)
    signal committed(real value)

    spacing: 10

    Slider {
        id: slider
        from: 0
        to: 100
        stepSize: 1
        enabled: root.enabledControl
        Layout.fillWidth: true
        onMoved: function() { root.previewed(value) }
        onPressedChanged: {
            if (!pressed)
                root.committed(value)
        }
    }
}
