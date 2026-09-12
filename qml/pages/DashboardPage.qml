import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../controls"

Item {
    property var fanModel
    property var sensorModel

    GridLayout {
        anchors.fill: parent
        columns: width > 760 ? 2 : 1
        columnSpacing: 14
        rowSpacing: 14

        GroupBox {
            title: qsTr("Temperature")
            Layout.fillWidth: true
            Layout.fillHeight: true

            GridView {
                anchors.fill: parent
                cellWidth: 220
                cellHeight: 110
                model: sensorModel
                clip: true

                delegate: SensorCard {
                    width: 204
                    height: 92
                    name: model.name
                    temperature: model.temperature
                    source: model.source
                    available: model.available
                }
            }
        }

        GroupBox {
            title: qsTr("Fans")
            Layout.fillWidth: true
            Layout.fillHeight: true

            ListView {
                anchors.fill: parent
                spacing: 10
                clip: true
                model: fanModel

                delegate: FanCard {
                    width: ListView.view.width
                    fanId: model.fanId
                    name: model.name
                    rpm: model.rpm
                    speedPercent: model.speedPercent
                    automatic: model.automatic
                    supportsControl: model.supportsControl
                    supportsRpm: model.supportsRpm
                    supportsFirmwareControl: model.supportsFirmwareControl
                    compact: true
                    onManualSpeedRequested: if (fanModel.setManualSpeed) fanModel.setManualSpeed(fanId, speed)
                    onAutomaticRequested: if (fanModel.restoreAutomaticControl) fanModel.restoreAutomaticControl(fanId)
                }
            }
        }
    }
}
