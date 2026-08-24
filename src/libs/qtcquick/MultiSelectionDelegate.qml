// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCreator.Ui

ColumnLayout {
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
    readonly property bool aspectVisible: aspect?.visible ?? true

    Connections {
        target: root.aspect
        function onControlConfigurationChanged() {
            root.pres = AspectModels.presentation(root.aspect)
        }
    }

    visible: aspectVisible
    spacing: Spacing.GapVXs
    Layout.fillWidth: true

    Label {
        text: root.labelText
        Layout.fillWidth: true
    }

    Repeater {
        model: root.pres.options

        delegate: CheckBox {
            id: option

            required property string modelData

            text: option.modelData
            checked: (root.aspect?.value ?? []).indexOf(option.modelData) !== -1
            enabled: (root.aspect?.enabled ?? false) && !(root.aspect?.readOnly ?? false)
            Layout.leftMargin: Spacing.PaddingHL
            Layout.fillWidth: true

            onToggled: {
                if (!root.aspect)
                    return

                const current = root.aspect.value ?? []
                const values = []
                for (let i = 0; i < current.length; ++i) {
                    if (current[i] !== option.modelData)
                        values.push(current[i])
                }
                if (option.checked)
                    values.push(option.modelData)
                root.aspect.value = values
            }
        }
    }
}
