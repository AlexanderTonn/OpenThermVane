import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import "pages"

ApplicationWindow {
    id: window

    property var fanModel: typeof FanModel === "undefined" ? designFanModel : FanModel
    property var sensorModel: typeof SensorModel === "undefined" ? designSensorModel : SensorModel
    property var fanCurveModel: typeof FanCurveModel === "undefined" ? designFanCurveModel : FanCurveModel
    property var fanController: typeof FanController === "undefined" ? designFanController : FanController
    property var languageManager: typeof LanguageManager === "undefined" ? designLanguageManager : LanguageManager

    width: 1040
    height: 680
    visible: true
    title: "ThermVane"
    color: "#111318"
    Material.theme: Material.Dark
    Material.accent: "#55d6be"
    Material.primary: "#1b1f29"

    header: ToolBar {
        height: 56

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 18
            anchors.rightMargin: 12
            spacing: 16

            Label {
                text: "ThermVane"
                font.pixelSize: 20
                font.weight: Font.DemiBold
                Layout.fillWidth: true
            }

            ComboBox {
                id: languageBox
                model: ["en", "de"]
                currentIndex: window.languageManager.language === "de" ? 1 : 0
                onActivated: window.languageManager.setLanguage(currentText)
            }

            Button {
                text: qsTr("Scan")
                onClicked: {
                    if (window.fanModel.scan)
                        window.fanModel.scan()
                    if (window.sensorModel.scan)
                        window.sensorModel.scan()
                }
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 18
        spacing: 14

        TabBar {
            id: tabs
            Layout.fillWidth: true

            TabButton { text: qsTr("Dashboard") }
            TabButton { text: qsTr("Fans") }
            TabButton { text: qsTr("Sensors") }
            TabButton { text: qsTr("Settings") }
        }

        StackLayout {
            currentIndex: tabs.currentIndex
            Layout.fillWidth: true
            Layout.fillHeight: true

            DashboardPage {
                fanModel: window.fanModel
                sensorModel: window.sensorModel
            }
            FanPage {
                fanModel: window.fanModel
                fanCurveModel: window.fanCurveModel
            }
            SensorPage {
                sensorModel: window.sensorModel
            }
            SettingsPage {
                fanModel: window.fanModel
                sensorModel: window.sensorModel
                fanController: window.fanController
            }
        }
    }

    ListModel {
        id: designSensorModel
        ListElement { sensorId: "cpu-package"; name: "CPU Package"; temperature: 54.0; source: "mock"; available: true }
        ListElement { sensorId: "gpu-core"; name: "GPU Core"; temperature: 61.0; source: "mock"; available: true }
        ListElement { sensorId: "ssd"; name: "SSD"; temperature: 42.0; source: "mock"; available: true }
    }

    ListModel {
        id: designFanModel
        ListElement { fanId: "cpu-fan"; name: "CPU Fan"; rpm: 1120; speedPercent: 42.0; automatic: true; supportsControl: true; supportsRpm: true; supportsFirmwareControl: true }
        ListElement { fanId: "case-fan"; name: "Case Fan"; rpm: 840; speedPercent: 31.0; automatic: true; supportsControl: true; supportsRpm: true; supportsFirmwareControl: true }
        ListElement { fanId: "laptop-fan"; name: "Laptop Fan"; rpm: 1680; speedPercent: 64.0; automatic: true; supportsControl: false; supportsRpm: true; supportsFirmwareControl: true }
    }

    ListModel {
        id: designFanCurveModel
        ListElement { temperature: 40.0; speed: 20.0 }
        ListElement { temperature: 50.0; speed: 30.0 }
        ListElement { temperature: 60.0; speed: 45.0 }
        ListElement { temperature: 70.0; speed: 65.0 }
        ListElement { temperature: 80.0; speed: 85.0 }
        ListElement { temperature: 90.0; speed: 100.0 }
    }

    QtObject {
        id: designFanController
        property real emergencyTemperature: 95
        property real minimumSpeed: 20
    }

    QtObject {
        id: designLanguageManager
        property string language: "en"
        function setLanguage(value) { language = value }
    }
}
