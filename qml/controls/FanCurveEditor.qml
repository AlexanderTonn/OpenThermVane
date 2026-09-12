import QtQuick
import QtQuick.Controls
import QtQuick.Shapes

Item {
    id: root

    property var curveModel
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

    function points() {
        if (!curveModel)
            return []

        if (curveModel.points)
            return curveModel.points()

        if (curveModel.count !== undefined) {
            let result = []
            for (let i = 0; i < curveModel.count; ++i)
                result.push(curveModel.get(i))
            return result
        }

        return []
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

            const points = root.points()
            if (points.length === 0) {
                return
            }

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
    }

    Repeater {
        model: root.curveModel

        Rectangle {
            id: handle

            width: 16
            height: 16
            radius: 8
            color: "#55d6be"
            border.color: "#d8fff7"
            border.width: 2
            x: root.xForTemperature(model.temperature) - width / 2
            y: root.yForSpeed(model.speed) - height / 2

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
                            root.curveModel.movePoint(index, temperature, speed)
                        } else if (root.curveModel.setProperty) {
                            root.curveModel.setProperty(index, "temperature", temperature)
                            root.curveModel.setProperty(index, "speed", speed)
                        }

                        root.redraw()
                    }
                }
            }

            ToolTip.visible: hover.hovered
            ToolTip.text: Math.round(model.temperature) + " C / " + Math.round(model.speed) + "%"

            HoverHandler { id: hover }
        }
    }

    Connections {
        target: root.curveModel
        function onModelReset() { root.redraw() }
        function onRowsInserted() { root.redraw() }
        function onRowsRemoved() { root.redraw() }
        function onDataChanged() { root.redraw() }
    }

    Component.onCompleted: redraw()
    onWidthChanged: redraw()
    onHeightChanged: redraw()
}
