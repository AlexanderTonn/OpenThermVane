import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../controls"

Item {
    function trText(key) { return typeof I18n === "undefined" ? key : I18n.translate(key, I18n.language) }

    property var sensorModel

    GridView {
        id: sensorGrid
        anchors.fill: parent
        cellWidth: 230
        cellHeight: 120
        model: sensorModel
        clip: true

        delegate: SensorCard {
            width: 212
            height: 96
            name: model.name
            temperature: model.temperature
            source: model.source
            available: model.available
        }
    }

    Label {
        anchors.centerIn: parent
        width: Math.min(parent.width - 40, 520)
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
        opacity: 0.7
        visible: sensorGrid.count === 0
        text: trText("No temperature sensors with readable values found.")
    }
}
