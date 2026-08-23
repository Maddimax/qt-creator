// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCreator.Ui

// A summary of the value and one button that acts on it, for an aspect edited
// through a dialog rather than in place.
RowLayout {
    id: root

    required property Aspect aspect
    required property string actionText
    readonly property string labelText: aspect?.labelText ?? ""
    readonly property string toolTip: aspect?.toolTip ?? ""
    readonly property bool aspectVisible: aspect?.visible ?? true

    visible: aspectVisible
    spacing: Spacing.GapHM
    Layout.fillWidth: true

    Label {
        text: root.labelText
        Layout.preferredWidth: Metrics.formLabelWidth
        elide: Text.ElideRight
    }

    Label {
        text: root.aspect?.displayText ?? ""
        elide: Text.ElideRight
        ToolTip.text: root.toolTip
        ToolTip.visible: false
        Layout.fillWidth: true
    }

    Button {
        text: root.actionText
        enabled: (root.aspect?.enabled ?? false) && !(root.aspect?.readOnly ?? false)
        onClicked: root.aspect?.triggerAction()
    }
}
