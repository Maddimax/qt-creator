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
    // The descriptor, read from the aspect rather than taken as model roles, so
    // that a hand-written page can use this delegate with nothing but the
    // aspect. See AspectModels::presentation().
    // Not readonly: an aspect can change what it wants drawn - the label on
    // Copilot's sign-in button is its state - and says so with
    // controlConfigurationChanged().
    property var pres: aspect ? AspectModels.presentation(aspect) : ({})
    readonly property string labelText: aspect?.plainLabelText ?? ""
    readonly property string toolTip: aspect?.toolTip ?? ""
    readonly property bool aspectVisible: aspect?.visible ?? true

    Connections {
        target: root.aspect
        function onControlConfigurationChanged() {
            root.pres = AspectModels.presentation(root.aspect)
        }
    }

    visible: aspectVisible
    spacing: Spacing.GapHM
    Layout.fillWidth: true

    Label {
        text: root.labelText
        Layout.preferredWidth: Metrics.formLabelWidth
        // An aspect with no label of its own reserves no room for one.
        visible: text !== ""
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
        text: root.pres.actionText
        enabled: (root.aspect?.enabled ?? false) && !(root.aspect?.readOnly ?? false)
        onClicked: root.aspect?.triggerAction()
    }
}
