import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root

    property string fanId
    property string name
    property int rpm
    property real speedPercent
    property bool automatic: true
    property bool supportsControl: false
    property bool supportsRpm: false
    property bool supportsFirmwareControl: false
    property bool compact: false

    signal manualSpeedRequested(real speed)
    signal automaticRequested()
    width: 300
    height: 250

    radius: 8
    color: "#1b1f29"
    border.color: supportsControl ? "#2d3442" : "#4b3f2d"

    ColumnLayout {
        visible: true
        anchors.fill: parent
        anchors.margins: 12
        clip: false
        transformOrigin: Item.Center
        z: 0
        layoutDirection: Qt.LeftToRight
        spacing: 5

        RowLayout {
            Layout.fillWidth: true

            Label {
                text: root.name
                font.pixelSize: 16
                font.weight: Font.DemiBold
                elide: Text.ElideRight
                Layout.fillWidth: true
            }

            Label {
                text: root.supportsRpm ? root.rpm + " RPM" : "--"
                opacity: 0.72
            }
        }

        RowLayout {
            spacing: 5
            Layout.fillWidth: true

            Label {
                text: root.automatic ? qsTr("Auto") : qsTr("Manual")
                opacity: 0.8
            }

            ProgressBar {
                from: 0
                to: 100
                value: root.speedPercent
                Layout.fillWidth: true
            }

            Label {
                text: Math.round(root.speedPercent) + "%"
                horizontalAlignment: Text.AlignRight
                Layout.preferredWidth: 44
            }
        }

        PercentageSlider {
            visible: !root.compact
            value: root.speedPercent
            enabledControl: root.supportsControl
            Layout.fillWidth: true
            onMoved: root.manualSpeedRequested(value)
        }

        Button {
            visible: !root.compact
            text: qsTr("Auto")
            bottomPadding: 5
            topPadding: 5
            enabled: root.supportsFirmwareControl
            onClicked: root.automaticRequested()
        }
    }
}
