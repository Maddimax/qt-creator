// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCreator.Ui

// Text the page shows and the user cannot edit: a TextDisplay's message, or a
// StringAspect drawn as a label. Both put what they show in displayText; a
// label of their own, where they have one, goes in front of it.
RowLayout {
    id: root

    required property Aspect aspect
    readonly property var pres: aspect ? AspectModels.presentation(aspect) : ({})
    readonly property string labelText: aspect?.labelText ?? ""
    readonly property string displayText: aspect?.displayText ?? ""
    readonly property string toolTip: aspect?.toolTip ?? ""
    readonly property bool aspectVisible: aspect?.visible ?? true

    visible: aspectVisible && (root.labelText !== "" || root.displayText !== "")
    spacing: Spacing.GapHM
    Layout.fillWidth: true

    Label {
        text: root.labelText
        visible: root.labelText !== ""
        Layout.preferredWidth: Metrics.formLabelWidth
        elide: Text.ElideRight
    }

    Label {
        text: root.displayText
        visible: root.displayText !== ""
        wrapMode: Text.WordWrap
        color: {
            switch (root.pres.infoType ?? "None") {
            case "Error":
            case "NotOk":
                return Tokens.notificationDangerDefault
            case "Warning":
                return Tokens.notificationAlertDefault
            case "Ok":
                return Tokens.notificationSuccessDefault
            default:
                return Tokens.textMuted
            }
        }
        ToolTip.text: root.toolTip
        ToolTip.visible: false
        Layout.fillWidth: true
    }
}
