import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

RowLayout {
    id: root

    property alias value: slider.value
    property bool enabledControl: true
    signal moved(real value)

    spacing: 10

    Slider {
        id: slider
        from: 0
        to: 100
        stepSize: 1
        enabled: root.enabledControl
        Layout.fillWidth: true
        onMoved: root.moved(value)
    }

    Label {
        text: Math.round(slider.value) + "%"
        horizontalAlignment: Text.AlignRight
        Layout.preferredWidth: 46
    }
}
