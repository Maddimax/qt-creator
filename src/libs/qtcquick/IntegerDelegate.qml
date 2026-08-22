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
    required property string labelText
    required property string toolTip
    required property bool aspectVisible
    required property var minimum
    required property var maximum
    required property var step

    visible: aspectVisible
    spacing: Spacing.GapHM
    Layout.fillWidth: true

    Label {
        text: delegate.labelText
        Layout.preferredWidth: 200
        elide: Text.ElideRight
    }

    SpinBox {
        editable: true
        enabled: (delegate.aspect?.enabled ?? false) && !(delegate.aspect?.readOnly ?? false)
        from: delegate.minimum
        to: delegate.maximum
        stepSize: delegate.step
        value: delegate.aspect?.value ?? 0
        ToolTip.text: delegate.toolTip
        ToolTip.visible: hovered && delegate.toolTip !== ""

        onValueModified: if (delegate.aspect) delegate.aspect.value = value
    }

    Item { Layout.fillWidth: true }
}
