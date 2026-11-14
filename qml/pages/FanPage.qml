import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import "../controls"

Item {
    id: root

    function trText(key) {
        return typeof I18n === "undefined" || I18n === null
            ? key : I18n.translate(key, I18n.language)
    }

    property var fanModel
    property var sensorModel
    property var fanCurveModel
    property var fanController
    property bool shuttingDown: false
    property var curveAutoFans: ({})
    property string selectedFanId: fanCurveModel && fanCurveModel.selectedFanId !== undefined
                                   ? fanCurveModel.selectedFanId : ""
    property string selectedSensorId: fanCurveModel && fanCurveModel.selectedSensorId !== undefined
                                      ? fanCurveModel.selectedSensorId : ""
    property real selectedSensorTemperature: -1
    property var displayedFanSpeeds: ({})
    property var lastAppliedCurveSpeeds: ({})
    property var applyingCurveSpeeds: ({})
    property var pendingCurveSpeeds: ({})
    property var curveApplyRetryAt: ({})
    property var sensorFailSafeState: ({})
    property var defaultSystemAutoAppliedFans: ({})
    property bool startupFanControlApplied: false

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
        // Do not silently substitute the first fan when a previously saved
        // fan is temporarily absent during an SMC/model refresh. Doing so
        // can apply that fan's default sensor (usually Battery) to the wrong
        // physical fan.
        return -1
    }

    function findSensorIndex(sensorId) {
        for (let i = 0; i < modelCount(sensorModel); ++i) {
            const sensor = itemAt(sensorModel, i)
            if (sensor.sensorId === sensorId)
                return i
        }
        // A saved fan binding is authoritative. Never substitute the first
        // sensor (usually Battery) while the sensor model is still loading or
        // when a temporarily unavailable sensor is omitted from the model.
        return -1
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
        else {
            delete next[fanId]
            clearPendingCurveSpeed(fanId)
            const retry = Object.assign({}, curveApplyRetryAt)
            delete retry[fanId]
            curveApplyRetryAt = retry
        }
        curveAutoFans = next

        if (fanCurveModel && fanCurveModel.setCurveAutoForFan)
            fanCurveModel.setCurveAutoForFan(fanId, enabled)
    }

    function requestSystemAuto(fanId) {
        if (!fanId || !fanModel || !fanModel.restoreAutomaticControl)
            return false

        setCurveAuto(fanId, false)
        clearPendingCurveSpeed(fanId)
        const failed = Object.assign({}, lastAppliedCurveSpeeds)
        delete failed[fanId]
        lastAppliedCurveSpeeds = failed
        return fanModel.restoreAutomaticControl(fanId)
    }

    function forgetLastAppliedCurveSpeed(fanId) {
        const next = Object.assign({}, lastAppliedCurveSpeeds)
        delete next[fanId]
        lastAppliedCurveSpeeds = next
    }

    function cancelApplyingCurveSpeed(fanId) {
        const next = Object.assign({}, applyingCurveSpeeds)
        delete next[fanId]
        applyingCurveSpeeds = next
    }

    function clearPendingCurveSpeed(fanId) {
        const next = Object.assign({}, pendingCurveSpeeds)
        delete next[fanId]
        pendingCurveSpeeds = next
    }

    function clearSensorFailSafeState(fanId) {
        if (sensorFailSafeState[fanId] === undefined)
            return
        const next = Object.assign({}, sensorFailSafeState)
        delete next[fanId]
        sensorFailSafeState = next
    }

    function handleUnavailableSensor(fanId) {
        if (shuttingDown)
            return
        if (!fanModel || !fanModel.restoreAutomaticControl)
            return

        const now = Date.now()
        const previous = sensorFailSafeState[fanId]
        // Sensor models can be populated a little later than fan models at
        // startup. Give the selected sensor a short grace period before
        // handing the fan back to firmware; otherwise one fan can remain in
        // System Auto while the other already runs the configured curve.
        if (!previous) {
            const initial = Object.assign({}, sensorFailSafeState)
            initial[fanId] = { missingSince: now, nextAttempt: now + 1000 }
            sensorFailSafeState = initial
            return
        }
        if (now < previous.nextAttempt)
            return

        if (now - previous.missingSince < 5000) {
            const waiting = Object.assign({}, sensorFailSafeState)
            waiting[fanId] = { missingSince: previous.missingSince, nextAttempt: now + 1000 }
            sensorFailSafeState = waiting
            return
        }

        const next = Object.assign({}, sensorFailSafeState)
        next[fanId] = { missingSince: previous.missingSince, nextAttempt: now + 15000 }
        sensorFailSafeState = next
        // Publish the backoff state before the synchronous backend call. The
        // restore triggers a hardware scan and QML model signals immediately;
        // without this ordering, re-entrant applyCurve() calls restore the
        // same fan in a tight loop.
        const restored = fanModel.restoreAutomaticControl(fanId)
        if (restored) {
            forgetLastAppliedCurveSpeed(fanId)
            clearPendingCurveSpeed(fanId)
        }
        console.warn("Selected fan sensor unavailable; system automatic restore requested", fanId,
                     "restored", restored)
    }

    function fanSpeedDecreaseDelayMs() {
        if (fanController && fanController.fanSpeedDecreaseDelayMs !== undefined)
            return Math.max(0, fanController.fanSpeedDecreaseDelayMs)
        return 2000
    }

    function disableCurveAutoAfterFailedApply(fanId) {
        clearPendingCurveSpeed(fanId)
        forgetLastAppliedCurveSpeed(fanId)

        // A failed first write is usually a transient startup condition
        // (thermalmonitord/helper not ready yet). Keep the user's Auto mode
        // selected and retry with a bounded backoff instead of silently
        // switching one fan to System Auto.
        const retry = Object.assign({}, curveApplyRetryAt)
        retry[fanId] = Date.now() + 2000
        curveApplyRetryAt = retry
        console.warn("Curve fan write failed; keeping Auto mode and retrying", fanId)

        const fanIndex = findFanIndex(fanId)
        if (fanIndex >= 0) {
            const fan = itemAt(fanModel, fanIndex)
            if (fan.speedPercent !== undefined)
                setDisplayedSpeedForFan(fanId, fan.speedPercent)
        }
    }

    function ensureDefaultSystemAutoForFans() {
        if (!fanModel || !fanModel.restoreAutomaticControl)
            return
        if (startupFanControlApplied || modelCount(fanModel) === 0)
            return

        const startInCurveAuto = fanCurveModel && fanCurveModel.startupMode === "curve"
        const nextApplied = Object.assign({}, defaultSystemAutoAppliedFans)
        for (let i = 0; i < modelCount(fanModel); ++i) {
            const fan = itemAt(fanModel, i)
            if (!fan.fanId || nextApplied[fan.fanId] === true)
                continue

            nextApplied[fan.fanId] = true
            defaultSystemAutoAppliedFans = Object.assign({}, nextApplied)
            if (startInCurveAuto) {
                setCurveAuto(fan.fanId, true)
                applyCurve(fan.fanId)
            } else if (fan.supportsFirmwareControl === true || fan.supportsControl === true) {
                // Do not perform a redundant SMC restore on a fan that is
                // already owned by the firmware. The restore call causes a
                // synchronous hardware scan during startup and can briefly
                // feed an unstable sensor reading into the emergency policy.
                if (fan.automatic !== true)
                    requestSystemAuto(fan.fanId)
                else
                    setCurveAuto(fan.fanId, false)
            }
        }
        defaultSystemAutoAppliedFans = nextApplied
        startupFanControlApplied = true
    }

    function applyCurve(fanId) {
        if (shuttingDown)
            return
        if (!fanModel || !fanModel.setManualSpeed || !fanCurveModel)
            return
        const fanIndex = findFanIndex(fanId)
        if (fanIndex < 0 || itemAt(fanModel, fanIndex).supportsControl !== true)
            return

        const retryAt = curveApplyRetryAt[fanId]
        if (retryAt !== undefined && Date.now() < retryAt)
            return

        const sensorId = fanCurveModel.sensorIdForFan ? fanCurveModel.sensorIdForFan(fanId) : selectedSensorId
        const temperature = sensorTemperature(sensorId)
        if (temperature <= 0) {
            handleUnavailableSensor(fanId)
            return
        }
        clearSensorFailSafeState(fanId)

        let speed = fanCurveModel.speedForFanTemperature
                    ? fanCurveModel.speedForFanTemperature(fanId, temperature)
                    : fanCurveModel.speedForTemperature(temperature)
        if (lastAppliedCurveSpeeds[fanId] !== undefined && Math.abs(lastAppliedCurveSpeeds[fanId] - speed) < 0.5)
            return
        if (applyingCurveSpeeds[fanId] === true)
            return

        const currentSpeed = displayedSpeedForFan(fanId)
        const decreaseDelay = fanSpeedDecreaseDelayMs()
        if (decreaseDelay > 0 && currentSpeed >= 0 && speed < currentSpeed - 0.5) {
            const now = Date.now()
            const pending = pendingCurveSpeeds[fanId]
            if (!pending) {
                const nextPending = Object.assign({}, pendingCurveSpeeds)
                nextPending[fanId] = { speed: speed, deadline: now + decreaseDelay }
                pendingCurveSpeeds = nextPending
                return
            }

            if (now < pending.deadline) {
                const updatedPending = Object.assign({}, pendingCurveSpeeds)
                updatedPending[fanId] = { speed: speed, deadline: pending.deadline }
                pendingCurveSpeeds = updatedPending
                return
            }

            speed = pending.speed
            clearPendingCurveSpeed(fanId)
        } else {
            clearPendingCurveSpeed(fanId)
        }

        let nextApplying = Object.assign({}, applyingCurveSpeeds)
        nextApplying[fanId] = true
        applyingCurveSpeeds = nextApplying

        if (fanModel.setManualSpeed(fanId, speed)) {
            const nextApplied = Object.assign({}, lastAppliedCurveSpeeds)
            nextApplied[fanId] = speed
            lastAppliedCurveSpeeds = nextApplied
            setDisplayedSpeedForFan(fanId, speed)

            const retry = Object.assign({}, curveApplyRetryAt)
            delete retry[fanId]
            curveApplyRetryAt = retry
        } else {
            disableCurveAutoAfterFailedApply(fanId)
        }

        cancelApplyingCurveSpeed(fanId)
    }

    function initializeSelection() {
        if (!fanCurveModel)
            return

        let fanIndex = findFanIndex(selectedFanId)
        if (fanIndex < 0 && !selectedFanId && modelCount(fanModel) > 0)
            fanIndex = 0
        if (fanIndex >= 0) {
            const fan = itemAt(fanModel, fanIndex)
            if (fan.fanId !== selectedFanId) {
                selectedFanId = fan.fanId
                fanCurveModel.selectedFanId = selectedFanId
            }
            fanCombo.currentIndex = fanIndex
        }

        const sensorId = fanCurveModel.sensorIdForFan
            ? fanCurveModel.sensorIdForFan(selectedFanId) : ""
        let sensorIndex = findSensorIndex(sensorId)
        if (sensorIndex >= 0) {
            const sensor = itemAt(sensorModel, sensorIndex)
            selectedSensorId = sensor.sensorId
            sensorCombo.currentIndex = sensorIndex
        } else {
            // Preserve the exact saved ID even if its sensor is temporarily
            // unavailable. An empty ID remains empty until the user selects a
            // sensor explicitly; there is intentionally no fallback.
            selectedSensorId = sensorId || ""
            sensorCombo.currentIndex = -1
        }
        updateSelectedSensorTemperature()
    }

    Timer {
        interval: 250
        repeat: true
        running: !root.shuttingDown
        onTriggered: {
            for (let fanId in root.curveAutoFans)
                root.applyCurve(fanId)
        }
    }

    RowLayout {
        anchors.fill: parent
        spacing: 14

        GroupBox {
            title: trText("Fan")
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
                    systemAutoMode: model.automatic && !root.isCurveAuto(model.fanId)
                    supportsControl: model.supportsControl
                    supportsRpm: model.supportsRpm
                    supportsFirmwareControl: model.supportsFirmwareControl
                    Component.onCompleted: root.setDisplayedSpeedForFan(fanCard.fanId, fanCard.displayedSpeedPercent)
                    onDisplayedSpeedPercentChanged: root.setDisplayedSpeedForFan(fanCard.fanId, displayedSpeedPercent)
                    onManualSpeedRequested: function(speed) {
                        root.setCurveAuto(fanCard.fanId, false)
                        root.forgetLastAppliedCurveSpeed(fanCard.fanId)
                        root.setDisplayedSpeedForFan(fanCard.fanId, speed)
                        if (fanModel.setManualSpeed && !fanModel.setManualSpeed(fanCard.fanId, speed))
                            fanCard.displayedSpeedPercent = fanCard.speedPercent
                    }
                    onManualModeRequested: function(speed) {
                        root.setCurveAuto(fanCard.fanId, false)
                        root.forgetLastAppliedCurveSpeed(fanCard.fanId)
                        root.setDisplayedSpeedForFan(fanCard.fanId, speed)
                        if (fanModel.setManualSpeed && !fanModel.setManualSpeed(fanCard.fanId, speed))
                            fanCard.displayedSpeedPercent = fanCard.speedPercent
                    }
                    onAutomaticRequested: {
                        root.setCurveAuto(fanCard.fanId, true)
                        root.applyCurve(fanCard.fanId)
                    }
                    onSystemAutomaticRequested: {
                        if (!root.requestSystemAuto(fanCard.fanId))
                            fanCard.displayedSpeedPercent = fanCard.speedPercent
                    }
                }
            }
        }

        GroupBox {
            title: trText("Curve")
            Layout.fillWidth: true
            Layout.fillHeight: true

            ColumnLayout {
                anchors.fill: parent
                spacing: 12

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12

                    Label { text: trText("Fan") }
                    ComboBox {
                        id: fanCombo
                        Layout.preferredWidth: 220
                        Layout.minimumWidth: 150
                        model: fanModel
                        textRole: "name"
                        valueRole: "fanId"
                        onActivated: function(index) {
                            const fan = root.itemAt(fanModel, index)
                            if (!fan.fanId)
                                return
                            root.selectedFanId = fan.fanId
                            fanCurveModel.selectedFanId = fan.fanId
                            const sensorId = fanCurveModel.sensorIdForFan
                                ? fanCurveModel.sensorIdForFan(fan.fanId) : ""
                            const sensorIndex = root.findSensorIndex(sensorId)
                            if (sensorIndex >= 0)
                                sensorCombo.currentIndex = sensorIndex
                            root.selectedSensorId = sensorId
                            if (sensorIndex < 0)
                                sensorCombo.currentIndex = -1
                            root.updateSelectedSensorTemperature()
                        }
                    }

                    Label { text: trText("Sensor") }
                    ComboBox {
                        id: sensorCombo
                        Layout.fillWidth: true
                        Layout.preferredWidth: 260
                        Layout.minimumWidth: 160
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

                    ToolButton {
                        icon.source: "qrc:/assets/icons/import.svg"
                        icon.width: 22
                        icon.height: 22
                        display: AbstractButton.IconOnly
                        Layout.preferredWidth: 48
                        Layout.minimumWidth: 44
                        ToolTip.visible: hovered
                        ToolTip.text: trText("Import")
                        Accessible.name: trText("Import")
                        onClicked: importCurveDialog.open()
                    }

                    ToolButton {
                        icon.source: "qrc:/assets/icons/export.svg"
                        icon.width: 22
                        icon.height: 22
                        display: AbstractButton.IconOnly
                        Layout.preferredWidth: 48
                        Layout.minimumWidth: 44
                        ToolTip.visible: hovered
                        ToolTip.text: trText("Export")
                        Accessible.name: trText("Export")
                        onClicked: exportCurveDialog.open()
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
        function onCountChanged() {
            root.initializeSelection()
            root.ensureDefaultSystemAutoForFans()
        }
        function onModelReset() {
            root.initializeSelection()
            root.ensureDefaultSystemAutoForFans()
        }
        function onRowsInserted() {
            root.initializeSelection()
            root.ensureDefaultSystemAutoForFans()
        }
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
                root.forgetLastAppliedCurveSpeed(root.selectedFanId)
                root.applyCurve(root.selectedFanId)
            }
        }
        function onSelectedFanIdChanged() {
            root.selectedFanId = fanCurveModel.selectedFanId
            root.initializeSelection()
        }
        function onSelectedSensorIdChanged() {
            root.selectedSensorId = fanCurveModel.selectedSensorId
            root.initializeSelection()
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
        ensureDefaultSystemAutoForFans()
    }

    FileDialog {
        id: importCurveDialog
        title: trText("Import curve profile")
        fileMode: FileDialog.OpenFile
        nameFilters: [trText("ThermVane curve profile") + " (*.otvc)"]
        onAccepted: {
            if (fanCurveModel && fanCurveModel.importCurves && fanCurveModel.importCurves(selectedFile)) {
                root.syncCurveAutoFromModel()
                root.initializeSelection()
                root.updateSelectedSensorTemperature()
            }
        }
    }

    FileDialog {
        id: exportCurveDialog
        title: trText("Export curve profile")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "otvc"
        nameFilters: [trText("ThermVane curve profile") + " (*.otvc)"]
        onAccepted: {
            if (fanCurveModel && fanCurveModel.exportCurves)
                fanCurveModel.exportCurves(selectedFile)
        }
    }
}
