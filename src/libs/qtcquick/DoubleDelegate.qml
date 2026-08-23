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

    TextField {
        text: Number(delegate.aspect?.value ?? 0).toLocaleString(Qt.locale())
        enabled: delegate.aspect?.enabled ?? false
        readOnly: delegate.aspect?.readOnly ?? true
        horizontalAlignment: TextInput.AlignRight
        ToolTip.text: delegate.toolTip
        ToolTip.visible: hovered && delegate.toolTip !== ""
        Layout.preferredWidth: Metrics.formControlWidth / 2

        validator: DoubleValidator {
            // See IntegerDelegate: pres is empty once the aspect is gone.
            bottom: delegate.pres.minimum ?? 0
            top: delegate.pres.maximum ?? 0
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
