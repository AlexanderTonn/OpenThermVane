import QtQuick
import QtQuick.Controls
import QtQuick.Shapes

Item {
    id: root

    property var curveModel
    property var fanModel
    property var sensorModel
    property string fanId: ""
    property string sensorId: ""
    property real activeTemperature: 0
    property real activeTemperatureOverride: -1
    property string activeFanName: ""
    property real activeFanSpeed: 0
    property real activeFanSpeedOverride: -1
    property int activeFanRpm: 0
    property bool activeFanAutomatic: true
    property bool activeFanAvailable: false
    property real minTemperature: 30
    property real maxTemperature: 95
    property real minSpeed: 0
    property real maxSpeed: 100

    implicitWidth: 520
    implicitHeight: 340

    function xForTemperature(value) {
        return (value - minTemperature) / (maxTemperature - minTemperature) * width
    }

    function yForSpeed(value) {
        return height - ((value - minSpeed) / (maxSpeed - minSpeed) * height)
    }

    function temperatureForX(value) {
        return minTemperature + (Math.max(0, Math.min(width, value)) / width) * (maxTemperature - minTemperature)
    }

    function speedForY(value) {
        return maxSpeed - (Math.max(0, Math.min(height, value)) / height) * (maxSpeed - minSpeed)
    }

    function modelItems(model) {
        if (!model)
            return []

        if (model.points)
            return model.points()

        if (model.count !== undefined) {
            let result = []
            for (let i = 0; i < model.count; ++i)
                result.push(model.get(i))
            return result
        }

        return []
    }

    function points() {
        return modelItems(curveModel)
    }

    function updateActiveTemperature() {
        if (root.activeTemperatureOverride >= 0) {
            activeTemperature = root.activeTemperatureOverride
            return
        }

        const sensors = modelItems(sensorModel)
        let preferredValue = 0
        let fallbackValue = 0

        for (let i = 0; i < sensors.length; ++i) {
            const sensor = sensors[i]
            if (sensor.available === false || sensor.temperature === undefined)
                continue

            if (root.sensorId !== "") {
                if (sensor.sensorId === root.sensorId) {
                    activeTemperature = sensor.temperature
                    return
                }
                continue
            }

            const name = sensor.name !== undefined ? sensor.name : ""
            if (name.indexOf("CPU") >= 0 || name.indexOf("GPU") >= 0)
                preferredValue = Math.max(preferredValue, sensor.temperature)

            fallbackValue = Math.max(fallbackValue, sensor.temperature)
        }

        activeTemperature = preferredValue > 0 ? preferredValue : fallbackValue
    }

    function selectedFan() {
        const fans = modelItems(fanModel)
        for (let i = 0; i < fans.length; ++i) {
            if (root.fanId === "" || fans[i].fanId === root.fanId)
                return fans[i]
        }
        return ({})
    }

    function updateActiveFan() {
        const fan = selectedFan()
        activeFanAvailable = fan.fanId !== undefined || root.activeFanSpeedOverride >= 0
        activeFanName = fan.name !== undefined && fan.name !== "" ? fan.name : qsTr("Fan")
        activeFanSpeed = root.activeFanSpeedOverride >= 0
            ? root.activeFanSpeedOverride
            : (fan.speedPercent !== undefined ? fan.speedPercent : 0)
        activeFanRpm = fan.rpm !== undefined ? fan.rpm : 0
        activeFanAutomatic = fan.automatic !== undefined ? fan.automatic : true
    }

    function activeTemperatureX() {
        const value = activeTemperature > 0 ? activeTemperature : minTemperature
        return Math.max(0, Math.min(width, xForTemperature(value)))
    }

    function pointTemperature() {
        return activeTemperature > 0 ? activeTemperature : minTemperature
    }

    function pointSpeed() {
        if (activeFanSpeedOverride >= 0)
            return activeFanSpeedOverride
        return activeFanSpeed >= 0 ? activeFanSpeed : 0
    }

    function pointX() {
        return Math.max(0, Math.min(width, xForTemperature(pointTemperature())))
    }

    function pointY() {
        return Math.max(0, Math.min(height, yForSpeed(pointSpeed())))
    }

    function activeFanY() {
        return pointY()
    }

    function pointLabel() {
        const temperatureText = activeTemperature > 0 ? Math.round(pointTemperature()) + " C" : qsTr("no sensor")
        const speedText = Math.round(pointSpeed()) + "%"
        return temperatureText + " / " + speedText
    }

    function updateActiveState() {
        updateActiveTemperature()
        updateActiveFan()
    }

    function redraw() {
        curveCanvas.requestPaint()
    }

    Shape {
        anchors.fill: parent

        ShapePath {
            strokeColor: "#303746"
            strokeWidth: 1
            fillColor: "transparent"
            startX: 0
            startY: root.height
            PathLine { x: root.width; y: root.height }
            PathLine { x: root.width; y: 0 }
        }
    }

    Canvas {
        id: curveCanvas
        anchors.fill: parent
        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            ctx.clearRect(0, 0, width, height)

            ctx.strokeStyle = "#252b37"
            ctx.lineWidth = 1
            for (let i = 1; i < 5; ++i) {
                const y = height * i / 5
                ctx.beginPath()
                ctx.moveTo(0, y)
                ctx.lineTo(width, y)
                ctx.stroke()
            }

            if (root.activeTemperature > 0) {
                const markerX = root.activeTemperatureX()
                ctx.strokeStyle = "#566174"
                ctx.lineWidth = 1
                ctx.setLineDash([5, 5])
                ctx.beginPath()
                ctx.moveTo(markerX, 0)
                ctx.lineTo(markerX, height)
                ctx.stroke()
                ctx.setLineDash([])
            }

            const points = root.points()
            if (points.length > 0) {
                ctx.strokeStyle = "#55d6be"
                ctx.lineWidth = 3
                ctx.beginPath()
                for (let p = 0; p < points.length; ++p) {
                    const x = root.xForTemperature(points[p].temperature)
                    const y = root.yForSpeed(points[p].speed)
                    if (p === 0) {
                        ctx.moveTo(x, y)
                    } else {
                        ctx.lineTo(x, y)
                    }
                }
                ctx.stroke()
            }

            {
                const fanX = root.pointX()
                const fanY = root.pointY()

                ctx.strokeStyle = "#ffb86c"
                ctx.lineWidth = 1
                ctx.setLineDash([4, 4])
                ctx.beginPath()
                ctx.moveTo(0, fanY)
                ctx.lineTo(width, fanY)
                ctx.stroke()
                ctx.setLineDash([])

                ctx.fillStyle = "#ffb86c"
                ctx.strokeStyle = "#ffffff"
                ctx.lineWidth = 3
                ctx.beginPath()
                ctx.arc(fanX, fanY, 9, 0, Math.PI * 2)
                ctx.fill()
                ctx.stroke()

                ctx.fillStyle = "#11151d"
                ctx.strokeStyle = "#ffb86c"
                ctx.lineWidth = 2
                ctx.beginPath()
                ctx.arc(fanX, fanY, 4, 0, Math.PI * 2)
                ctx.fill()
                ctx.stroke()
            }
        }
    }


    Item {
        id: activeFanOverlay

        visible: true
        x: root.pointX()
        y: root.pointY()
        width: 1
        height: 1
        z: 1000

        Rectangle {
            id: activeFanVerticalLine
            x: -1
            y: -activeFanOverlay.y
            width: 2
            height: root.height
            color: "#ffb86c"
            opacity: 0.55
        }

        Rectangle {
            id: activeFanHorizontalLine
            x: -activeFanOverlay.x
            y: -1
            width: root.width
            height: 2
            color: "#ffb86c"
            opacity: 0.55
        }

        Rectangle {
            id: activeFanPoint
            x: -13
            y: -13
            width: 26
            height: 26
            radius: 13
            color: "#ff6b35"
            border.color: "#ffffff"
            border.width: 4

            Rectangle {
                anchors.centerIn: parent
                width: 8
                height: 8
                radius: 4
                color: "#11151d"
            }
        }

        Rectangle {
            id: activeFanLabel
            x: Math.min(14, root.width - activeFanOverlay.x - width - 4)
            y: activeFanOverlay.y > 42 ? -42 : 16
            width: activeFanLabelText.implicitWidth + 16
            height: activeFanLabelText.implicitHeight + 8
            radius: 6
            color: "#ff6b35"
            border.color: "#ffffff"
            border.width: 1

            Text {
                id: activeFanLabelText
                anchors.centerIn: parent
                text: root.pointLabel()
                color: "#ffffff"
                font.pixelSize: 12
                font.bold: true
            }
        }

        ToolTip.visible: fanHover.hovered
        ToolTip.text: root.activeFanName + "\n"
                      + root.pointLabel()
                      + (root.activeFanRpm > 0 ? " / " + root.activeFanRpm + " RPM" : "")

        HoverHandler { id: fanHover }
    }

    Repeater {
        id: curvePointRepeater
        model: root.curveModel

        Rectangle {
            id: handle

            required property int index
            required property real temperature
            required property real speed

            width: 16
            height: 16
            radius: 8
            color: "#55d6be"
            border.color: "#d8fff7"
            border.width: 2
            x: root.xForTemperature(handle.temperature) - width / 2
            y: root.yForSpeed(handle.speed) - height / 2

            DragHandler {
                target: handle
                xAxis.minimum: -handle.width / 2
                xAxis.maximum: root.width - handle.width / 2
                yAxis.minimum: -handle.height / 2
                yAxis.maximum: root.height - handle.height / 2
                onActiveChanged: {
                    if (!active && root.curveModel) {
                        const temperature = root.temperatureForX(handle.x + handle.width / 2)
                        const speed = root.speedForY(handle.y + handle.height / 2)

                        if (root.curveModel.movePoint) {
                            root.curveModel.movePoint(handle.index, temperature, speed)
                        } else if (root.curveModel.setProperty) {
                            root.curveModel.setProperty(handle.index, "temperature", temperature)
                            root.curveModel.setProperty(handle.index, "speed", speed)
                        }

                        root.redraw()
                    }
                }
            }

            ToolTip.visible: hover.hovered
            ToolTip.text: Math.round(handle.temperature) + " C / " + Math.round(handle.speed) + "%"

            HoverHandler { id: hover }
        }
    }

    Connections {
        target: root.curveModel || null
        function onModelReset() { root.redraw() }
        function onRowsInserted() { root.redraw() }
        function onRowsRemoved() { root.redraw() }
        function onDataChanged() { root.redraw() }
    }

    Connections {
        target: root.sensorModel || null
        ignoreUnknownSignals: true
        function onModelReset() { root.updateActiveState(); root.redraw() }
        function onRowsInserted() { root.updateActiveState(); root.redraw() }
        function onRowsRemoved() { root.updateActiveState(); root.redraw() }
        function onDataChanged() { root.updateActiveState(); root.redraw() }
    }

    Connections {
        target: root.fanModel || null
        ignoreUnknownSignals: true
        function onModelReset() { root.updateActiveFan(); root.redraw() }
        function onRowsInserted() { root.updateActiveFan(); root.redraw() }
        function onRowsRemoved() { root.updateActiveFan(); root.redraw() }
        function onDataChanged() { root.updateActiveFan(); root.redraw() }
    }

    Component.onCompleted: {
        updateActiveState()
        redraw()
    }
    onActiveTemperatureChanged: redraw()
    onActiveTemperatureOverrideChanged: { updateActiveTemperature(); redraw() }
    onActiveFanSpeedChanged: redraw()
    onActiveFanSpeedOverrideChanged: { updateActiveFan(); redraw() }
    onActiveFanAvailableChanged: redraw()
    onFanIdChanged: { updateActiveFan(); redraw() }
    onSensorIdChanged: { updateActiveTemperature(); redraw() }
    onWidthChanged: redraw()
    onHeightChanged: redraw()
}
