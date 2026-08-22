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
    required property string labelText
    required property string toolTip
    required property bool aspectVisible

    visible: aspectVisible
    spacing: Spacing.GapHM
    Layout.fillWidth: true

    Label {
        text: root.labelText
        Layout.preferredWidth: Metrics.formLabelWidth
        elide: Text.ElideRight
    }

    TextField {
        text: (root.aspect?.value ?? []).join("; ")
        enabled: (root.aspect?.enabled ?? false) && !(root.aspect?.readOnly ?? false)
        readOnly: root.aspect?.readOnly ?? true
        placeholderText: qsTr("Semicolon-separated paths")
        ToolTip.text: root.toolTip
        ToolTip.visible: hovered && root.toolTip !== ""
        Layout.fillWidth: true

        onEditingFinished: {
            if (!root.aspect)
                return

            const parts = text.split(";")
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
