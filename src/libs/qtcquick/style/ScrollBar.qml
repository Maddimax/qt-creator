// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Templates as T
import QtCreator.Ui

T.ScrollBar {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding)

    padding: Spacing.PrimitiveXxs
    visible: control.policy !== T.ScrollBar.AlwaysOff

    contentItem: Rectangle {
        implicitWidth: control.interactive ? Spacing.PrimitiveS : Spacing.PrimitiveXs
        implicitHeight: control.interactive ? Spacing.PrimitiveS : Spacing.PrimitiveXs
        radius: width / 2
        color: control.pressed ? Tokens.foregroundDefault
             : control.hovered ? Tokens.foregroundMuted : Tokens.foregroundSubtle
        opacity: 0.0

        states: State {
            name: "active"
            when: control.policy === T.ScrollBar.AlwaysOn
                  || (control.active && control.size < 1.0)
            PropertyChanges { control.contentItem.opacity: 1.0 }
        }

        transitions: Transition {
            from: "active"
            SequentialAnimation {
                PauseAnimation { duration: 400 }
                NumberAnimation { target: control.contentItem; property: "opacity"; to: 0.0 }
            }
        }
    }
}
