// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCreator.Ui

RowLayout {
    id: root

    required property Aspect aspect
    readonly property string labelText: aspect?.plainLabelText ?? ""
    readonly property string toolTip: aspect?.toolTip ?? ""
    readonly property bool aspectVisible: aspect?.visible ?? true

    visible: aspectVisible
    spacing: Spacing.GapHM
    Layout.fillWidth: true

    FormLabel {
        text: root.labelText
    }

    TextField {
        text: (root.aspect?.value ?? []).join(", ")
        // A field shows the text around its cursor, and text set from the
        // aspect leaves it at the end - so a list too long for the field was
        // drawn from its tail, with the first patterns out of sight.
        onTextChanged: if (!activeFocus) cursorPosition = 0
        enabled: (root.aspect?.enabled ?? false) && !(root.aspect?.readOnly ?? false)
        readOnly: root.aspect?.readOnly ?? true
        ToolTip.text: root.toolTip
        ToolTip.visible: hovered && root.toolTip !== ""
        Layout.fillWidth: true

        onEditingFinished: {
            if (!root.aspect)
                return

            const parts = text.split(",")
            const values = []
            for (let i = 0; i < parts.length; ++i) {
                const trimmedPart = parts[i].trim()
                if (trimmedPart.length > 0)
                    values.push(trimmedPart)
            }
            root.aspect.value = values
        }
    }
}
