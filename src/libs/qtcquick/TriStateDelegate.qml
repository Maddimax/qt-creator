// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCreator.Ui

// On, off, or neither: what a setting says when nothing has been decided about
// it - a project leaving it to the global settings, a plugin the language
// server has not been told about. See Utils::TriStateAspect.
CheckBox {
    id: delegate

    required property Aspect aspect
    readonly property string labelText: aspect?.plainLabelText ?? ""
    readonly property string toolTip: aspect?.toolTip ?? ""
    readonly property bool aspectVisible: aspect?.visible ?? true

    // TriState::Value, in its own order: enabled, disabled, neither.
    readonly property int state_: aspect?.value ?? 2

    text: labelText
    visible: aspectVisible
    enabled: (aspect?.enabled ?? false) && !(aspect?.readOnly ?? false)
    // Shown, not cycled to: "neither" is what the setting says before anything
    // has been decided, and a click is a decision.
    tristate: true
    checkState: delegate.state_ === 0 ? Qt.Checked
              : delegate.state_ === 1 ? Qt.Unchecked
                                      : Qt.PartiallyChecked
    ToolTip.text: toolTip
    ToolTip.visible: hovered && toolTip !== ""
    Layout.fillWidth: true

    nextCheckState: function() {
        return checkState === Qt.Checked ? Qt.Unchecked : Qt.Checked
    }

    onToggled: if (aspect) aspect.value = checkState === Qt.Checked ? 0 : 1
}
