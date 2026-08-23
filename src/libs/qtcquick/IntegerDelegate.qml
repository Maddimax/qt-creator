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
    // The descriptor, read from the aspect rather than taken as model roles, so
    // that a hand-written page can use this delegate with nothing but the
    // aspect. See AspectModels::presentation().
    readonly property var pres: aspect ? AspectModels.presentation(aspect) : ({})

    visible: aspectVisible
    spacing: Spacing.GapHM
    Layout.fillWidth: true

    Label {
        text: delegate.labelText
        Layout.preferredWidth: Metrics.formLabelWidth
        elide: Text.ElideRight
    }

    SpinBox {
        editable: true
        enabled: (delegate.aspect?.enabled ?? false) && !(delegate.aspect?.readOnly ?? false)
        from: delegate.pres.minimum
        to: delegate.pres.maximum
        stepSize: delegate.pres.step
        value: delegate.aspect?.value ?? 0
        ToolTip.text: delegate.toolTip
        ToolTip.visible: hovered && delegate.toolTip !== ""

        onValueModified: if (delegate.aspect) delegate.aspect.value = value
    }

    Item { Layout.fillWidth: true }
}
