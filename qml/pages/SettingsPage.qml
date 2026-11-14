import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    function trText(key) {
        return typeof I18n === "undefined" || I18n === null
            ? key : I18n.translate(key, I18n.language)
    }

    property var fanModel
    property var sensorModel
    property var fanController
    property var fanCurveModel
    property var loggingManager

    ScrollView {
        id: settingsScrollView
        anchors.fill: parent
        clip: true
        contentWidth: availableWidth
        ScrollBar.vertical.policy: ScrollBar.AsNeeded

        ColumnLayout {
            width: Math.min(settingsScrollView.availableWidth, 520)
            spacing: 16

            GroupBox {
            title: trText("About")
            Layout.fillWidth: true

            ColumnLayout {
                anchors.fill: parent
                spacing: 10

                RowLayout {
                    Layout.fillWidth: true

                    Label {
                        text: trText("CMake version")
                        Layout.fillWidth: true
                    }

                    Label {
                        text: CMakeProjectVersion && CMakeProjectVersion.length > 0 ? CMakeProjectVersion : trText("Unknown")
                        opacity: 0.8
                    }
                }
            }
            }

            GroupBox {
            title: trText("Safety")
            Layout.fillWidth: true

            ColumnLayout {
                anchors.fill: parent
                spacing: 10

                RowLayout {
                    Layout.fillWidth: true
                    Label {
                        text: trText("Emergency")
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
                        text: trText("Minimum")
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

                RowLayout {
                    Layout.fillWidth: true
                    Label {
                        text: trText("Fan speed decrease delay")
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                    }
                    SpinBox {
                        from: 0
                        to: 30000
                        stepSize: 250
                        editable: true
                        value: fanController && fanController.fanSpeedDecreaseDelayMs !== undefined
                               ? fanController.fanSpeedDecreaseDelayMs
                               : 2000
                        onValueModified: {
                            if (fanController)
                                fanController.fanSpeedDecreaseDelayMs = value
                        }
                    }
                    Label { text: "ms" }
                }
            }
            }

            GroupBox {
            title: trText("Sensors")
            Layout.fillWidth: true

            ColumnLayout {
                anchors.fill: parent
                spacing: 10

                RowLayout {
                    Layout.fillWidth: true

                    Label {
                        text: trText("Update interval")
                        Layout.fillWidth: true
                    }

                    SpinBox {
                        id: sensorRefreshInterval
                        from: 250
                        to: 10000
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
                    text: trText("Only temperature sensors with readable values are shown.")
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

            GroupBox {
            title: trText("Startup")
            Layout.fillWidth: true

            RowLayout {
                anchors.fill: parent
                spacing: 10

                Label {
                    text: trText("Fan control at startup")
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                }

                ComboBox {
                    id: startupMode
                    Layout.preferredWidth: 170
                    model: [trText("System Auto"), trText("Auto")]
                    currentIndex: fanCurveModel && fanCurveModel.startupMode === "curve" ? 1 : 0
                    onActivated: function(index) {
                        if (fanCurveModel)
                            fanCurveModel.startupMode = index === 1 ? "curve" : "system"
                    }
                }
            }
            }

            GroupBox {
            title: trText("Logging")
            Layout.fillWidth: true

            ColumnLayout {
                anchors.fill: parent
                spacing: 8

                CheckBox {
                    id: loggingEnabled
                    text: trText("Enable detailed logging")
                    checked: loggingManager && loggingManager.enabled
                    onToggled: {
                        if (loggingManager)
                            loggingManager.enabled = checked
                    }
                }

                Label {
                    Layout.fillWidth: true
                    opacity: 0.65
                    wrapMode: Text.WordWrap
                    text: trText("Log files are stored by day.")
                }

                Label {
                    Layout.fillWidth: true
                    visible: loggingManager && loggingManager.logDirectory
                    opacity: 0.55
                    elide: Text.ElideMiddle
                    text: loggingManager ? loggingManager.logDirectory : ""
                }

                Connections {
                    target: loggingManager
                    ignoreUnknownSignals: true

                    function onEnabledChanged() {
                        if (loggingManager)
                            loggingEnabled.checked = loggingManager.enabled
                    }
                }
            }
            }
        }
    }
}
