import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../controls"

Item {
    id: root

    property var fanModel
    property var sensorModel
    property var fanCurveModel
    property var curveAutoFans: ({})
    property string selectedFanId: fanCurveModel && fanCurveModel.selectedFanId !== undefined
                                   ? fanCurveModel.selectedFanId : ""
    property string selectedSensorId: fanCurveModel && fanCurveModel.selectedSensorId !== undefined
                                      ? fanCurveModel.selectedSensorId : ""
    property real selectedSensorTemperature: -1
    property var displayedFanSpeeds: ({})
    property var lastAppliedCurveSpeeds: ({})
    property var applyingCurveSpeeds: ({})

    function itemAt(model, row) {
        if (!model || row < 0)
            return ({})
        if (model.get)
            return model.get(row)
        return ({})
    }

    function modelCount(model) {
        return model && model.count !== undefined ? model.count : 0
    }

    function findFanIndex(fanId) {
        for (let i = 0; i < modelCount(fanModel); ++i) {
            const fan = itemAt(fanModel, i)
            if (fan.fanId === fanId)
                return i
        }
        return modelCount(fanModel) > 0 ? 0 : -1
    }

    function findSensorIndex(sensorId) {
        for (let i = 0; i < modelCount(sensorModel); ++i) {
            const sensor = itemAt(sensorModel, i)
            if (sensor.sensorId === sensorId)
                return i
        }
        return modelCount(sensorModel) > 0 ? 0 : -1
    }

    function sensorTemperature(sensorId) {
        for (let i = 0; i < modelCount(sensorModel); ++i) {
            const sensor = itemAt(sensorModel, i)
            if (sensor.sensorId === sensorId && sensor.temperature !== undefined)
                return sensor.temperature
        }
        return 0
    }

    function updateSelectedSensorTemperature() {
        let sensor = ({})
        if (selectedSensorId) {
            const sensorIndex = findSensorIndex(selectedSensorId)
            if (sensorIndex >= 0)
                sensor = itemAt(sensorModel, sensorIndex)
        }

        if (!sensor.sensorId && sensorCombo && sensorCombo.currentIndex >= 0)
            sensor = itemAt(sensorModel, sensorCombo.currentIndex)

        if (sensor.sensorId && !selectedSensorId)
            selectedSensorId = sensor.sensorId

        selectedSensorTemperature = sensor.temperature !== undefined && sensor.available !== false
            ? sensor.temperature
            : -1
    }


    function displayedSpeedForFan(fanId) {
        if (displayedFanSpeeds[fanId] !== undefined)
            return displayedFanSpeeds[fanId]

        const fanIndex = findFanIndex(fanId)
        if (fanIndex >= 0) {
            const fan = itemAt(fanModel, fanIndex)
            if (fan.speedPercent !== undefined)
                return fan.speedPercent
        }
        return -1
    }

    function setDisplayedSpeedForFan(fanId, speed) {
        const next = Object.assign({}, displayedFanSpeeds)
        next[fanId] = speed
        displayedFanSpeeds = next
    }

    function isCurveAuto(fanId) {
        if (curveAutoFans[fanId] === true)
            return true
        if (fanCurveModel && fanCurveModel.curveAutoForFan)
            return fanCurveModel.curveAutoForFan(fanId)
        return curveAutoFans[fanId] === true
    }

    function syncCurveAutoFromModel() {
        if (!fanCurveModel || !fanCurveModel.curveAutoFanIds)
            return

        const next = ({})
        const fanIds = fanCurveModel.curveAutoFanIds()
        for (let i = 0; i < fanIds.length; ++i) {
            if (fanIds[i])
                next[fanIds[i]] = true
        }
        curveAutoFans = next
    }

    function selectedFanSupportsControl() {
        const fanIndex = findFanIndex(selectedFanId)
        if (fanIndex < 0)
            return false
        const fan = itemAt(fanModel, fanIndex)
        return fan.supportsControl === true
    }

    function setCurveAuto(fanId, enabled) {
        if (!fanId)
            return

        const next = Object.assign({}, curveAutoFans)
        if (enabled)
            next[fanId] = true
        else
            delete next[fanId]
        curveAutoFans = next

        if (fanCurveModel && fanCurveModel.setCurveAutoForFan)
            fanCurveModel.setCurveAutoForFan(fanId, enabled)
    }

    function applyCurve(fanId) {
        if (!fanModel || !fanModel.setManualSpeed || !fanCurveModel)
            return
        const fanIndex = findFanIndex(fanId)
        if (fanIndex < 0 || itemAt(fanModel, fanIndex).supportsControl !== true)
            return

        const sensorId = fanCurveModel.sensorIdForFan ? fanCurveModel.sensorIdForFan(fanId) : selectedSensorId
        const temperature = sensorTemperature(sensorId)
        if (temperature <= 0)
            return

        const speed = fanCurveModel.speedForFanTemperature
                    ? fanCurveModel.speedForFanTemperature(fanId, temperature)
                    : fanCurveModel.speedForTemperature(temperature)
        if (lastAppliedCurveSpeeds[fanId] !== undefined && Math.abs(lastAppliedCurveSpeeds[fanId] - speed) < 0.5)
            return
        if (applyingCurveSpeeds[fanId] === true)
            return

        let nextApplying = Object.assign({}, applyingCurveSpeeds)
        nextApplying[fanId] = true
        applyingCurveSpeeds = nextApplying

        const nextApplied = Object.assign({}, lastAppliedCurveSpeeds)
        nextApplied[fanId] = speed
        lastAppliedCurveSpeeds = nextApplied
        if (fanModel.setManualSpeed(fanId, speed)) {
            setDisplayedSpeedForFan(fanId, speed)
        } else {
            const failed = Object.assign({}, lastAppliedCurveSpeeds)
            delete failed[fanId]
            lastAppliedCurveSpeeds = failed
        }

        nextApplying = Object.assign({}, applyingCurveSpeeds)
        delete nextApplying[fanId]
        applyingCurveSpeeds = nextApplying
    }

    function defaultSensorId() {
        for (let i = 0; i < modelCount(sensorModel); ++i) {
            const sensor = itemAt(sensorModel, i)
            if (sensor.sensorId && sensor.available !== false)
                return sensor.sensorId
        }
        return ""
    }

    function initializeSelection() {
        if (!fanCurveModel)
            return

        let fanIndex = findFanIndex(selectedFanId)
        if (fanIndex >= 0) {
            const fan = itemAt(fanModel, fanIndex)
            if (fan.fanId !== selectedFanId) {
                selectedFanId = fan.fanId
                fanCurveModel.selectedFanId = selectedFanId
            }
            fanCombo.currentIndex = fanIndex
        }

        let sensorId = fanCurveModel.sensorIdForFan ? fanCurveModel.sensorIdForFan(selectedFanId) : selectedSensorId
        if (!sensorId) {
            sensorId = selectedSensorId || defaultSensorId()
            if (sensorId && fanCurveModel.setSensorIdForFan)
                fanCurveModel.setSensorIdForFan(selectedFanId, sensorId)
        }
        let sensorIndex = findSensorIndex(sensorId)
        if (sensorIndex >= 0) {
            const sensor = itemAt(sensorModel, sensorIndex)
            if (sensor.sensorId !== sensorId && fanCurveModel.setSensorIdForFan)
                fanCurveModel.setSensorIdForFan(selectedFanId, sensor.sensorId)
            selectedSensorId = sensor.sensorId
            sensorCombo.currentIndex = sensorIndex
        }
        updateSelectedSensorTemperature()
    }

    Timer {
        interval: 1000
        repeat: true
        running: true
        onTriggered: {
            for (let fanId in root.curveAutoFans)
                root.applyCurve(fanId)
        }
    }

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
                    id: fanCard
                    width: ListView.view.width
                    fanId: model.fanId
                    name: model.name
                    rpm: model.rpm
                    speedPercent: model.speedPercent
                    automatic: model.automatic
                    curveMode: root.isCurveAuto(model.fanId)
                    supportsControl: model.supportsControl
                    supportsRpm: model.supportsRpm
                    supportsFirmwareControl: model.supportsFirmwareControl
                    Component.onCompleted: root.setDisplayedSpeedForFan(fanCard.fanId, fanCard.displayedSpeedPercent)
                    onDisplayedSpeedPercentChanged: root.setDisplayedSpeedForFan(fanCard.fanId, displayedSpeedPercent)
                    onManualSpeedRequested: function(speed) {
                        root.setCurveAuto(fanCard.fanId, false)
                        delete root.lastAppliedCurveSpeeds[fanCard.fanId]
                        root.setDisplayedSpeedForFan(fanCard.fanId, speed)
                        if (fanModel.setManualSpeed && !fanModel.setManualSpeed(fanCard.fanId, speed))
                            fanCard.displayedSpeedPercent = fanCard.speedPercent
                    }
                    onManualModeRequested: function(speed) {
                        root.setCurveAuto(fanCard.fanId, false)
                        delete root.lastAppliedCurveSpeeds[fanCard.fanId]
                        root.setDisplayedSpeedForFan(fanCard.fanId, speed)
                        if (fanModel.setManualSpeed && !fanModel.setManualSpeed(fanCard.fanId, speed))
                            fanCard.displayedSpeedPercent = fanCard.speedPercent
                    }
                    onAutomaticRequested: {
                        root.setCurveAuto(fanCard.fanId, true)
                        root.applyCurve(fanCard.fanId)
                    }
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

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12

                    Label { text: qsTr("Fan") }
                    ComboBox {
                        id: fanCombo
                        Layout.preferredWidth: 220
                        model: fanModel
                        textRole: "name"
                        valueRole: "fanId"
                        onActivated: function(index) {
                            const fan = root.itemAt(fanModel, index)
                            if (!fan.fanId)
                                return
                            root.selectedFanId = fan.fanId
                            fanCurveModel.selectedFanId = fan.fanId
                            let sensorId = fanCurveModel.sensorIdForFan ? fanCurveModel.sensorIdForFan(fan.fanId) : ""
                            if (!sensorId) {
                                sensorId = root.selectedSensorId || root.defaultSensorId()
                                if (sensorId && fanCurveModel.setSensorIdForFan)
                                    fanCurveModel.setSensorIdForFan(fan.fanId, sensorId)
                            }
                            const sensorIndex = root.findSensorIndex(sensorId)
                            if (sensorIndex >= 0)
                                sensorCombo.currentIndex = sensorIndex
                            root.selectedSensorId = sensorIndex >= 0
                                ? root.itemAt(sensorModel, sensorIndex).sensorId : sensorId
                            root.updateSelectedSensorTemperature()
                        }
                    }

                    Label { text: qsTr("Sensor") }
                    ComboBox {
                        id: sensorCombo
                        Layout.preferredWidth: 260
                        model: sensorModel
                        textRole: "name"
                        valueRole: "sensorId"
                        onActivated: function(index) {
                            const sensor = root.itemAt(sensorModel, index)
                            if (!sensor.sensorId)
                                return
                            root.selectedSensorId = sensor.sensorId
                            const sensorReadable = sensor.temperature !== undefined && sensor.available !== false
                            root.selectedSensorTemperature = sensorReadable
                                ? sensor.temperature : -1
                            if (fanCurveModel && fanCurveModel.setSensorIdForFan)
                                fanCurveModel.setSensorIdForFan(root.selectedFanId, sensor.sensorId)
                            if (root.isCurveAuto(root.selectedFanId))
                                root.applyCurve(root.selectedFanId)
                        }
                    }

                }

                FanCurveEditor {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    curveModel: fanCurveModel
                    fanModel: fanModel
                    sensorModel: sensorModel
                    fanId: root.selectedFanId
                    sensorId: root.selectedSensorId
                    activeTemperatureOverride: root.selectedSensorTemperature
                    activeFanSpeedOverride: root.displayedSpeedForFan(root.selectedFanId)
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

    Connections {
        target: fanModel || null
        function onCountChanged() { root.initializeSelection() }
        function onModelReset() { root.initializeSelection() }
        function onRowsInserted() { root.initializeSelection() }
        function onRowsRemoved() { root.initializeSelection() }
        function onDataChanged() { root.updateSelectedSensorTemperature() }
    }


    Connections {
        target: fanCurveModel || null
        function onModelReset() {
            if (root.isCurveAuto(root.selectedFanId))
                root.applyCurve(root.selectedFanId)
        }
        function onDataChanged() {
            if (root.isCurveAuto(root.selectedFanId)) {
                delete root.lastAppliedCurveSpeeds[root.selectedFanId]
                root.applyCurve(root.selectedFanId)
            }
        }
        function onSelectedFanIdChanged() {
            root.selectedFanId = fanCurveModel.selectedFanId
            root.initializeSelection()
        }
        function onSelectedSensorIdChanged() {
            root.selectedSensorId = fanCurveModel.selectedSensorId
        }
        function onCurveAutoFansChanged() {
            root.syncCurveAutoFromModel()
        }
    }

    Connections {
        target: sensorModel || null
        function onCountChanged() { root.initializeSelection() }
        function onModelReset() { root.initializeSelection() }
        function onRowsInserted() { root.initializeSelection() }
        function onRowsRemoved() { root.initializeSelection() }
        function onDataChanged() {
            root.initializeSelection()
            root.updateSelectedSensorTemperature()
            for (let fanId in root.curveAutoFans)
                root.applyCurve(fanId)
        }
    }

    Component.onCompleted: {
        syncCurveAutoFromModel()
        initializeSelection()
        updateSelectedSensorTemperature()
    }
}
