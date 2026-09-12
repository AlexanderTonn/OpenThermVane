import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    property var fanModel
    property var sensorModel
    property var fanController

    ColumnLayout {
        width: Math.min(parent.width, 520)
        spacing: 16

        GroupBox {
            title: qsTr("Safety")
            Layout.fillWidth: true

            ColumnLayout {
                anchors.fill: parent
                spacing: 10

                RowLayout {
                    Layout.fillWidth: true
                    Label {
                        text: qsTr("Emergency")
                        Layout.fillWidth: true
                    }
                    SpinBox {
                        from: 70
                        to: 110
                        value: Math.round(fanController.emergencyTemperature)
                        onValueModified: fanController.emergencyTemperature = value
                    }
                    Label { text: "C" }
                }

                RowLayout {
                    Layout.fillWidth: true
                    Label {
                        text: qsTr("Minimum")
                        Layout.fillWidth: true
                    }
                    SpinBox {
                        from: 0
                        to: 100
                        value: Math.round(fanController.minimumSpeed)
                        onValueModified: fanController.minimumSpeed = value
                    }
                    Label { text: "%" }
                }
            }
        }

        GroupBox {
            title: qsTr("Backend")
            Layout.fillWidth: true

            RowLayout {
                anchors.fill: parent
                Label {
                    text: qsTr("Mock")
                    Layout.fillWidth: true
                }
                Button {
                    text: qsTr("Scan")
                    onClicked: {
                        if (fanModel.scan)
                            fanModel.scan()
                        if (sensorModel.scan)
                            sensorModel.scan()
                    }
                }
            }
        }
    }
}
