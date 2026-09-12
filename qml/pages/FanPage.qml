import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../controls"

Item {
    property var fanModel
    property var fanCurveModel

    RowLayout {
        anchors.fill: parent
        spacing: 14

        GroupBox {
            title: qsTr("Fan")
            Layout.preferredWidth: 360
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
                    onManualSpeedRequested: if (fanModel.setManualSpeed) fanModel.setManualSpeed(fanId, speed)
                    onAutomaticRequested: if (fanModel.restoreAutomaticControl) fanModel.restoreAutomaticControl(fanId)
                }
            }
        }

        GroupBox {
            title: qsTr("Curve")
            Layout.fillWidth: true
            Layout.fillHeight: true

            ColumnLayout {
                anchors.fill: parent
                spacing: 12

                FanCurveEditor {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    curveModel: fanCurveModel
                }

                RowLayout {
                    Layout.fillWidth: true

                    Label { text: "30 C" }
                    Item { Layout.fillWidth: true }
                    Label { text: "95 C" }
                }
            }
        }
    }
}
