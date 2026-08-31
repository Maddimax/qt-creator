// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCreator.Ui

// A setting turned on and off from a toolbar rather than from a form: a button
// carrying an icon, held down while it is on. Which state it is in is what the
// icon, the text and the tool tip say, so all three come from the descriptor
// and are re-read when the aspect says they changed. See Utils::ToggleAspect.
Button {
    id: delegate

    required property Aspect aspect
    // Not readonly: the whole point of this control is that what it draws
    // depends on the value, and the aspect says so with
    // controlConfigurationChanged().
    property var pres: aspect ? AspectModels.presentation(aspect) : ({})
    readonly property bool aspectVisible: aspect?.visible ?? true

    Connections {
        target: delegate.aspect
        function onControlConfigurationChanged(): void {
            delegate.pres = AspectModels.presentation(delegate.aspect)
        }
    }

    checkable: true
    checked: (aspect?.value ?? false) === true
    text: pres.actionText ?? ""
    icon.source: pres.actionIcon ?? ""
    // The aspect's own tool tip where the state does not name one, which is
    // what BaseAspect::presentation() has already put there.
    ToolTip.text: pres.toolTip ?? ""
    ToolTip.visible: hovered && ToolTip.text !== ""
    visible: aspectVisible
    enabled: (aspect?.enabled ?? false) && !(aspect?.readOnly ?? false)
    // A toolbar button takes the width it needs and sits at the left, the same
    // as ButtonDelegate's.
    Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter

    onToggled: if (aspect) aspect.value = checked
}
