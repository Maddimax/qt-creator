// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCreator.Ui

// QtQuick.Controls SpinBox is integer only, so a double aspect gets a validated
// text field rather than a spin box that would truncate it.
RowLayout {
    id: delegate

    required property Aspect aspect
    required property string labelText
    required property string toolTip
    required property bool aspectVisible
    required property var minimum
    required property var maximum

    visible: aspectVisible
    spacing: Spacing.GapHM
    Layout.fillWidth: true

    Label {
        text: delegate.labelText
        Layout.preferredWidth: 200
        elide: Text.ElideRight
    }

    TextField {
        text: Number(delegate.aspect?.value ?? 0).toLocaleString(Qt.locale())
        enabled: delegate.aspect?.enabled ?? false
        readOnly: delegate.aspect?.readOnly ?? true
        horizontalAlignment: TextInput.AlignRight
        ToolTip.text: delegate.toolTip
        ToolTip.visible: hovered && delegate.toolTip !== ""
        Layout.preferredWidth: 120

        validator: DoubleValidator {
            bottom: delegate.minimum
            top: delegate.maximum
            notation: DoubleValidator.StandardNotation
        }

        onEditingFinished: {
            const parsed = Number.fromLocaleString(Qt.locale(), text)
            if (!isNaN(parsed) && delegate.aspect)
                delegate.aspect.value = parsed
        }
    }

    Item { Layout.fillWidth: true }
}
