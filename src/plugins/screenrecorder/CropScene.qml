// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Layouts
import QtQuick.Shapes
import QtCreator.Ui

// A frame with a rectangle over it: what is outside the rectangle is dimmed,
// and each of its edges is a dashed line that can be dragged. The middle can
// be dragged too, which moves the whole rectangle.
Item {
    id: root

    // Untyped: what this draws is a CropSceneAspect, whose setCropRect() is not
    // on BaseAspect and cannot be, so the call is resolved at run time. See
    // the note about Q_INVOKABLE and Q_OBJECT in the migration plan.
    required property var aspect

    // The frame and the rectangle, which only ever change together. See
    // ScreenRecorder::Internal::CropSceneAspect.
    readonly property var scene: root.aspect?.value ?? ({})
    readonly property int fullWidth: root.scene.fullWidth ?? 0
    readonly property int fullHeight: root.scene.fullHeight ?? 0
    readonly property bool editable: (root.aspect?.enabled ?? false)
                                     && !(root.aspect?.readOnly ?? false)

    // How wide the grip along an edge is, in frame pixels - the same eight the
    // widget scene used.
    readonly property int gripWidth: 8

    implicitWidth: root.fullWidth
    implicitHeight: root.fullHeight

    function tell(x: int, y: int, w: int, h: int): void {
        root.aspect?.setCropRect(Qt.rect(x, y, w, h))
    }

    Image {
        id: frame

        objectName: "cropSceneFrame"
        anchors.fill: parent
        source: root.scene.source ?? ""
        fillMode: Image.Stretch
        cache: false
        smooth: true
    }

    // Everything outside the rectangle, in four pieces so that the rectangle
    // itself is left alone. The colour and the opacity are the widget's.
    Repeater {
        model: [
            Qt.rect(0, 0, root.width, root.scene.y ?? 0),
            Qt.rect(0, (root.scene.y ?? 0) + (root.scene.height ?? 0),
                    root.width, root.height),
            Qt.rect(0, root.scene.y ?? 0, root.scene.x ?? 0, root.scene.height ?? 0),
            Qt.rect((root.scene.x ?? 0) + (root.scene.width ?? 0), root.scene.y ?? 0,
                    root.width, root.scene.height ?? 0)
        ]

        delegate: Rectangle {
            required property rect modelData

            objectName: "cropSceneShade"
            x: modelData.x
            y: modelData.y
            width: Math.max(0, modelData.width)
            height: Math.max(0, modelData.height)
            color: "#303030"
            opacity: 0.85
        }
    }

    // The four edges, dashed white over black, which is what the widget drew
    // with two pens over the same line.
    Shape {
        anchors.fill: parent
        preferredRendererType: Shape.CurveRenderer

        ShapePath {
            id: edges

            objectName: "cropSceneEdges"
            strokeColor: "white"
            strokeWidth: 1
            strokeStyle: ShapePath.DashLine
            fillColor: "transparent"

            readonly property int left: root.scene.x ?? 0
            readonly property int top: root.scene.y ?? 0
            readonly property int right: (root.scene.x ?? 0) + (root.scene.width ?? 0)
            readonly property int bottom: (root.scene.y ?? 0) + (root.scene.height ?? 0)

            startX: edges.left
            startY: 0
            PathLine { x: edges.left; y: root.height }
            PathMove { x: edges.right; y: 0 }
            PathLine { x: edges.right; y: root.height }
            PathMove { x: 0; y: edges.top }
            PathLine { x: root.width; y: edges.top }
            PathMove { x: 0; y: edges.bottom }
            PathLine { x: root.width; y: edges.bottom }
        }
    }

    // Dragging an edge, and the middle. One handler each rather than one that
    // works out what it is on: which edge is being dragged is what the grip
    // says, and a handler already knows which one it belongs to.
    Repeater {
        model: ["left", "right", "top", "bottom"]

        delegate: Item {
            id: grip

            required property string modelData

            readonly property bool horizontal: grip.modelData === "left"
                                               || grip.modelData === "right"
            readonly property int edge: grip.modelData === "left" ? (root.scene.x ?? 0)
                                      : grip.modelData === "right"
                                        ? (root.scene.x ?? 0) + (root.scene.width ?? 0)
                                      : grip.modelData === "top" ? (root.scene.y ?? 0)
                                        : (root.scene.y ?? 0) + (root.scene.height ?? 0)

            objectName: "cropSceneGrip" + grip.modelData
            x: grip.horizontal ? grip.edge - root.gripWidth : 0
            y: grip.horizontal ? 0 : grip.edge - root.gripWidth
            width: grip.horizontal ? root.gripWidth * 2 : root.width
            height: grip.horizontal ? root.height : root.gripWidth * 2

            DragHandler {
                enabled: root.editable
                target: null
                xAxis.enabled: grip.horizontal
                yAxis.enabled: !grip.horizontal

                onCentroidChanged: {
                    if (!active)
                        return
                    const at = grip.mapToItem(root, centroid.position.x, centroid.position.y)
                    const s = root.scene
                    if (grip.modelData === "left")
                        root.tell(at.x, s.y, s.width + s.x - at.x, s.height)
                    else if (grip.modelData === "right")
                        root.tell(s.x, s.y, at.x - s.x, s.height)
                    else if (grip.modelData === "top")
                        root.tell(s.x, at.y, s.width, s.height + s.y - at.y)
                    else
                        root.tell(s.x, s.y, s.width, at.y - s.y)
                }
            }
        }
    }

    // The middle, which moves the whole rectangle. Only where there is
    // something to move it out of: a rectangle that is the whole frame has
    // nowhere to go, which is why the widget refused it too.
    Item {
        objectName: "cropSceneMoveArea"
        x: (root.scene.x ?? 0) + root.gripWidth
        y: (root.scene.y ?? 0) + root.gripWidth
        width: Math.max(0, (root.scene.width ?? 0) - root.gripWidth * 2)
        height: Math.max(0, (root.scene.height ?? 0) - root.gripWidth * 2)

        DragHandler {
            enabled: root.editable
                     && !(root.scene.width === root.fullWidth
                          && root.scene.height === root.fullHeight)
            target: null

            property point grabbedAt

            onActiveChanged: {
                if (active)
                    grabbedAt = Qt.point(centroid.position.x, centroid.position.y)
            }
            onCentroidChanged: {
                if (!active)
                    return
                const at = parent.mapToItem(root, centroid.position.x, centroid.position.y)
                const s = root.scene
                root.tell(Math.round(at.x - grabbedAt.x - root.gripWidth),
                          Math.round(at.y - grabbedAt.y - root.gripWidth),
                          s.width, s.height)
            }
        }
    }
}
