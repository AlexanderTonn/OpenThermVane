import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import "pages"

ApplicationWindow {
    id: window

    function trText(key) {
        return typeof I18n === "undefined" || I18n === null
            ? key : I18n.translate(key, I18n.language)
    }
    function languageIndex(language) {
        const languages = window.languageManager.languages || []
        for (let i = 0; i < languages.length; ++i) {
            if (languages[i].code === language)
                return i
        }
        return 0
    }

    property var fanModel: typeof FanModel === "undefined" ? designFanModel : FanModel
    property var sensorModel: typeof SensorModel === "undefined" ? designSensorModel : SensorModel
    property var fanCurveModel: typeof FanCurveModel === "undefined" ? designFanCurveModel : FanCurveModel
    property var fanController: typeof FanController === "undefined" ? designFanController : FanController
    property var languageManager: typeof LanguageManager === "undefined" ? designLanguageManager : LanguageManager
    property var applicationController: typeof ThermVaneApplication === "undefined" ? null : ThermVaneApplication
    property var loggingManager: typeof LoggingManager === "undefined" ? designLoggingManager : LoggingManager

    readonly property int usableMinimumWidth: 1300
    readonly property int usableMinimumHeight: 680

    width: usableMinimumWidth
    height: usableMinimumHeight
    minimumWidth: usableMinimumWidth
    minimumHeight: usableMinimumHeight
    // Keep the UI hidden behind the native splash until the backend has
    // completed its first stable fan/sensor scan. In Qt Design Studio there is
    // no application controller, so the preview remains visible.
    visible: !window.applicationController || window.applicationController.backendReady
    title: "ThermVane"
    color: "#111318"
    Material.theme: Material.Dark
    Material.accent: "#55d6be"
    Material.primary: "#1b1f29"

    onClosing: function(close) {
        if (window.applicationController && window.applicationController.restoreFansToAutomatic)
            window.applicationController.restoreFansToAutomatic()
    }

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
                Layout.preferredWidth: 72
                model: window.languageManager.languages
                textRole: "flag"
                valueRole: "code"
                currentIndex: window.languageIndex(window.languageManager.language)
                font.pixelSize: 22
                onActivated: function(index) {
                    const languages = window.languageManager.languages || []
                    if (index >= 0 && index < languages.length)
                        window.languageManager.setLanguage(languages[index].code)
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

            TabButton { text: trText("Dashboard") }
            TabButton { text: trText("Fans") }
            TabButton { text: trText("Sensors") }
            TabButton { text: trText("Settings") }
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
                sensorModel: window.sensorModel
                fanCurveModel: window.fanCurveModel
                fanController: window.fanController
                shuttingDown: window.applicationController
                              ? window.applicationController.shuttingDown : false
            }
            SensorPage {
                sensorModel: window.sensorModel
            }
            SettingsPage {
                fanModel: window.fanModel
                sensorModel: window.sensorModel
                fanController: window.fanController
                fanCurveModel: window.fanCurveModel
                loggingManager: window.loggingManager
            }
        }
    }

    ListModel {
        id: designSensorModel
        property int refreshIntervalMs: 1000
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
        property int fanSpeedDecreaseDelayMs: 2000
    }

    QtObject {
        id: designLanguageManager
        property string language: "en"
        property var languages: [
            { "code": "en", "flag": "🇬🇧", "name": "English" },
            { "code": "de", "flag": "🇩🇪", "name": "Deutsch" }
        ]
        function setLanguage(value) { language = value }
    }

    QtObject {
        id: designLoggingManager
        property bool enabled: false
        property string logDirectory: ""
    }
}
