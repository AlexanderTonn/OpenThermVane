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
            title: qsTr("Sensors")
            Layout.fillWidth: true

            ColumnLayout {
                anchors.fill: parent
                spacing: 10

                RowLayout {
                    Layout.fillWidth: true

                    Label {
                        text: qsTr("Update interval")
                        Layout.fillWidth: true
                    }

                    SpinBox {
                        id: sensorRefreshInterval
                        from: 250
                        to: 60000
                        stepSize: 250
                        editable: true
                        value: sensorModel && sensorModel.refreshIntervalMs !== undefined
                               ? sensorModel.refreshIntervalMs
                               : 1000
                        onValueModified: {
                            if (sensorModel && sensorModel.refreshIntervalMs !== undefined)
                                sensorModel.refreshIntervalMs = value
                        }
                    }

                    Label { text: "ms" }
                }

                Label {
                    Layout.fillWidth: true
                    opacity: 0.65
                    wrapMode: Text.WordWrap
                    text: qsTr("Only temperature sensors with readable values are shown.")
                }


                Connections {
                    target: sensorModel
                    ignoreUnknownSignals: true

                    function onRefreshIntervalMsChanged() {
                        if (sensorModel && sensorModel.refreshIntervalMs !== undefined)
                            sensorRefreshInterval.value = sensorModel.refreshIntervalMs
                    }
                }
            }
        }
    }
}
