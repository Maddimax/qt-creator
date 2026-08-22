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

    readonly property var fontFamilies: Qt.fontFamilies()

    visible: aspectVisible
    spacing: Spacing.GapHM
    Layout.fillWidth: true

    Label {
        text: root.labelText
        Layout.preferredWidth: Metrics.formLabelWidth
        elide: Text.ElideRight
    }

    ComboBox {
        id: control

        enabled: (root.aspect?.enabled ?? false) && !(root.aspect?.readOnly ?? false)
        model: root.fontFamilies
        currentIndex: root.fontFamilies.indexOf(root.aspect?.value ?? "")
        ToolTip.text: root.toolTip
        ToolTip.visible: hovered && root.toolTip !== ""
        Layout.preferredWidth: Metrics.formControlWidth

        onActivated: (index) => { if (root.aspect) root.aspect.value = control.textAt(index) }
    }

    Item { Layout.fillWidth: true }
}
