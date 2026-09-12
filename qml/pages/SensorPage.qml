import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../controls"

Item {
    property var sensorModel

    GridView {
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
}
