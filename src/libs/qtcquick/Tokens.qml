// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma Singleton
pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtCreator.Ui

QtObject {
    readonly property color basicBlack: Theme.color(Theme.Token_Basic_Black)
    readonly property color basicWhite: Theme.color(Theme.Token_Basic_White)
    readonly property color accentDefault: Theme.color(Theme.Token_Accent_Default)
    readonly property color accentMuted: Theme.color(Theme.Token_Accent_Muted)
    readonly property color accentSubtle: Theme.color(Theme.Token_Accent_Subtle)
    readonly property color backgroundDefault: Theme.color(Theme.Token_Background_Default)
    readonly property color backgroundMuted: Theme.color(Theme.Token_Background_Muted)
    readonly property color backgroundSubtle: Theme.color(Theme.Token_Background_Subtle)
    readonly property color foregroundDefault: Theme.color(Theme.Token_Foreground_Default)
    readonly property color foregroundMuted: Theme.color(Theme.Token_Foreground_Muted)
    readonly property color foregroundSubtle: Theme.color(Theme.Token_Foreground_Subtle)
    readonly property color textDefault: Theme.color(Theme.Token_Text_Default)
    readonly property color textMuted: Theme.color(Theme.Token_Text_Muted)
    readonly property color textSubtle: Theme.color(Theme.Token_Text_Subtle)
    readonly property color textAccent: Theme.color(Theme.Token_Text_Accent)
    readonly property color textOnAccent: Theme.color(Theme.Token_Text_On_Accent)
    readonly property color strokeStrong: Theme.color(Theme.Token_Stroke_Strong)
    readonly property color strokeMuted: Theme.color(Theme.Token_Stroke_Muted)
    readonly property color strokeSubtle: Theme.color(Theme.Token_Stroke_Subtle)
    readonly property color notificationAlertDefault: Theme.color(Theme.Token_Notification_Alert_Default)
    readonly property color notificationAlertMuted: Theme.color(Theme.Token_Notification_Alert_Muted)
    readonly property color notificationAlertSubtle: Theme.color(Theme.Token_Notification_Alert_Subtle)
    readonly property color notificationSuccessDefault: Theme.color(Theme.Token_Notification_Success_Default)
    readonly property color notificationSuccessMuted: Theme.color(Theme.Token_Notification_Success_Muted)
    readonly property color notificationSuccessSubtle: Theme.color(Theme.Token_Notification_Success_Subtle)
    readonly property color notificationNeutralDefault: Theme.color(Theme.Token_Notification_Neutral_Default)
    readonly property color notificationNeutralMuted: Theme.color(Theme.Token_Notification_Neutral_Muted)
    readonly property color notificationNeutralSubtle: Theme.color(Theme.Token_Notification_Neutral_Subtle)
    readonly property color notificationDangerDefault: Theme.color(Theme.Token_Notification_Danger_Default)
    readonly property color notificationDangerMuted: Theme.color(Theme.Token_Notification_Danger_Muted)
    readonly property color notificationDangerSubtle: Theme.color(Theme.Token_Notification_Danger_Subtle)
    readonly property color gradient01Start: Theme.color(Theme.Token_Gradient01_Start)
    readonly property color gradient01End: Theme.color(Theme.Token_Gradient01_End)
    readonly property color gradient02Start: Theme.color(Theme.Token_Gradient02_Start)
    readonly property color gradient02End: Theme.color(Theme.Token_Gradient02_End)
}
