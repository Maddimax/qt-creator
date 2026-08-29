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

    // Whether this is a row of the form, whose label takes the form's label
    // column, or a continuation of a check box beside it.
    property bool compact: false

    visible: aspectVisible
    spacing: Spacing.GapHM
    Layout.fillWidth: true

    Label {
        text: delegate.labelText
        // The form's label column, so that the labels down a page line up.
        // A row that begins with a check box is a continuation of it rather
        // than a row of the form, and there the label belongs beside its own
        // control - a column's width of air between them reads as two
        // unrelated things.
        Layout.preferredWidth: delegate.compact ? implicitWidth
                                                : Metrics.formLabelWidth
        // An aspect with no label of its own reserves no room for one.
        visible: text !== ""
        elide: Text.ElideRight
    }

    ComboBox {
        id: combo

        // What a QComboBox would show beside the text, empty for most lists.
        readonly property var icons: delegate.pres.optionIcons ?? []

        // Which entry the aspect says is current. Named so that it can be
        // bound again: a ComboBox puts currentIndex back to 0 when its model
        // changes, and the binding only re-evaluates when the aspect's value
        // changes - which refilling a list with the same value selected does
        // not do. A list that is refilled while the page is open (the kit's
        // device type, a toolchain's ABI) lost what was picked that way.
        readonly property int wantedIndex: delegate.pres.valueIsChoiceId
                                           ? delegate.pres.optionIds.indexOf(
                                                 String(delegate.aspect?.value ?? ""))
                                           : (delegate.aspect?.value ?? 0)

        enabled: (delegate.aspect?.enabled ?? false) && !(delegate.aspect?.readOnly ?? false)
        currentIndex: wantedIndex
        onModelChanged: currentIndex = Qt.binding(() => combo.wantedIndex)
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
