// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCreator.Ui

// Where the reader is in the clip, over a band showing which part of it is
// being kept. The widget painted the band into the groove and left the handle
// to the style; here the groove is the band's background and the handle is the
// control's own.
Slider {
    id: root

    // Untyped: what this draws is a TrimSliderAspect, whose setCurrentFrame()
    // is not on BaseAspect. See CropScene.qml.
    required property var aspect

    readonly property var trim: root.aspect?.value ?? ({})
    readonly property int frames: root.trim.frames ?? 0

    from: 0
    to: Math.max(1, root.frames)
    stepSize: 1
    snapMode: Slider.SnapAlways
    value: root.trim.current ?? 0
    enabled: (root.aspect?.enabled ?? false) && !(root.aspect?.readOnly ?? false)
    Layout.fillWidth: true

    onMoved: root.aspect?.setCurrentFrame(Math.round(root.value))

    background: Rectangle {
        objectName: "trimSliderGroove"
        x: root.leftPadding
        y: root.topPadding + root.availableHeight / 2 - height / 2
        width: root.availableWidth
        height: Spacing.PrimitiveS
        radius: height / 2
        color: Tokens.foregroundSubtle

        // What is kept, in the clip's own frames.
        Rectangle {
            objectName: "trimSliderBand"
            x: parent.width * ((root.trim.start ?? 0) / Math.max(1, root.frames))
            width: parent.width * (((root.trim.end ?? 0) - (root.trim.start ?? 0))
                                   / Math.max(1, root.frames))
            height: parent.height
            radius: parent.radius
            color: Tokens.accentDefault
        }
    }
}
