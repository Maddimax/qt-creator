// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCreator.Ui

CheckBox {
    required property Aspect aspect
    readonly property string labelText: aspect?.labelText ?? ""
    readonly property string toolTip: aspect?.toolTip ?? ""
    readonly property bool aspectVisible: aspect?.visible ?? true

    text: labelText
    visible: aspectVisible
    enabled: (aspect?.enabled ?? false) && !(aspect?.readOnly ?? false)
    checked: (aspect?.value ?? false) === true
    ToolTip.text: toolTip
    ToolTip.visible: hovered && toolTip !== ""
    Layout.fillWidth: true

    onToggled: if (aspect) aspect.value = checked
}
