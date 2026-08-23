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
    required property var options
    // Aspects differ in what they store: an index, or the id of the choice.
    required property var optionIds
    required property bool valueIsChoiceId
    readonly property string labelText: aspect?.labelText ?? ""
    readonly property string toolTip: aspect?.toolTip ?? ""
    readonly property bool aspectVisible: aspect?.visible ?? true

    visible: aspectVisible
    spacing: Spacing.GapHM
    Layout.fillWidth: true

    Label {
        text: delegate.labelText
        Layout.preferredWidth: Metrics.formLabelWidth
        elide: Text.ElideRight
    }

    ComboBox {
        enabled: (delegate.aspect?.enabled ?? false) && !(delegate.aspect?.readOnly ?? false)
        currentIndex: delegate.valueIsChoiceId
                      ? delegate.optionIds.indexOf(String(delegate.aspect?.value ?? ""))
                      : (delegate.aspect?.value ?? 0)
        ToolTip.text: delegate.toolTip
        ToolTip.visible: hovered && delegate.toolTip !== ""
        Layout.preferredWidth: Metrics.formControlWidth

        model: delegate.options

        onActivated: (index) => {
            if (!delegate.aspect)
                return
            delegate.aspect.value = delegate.valueIsChoiceId ? delegate.optionIds[index] : index
        }
    }

    Item { Layout.fillWidth: true }
}
