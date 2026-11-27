import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root

    function trText(key) {
        return typeof I18n === "undefined" || I18n === null
            ? key : I18n.translate(key, I18n.language)
    }

    property string fanId
    property string name
    property int rpm
    property real speedPercent
    property real displayedSpeedPercent: speedPercent
    property bool automatic: true
    property bool curveMode: false
    property bool systemAutoMode: automatic && !curveMode
    property bool supportsControl: false
    property bool supportsRpm: false
    property bool supportsFirmwareControl: false
    property bool compact: false
    readonly property bool manualMode: !root.curveMode && !root.systemAutoMode

    signal manualSpeedRequested(real speed)
    signal manualModeRequested(real speed)
    signal automaticRequested()
    signal systemAutomaticRequested()

    onSpeedPercentChanged: {
        if (!sliderControl.dragging)
            displayedSpeedPercent = speedPercent
    }
    onCurveModeChanged: displayedSpeedPercent = speedPercent
    Component.onCompleted: displayedSpeedPercent = speedPercent

    width: 300
    implicitHeight: contentLayout.implicitHeight + 24
    height: implicitHeight

    radius: 8
    color: "#1b1f29"
    border.color: supportsControl ? "#2d3442" : "#4b3f2d"

    ColumnLayout {
        id: contentLayout
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

            ProgressBar {
                from: 0
                to: 100
                value: root.displayedSpeedPercent
                Layout.fillWidth: true
            }

            Label {
                text: Math.round(root.displayedSpeedPercent) + "%"
                horizontalAlignment: Text.AlignRight
                Layout.preferredWidth: 44
            }
        }

        PercentageSlider {
            id: sliderControl
            visible: !root.compact && root.manualMode
            value: root.displayedSpeedPercent
            enabledControl: root.supportsControl && !root.curveMode && !root.systemAutoMode
            Layout.fillWidth: true
            onPreviewed: function(speed) {
                root.displayedSpeedPercent = speed
            }
            onCommitted: function(speed) {
                root.displayedSpeedPercent = speed
                root.manualSpeedRequested(speed)
            }
        }

        RowLayout {
            visible: !root.compact
            Layout.fillWidth: true
            spacing: 8

            Button {
                text: trText("Manual")
                bottomPadding: 5
                topPadding: 5
                enabled: root.supportsControl && (root.curveMode || root.systemAutoMode)
                opacity: (!root.curveMode && !root.systemAutoMode) ? 1.0 : 0.8
                Layout.fillWidth: true
                onClicked: root.manualModeRequested(root.displayedSpeedPercent)
            }

            Button {
                text: trText("Auto")
                bottomPadding: 5
                topPadding: 5
                enabled: (root.supportsControl || root.supportsFirmwareControl) && !root.curveMode
                opacity: root.curveMode ? 1.0 : 0.8
                Layout.fillWidth: true
                onClicked: root.automaticRequested()
            }

            Button {
                text: trText("System Auto")
                bottomPadding: 5
                topPadding: 5
                enabled: root.supportsFirmwareControl && !root.systemAutoMode
                opacity: root.systemAutoMode ? 1.0 : 0.8
                Layout.fillWidth: true
                onClicked: root.systemAutomaticRequested()
            }
        }
    }
}
