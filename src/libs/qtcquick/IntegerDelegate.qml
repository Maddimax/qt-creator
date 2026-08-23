// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCreator.Ui

RowLayout {
    id: delegate

    required property Aspect aspect
    readonly property string labelText: aspect?.labelText ?? ""
    readonly property string toolTip: aspect?.toolTip ?? ""
    readonly property bool aspectVisible: aspect?.visible ?? true

    Connections {
        target: delegate.aspect
        function onControlConfigurationChanged() {
            delegate.pres = AspectModels.presentation(delegate.aspect)
        }
    }
    // The descriptor, read from the aspect rather than taken as model roles, so
    // that a hand-written page can use this delegate with nothing but the
    // aspect. See AspectModels::presentation().
    // Not readonly: an aspect can change what it wants drawn - the label on
    // Copilot's sign-in button is its state - and says so with
    // controlConfigurationChanged().
    property var pres: aspect ? AspectModels.presentation(aspect) : ({})

    visible: aspectVisible
    spacing: Spacing.GapHM
    Layout.fillWidth: true

    Label {
        text: delegate.labelText
        Layout.preferredWidth: Metrics.formLabelWidth
        elide: Text.ElideRight
    }

    Label {
        text: delegate.pres.prefix ?? ""
        visible: text !== ""
    }

    SpinBox {
        editable: true
        enabled: (delegate.aspect?.enabled ?? false) && !(delegate.aspect?.readOnly ?? false)
        // A delegate can outlive its aspect: the property goes null and pres
        // becomes empty, so every bound needs a value of the right type.
        from: delegate.pres.minimum ?? 0
        to: delegate.pres.maximum ?? 0
        stepSize: delegate.pres.step ?? 1
        value: delegate.aspect?.value ?? 0
        ToolTip.text: delegate.toolTip
        ToolTip.visible: hovered && delegate.toolTip !== ""

        onValueModified: if (delegate.aspect) delegate.aspect.value = value
    }

    Label {
        text: delegate.pres.suffix ?? ""
        visible: text !== ""
    }

    Item { Layout.fillWidth: true }
}
