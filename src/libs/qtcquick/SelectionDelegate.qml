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
    required property string labelText
    required property string toolTip
    required property bool aspectVisible

    visible: aspectVisible
    spacing: Spacing.GapHM
    Layout.fillWidth: true

    Label {
        text: delegate.labelText
        Layout.preferredWidth: 200
        elide: Text.ElideRight
    }

    ComboBox {
        enabled: delegate.aspect.enabled && !delegate.aspect.readOnly
        currentIndex: delegate.aspect.value ?? 0
        ToolTip.text: delegate.toolTip
        ToolTip.visible: hovered && delegate.toolTip !== ""
        Layout.preferredWidth: 240

        model: delegate.options

        onActivated: (index) => delegate.aspect.value = index
    }

    Item { Layout.fillWidth: true }
}
