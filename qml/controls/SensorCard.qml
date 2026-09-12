import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root

    property string name
    property real temperature
    property string source
    property bool available: true

    radius: 8
    color: "#1b1f29"
    border.color: available ? "#2d3442" : "#5a2f38"
    ToolTip.visible: mouseArea.containsMouse && root.source.length > 0
    ToolTip.text: root.source

    MouseArea {
        id: mouseArea
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.NoButton
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 4

        Label {
            text: root.name
            font.pixelSize: 15
            font.weight: Font.DemiBold
            elide: Text.ElideRight
            Layout.fillWidth: true
        }

        Label {
            text: root.available ? Number(root.temperature).toFixed(1) + " C" : "--"
            font.pixelSize: 28
            font.weight: Font.Bold
        }

    }
}
