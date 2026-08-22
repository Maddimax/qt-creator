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
    required property string labelText
    required property string toolTip
    required property bool aspectVisible

    text: labelText
    visible: aspectVisible
    enabled: aspect.enabled && !aspect.readOnly
    checked: aspect.value === true
    ToolTip.text: toolTip
    ToolTip.visible: hovered && toolTip !== ""
    Layout.fillWidth: true

    onToggled: aspect.value = checked
}
