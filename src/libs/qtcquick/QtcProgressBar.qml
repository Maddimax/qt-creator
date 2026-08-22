// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtCreator.Ui

// Mirrors Utils::QtcProgressBar: a track and a fill, determinate only (the
// widget shows an empty track rather than animating when to <= from, and so
// does this). QQuickItem's Accessible attached property has no value/
// minimum/maximum properties to bind, so the percentage goes into the name
// instead.
Item {
    id: root

    property real from: 0
    property real to: 100
    property real value: 0

    readonly property real span: root.to - root.from
    readonly property real progress: root.span > 0
                                     ? Math.max(0, Math.min(1, (root.value - root.from) / root.span))
                                     : 0

    implicitWidth: Metrics.progressBarMinimumWidth
    implicitHeight: Spacing.PrimitiveM

    Accessible.role: Accessible.ProgressBar
    Accessible.name: qsTr("%1 percent").arg(Math.round(root.progress * 100))

    Rectangle {
        id: track
        anchors.fill: parent
        radius: height / 2
        color: Tokens.foregroundSubtle
    }

    Rectangle {
        visible: root.progress > 0
        x: track.x
        y: track.y
        width: track.width * root.progress
        height: track.height
        radius: height / 2
        color: root.enabled ? Tokens.accentDefault : Tokens.foregroundDefault
    }
}
