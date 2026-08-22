// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtCreator.Ui

// Mirrors Utils::QtcBadge. Utils::InfoLabelType (Information, Warning,
// Error, Ok, NotOk, None) is a plain C++ enum with no QML registration, so
// it is re-expressed here as Status with one entry per distinct colour:
// NotOk collapses into Error and None into Ok, since they already resolved
// to the same colour in Utils::colorForInfoType.
Item {
    id: root

    enum Role {
        NumberPrimary,
        NumberSecondary
    }
    enum Status {
        Ok,
        Information,
        Warning,
        Error
    }

    property string text: ""
    property int role: QtcBadge.Role.NumberPrimary
    property int status: QtcBadge.Status.Ok

    readonly property bool filled: root.role === QtcBadge.Role.NumberPrimary

    readonly property color statusColor: {
        if (root.status === QtcBadge.Status.Information)
            return Tokens.notificationNeutralMuted
        if (root.status === QtcBadge.Status.Warning)
            return Tokens.notificationAlertMuted
        if (root.status === QtcBadge.Status.Error)
            return Tokens.notificationDangerMuted
        return Tokens.notificationSuccessMuted // Ok
    }

    readonly property color badgeColor: root.enabled ? root.statusColor : Tokens.foregroundSubtle
    readonly property color textColor: !root.enabled ? Tokens.textSubtle
                                       : root.filled ? Tokens.basicWhite : Tokens.textDefault

    implicitWidth: Spacing.PaddingHXs * 2 + label.implicitWidth
    implicitHeight: Spacing.PaddingVXxs * 2 + Fonts.labelSmallLineHeight

    Accessible.role: Accessible.StaticText
    Accessible.name: root.text

    Rectangle {
        anchors.fill: parent
        radius: Math.min(parent.width, parent.height) / 2
        color: root.filled ? root.badgeColor : "transparent"
        border.width: root.filled ? 0 : 1
        border.color: root.badgeColor
    }

    Text {
        id: label
        anchors.fill: parent
        text: root.text
        font: Fonts.labelSmall
        color: root.textColor
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }
}
