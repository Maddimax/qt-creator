// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCreator.Ui

// One choice out of a few, drawn as radio buttons rather than a combo box,
// because the aspect asked for it - a handful of alternatives the user should
// be able to read at a glance. The same aspect as SelectionDelegate draws;
// which of the two it gets is DisplayStyle's answer.
ColumnLayout {
    id: delegate

    required property Aspect aspect
    // The descriptor, read from the aspect rather than taken as model roles, so
    // that a hand-written page can use this delegate with nothing but the
    // aspect. See AspectModels::presentation().
    property var pres: aspect ? AspectModels.presentation(aspect) : ({})
    readonly property string labelText: aspect?.plainLabelText ?? ""
    readonly property string toolTip: aspect?.toolTip ?? ""
    readonly property bool aspectVisible: aspect?.visible ?? true

    Connections {
        target: delegate.aspect
        function onControlConfigurationChanged() {
            delegate.pres = AspectModels.presentation(delegate.aspect)
        }
    }

    visible: aspectVisible
    spacing: Spacing.GapVXs
    Layout.fillWidth: true

    Label {
        text: delegate.labelText
        // An aspect with no label of its own reserves no room for one.
        visible: text !== ""
        elide: Text.ElideRight
        Layout.fillWidth: true
    }

    Repeater {
        model: delegate.pres.options ?? []

        RadioButton {
            required property int index
            required property string modelData

            text: modelData
            // Grouped by the aspect's value rather than by autoExclusive: the
            // buttons come and go with the choices, and only one value is ever
            // current anyway.
            checked: delegate.pres.valueIsChoiceId
                     ? delegate.pres.optionIds[index] === String(delegate.aspect?.value ?? "")
                     : (delegate.aspect?.value ?? 0) === index
            autoExclusive: false
            enabled: (delegate.aspect?.enabled ?? false)
                     && !(delegate.aspect?.readOnly ?? false)
                     && ((delegate.pres.optionsEnabled ?? [])[index] ?? true)
            ToolTip.text: delegate.toolTip
            ToolTip.visible: hovered && delegate.toolTip !== ""
            Layout.fillWidth: true

            onToggled: {
                if (!delegate.aspect || !checked)
                    return
                delegate.aspect.value = delegate.pres.valueIsChoiceId
                    ? delegate.pres.optionIds[index] : index
            }
        }
    }
}
