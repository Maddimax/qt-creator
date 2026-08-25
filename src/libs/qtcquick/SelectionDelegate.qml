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
        target: delegate.aspect
        function onControlConfigurationChanged() {
            delegate.pres = AspectModels.presentation(delegate.aspect)
        }
    }

    visible: aspectVisible
    spacing: Spacing.GapHM
    Layout.fillWidth: true

    Label {
        text: delegate.labelText
        Layout.preferredWidth: Metrics.formLabelWidth
        // An aspect with no label of its own reserves no room for one.
        visible: text !== ""
        elide: Text.ElideRight
    }

    ComboBox {
        id: combo

        // What a QComboBox would show beside the text, empty for most lists.
        readonly property var icons: delegate.pres.optionIcons ?? []

        enabled: (delegate.aspect?.enabled ?? false) && !(delegate.aspect?.readOnly ?? false)
        currentIndex: delegate.pres.valueIsChoiceId
                      ? delegate.pres.optionIds.indexOf(String(delegate.aspect?.value ?? ""))
                      : (delegate.aspect?.value ?? 0)
        // The choice under the cursor has more to say than the aspect does,
        // where the list is of things that need telling apart.
        ToolTip.text: (delegate.pres.optionToolTips?.[combo.highlightedIndex] ?? "")
                      || delegate.toolTip
        ToolTip.visible: hovered && ToolTip.text !== ""
        Layout.preferredWidth: Metrics.formControlWidth

        model: delegate.pres.options

        delegate: ItemDelegate {
            required property int index
            required property string modelData

            width: combo.width
            highlighted: combo.highlightedIndex === index
            icon.source: combo.icons[index] ?? ""
            text: modelData
        }

        onActivated: (index) => {
            if (!delegate.aspect)
                return
            delegate.aspect.value = delegate.pres.valueIsChoiceId ? delegate.pres.optionIds[index] : index
        }

        AspectContextMenu { aspect: delegate.aspect; pres: delegate.pres }
    }

    Item { Layout.fillWidth: true }
}
