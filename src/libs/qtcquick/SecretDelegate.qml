// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCreator.Ui

// A value the aspect does not keep: a secret out of the keychain. It is asked
// for once the field exists and arrives later, so the field stays read-only
// until it does - typing into it before then would overwrite what is stored
// with nothing. The aspect is what knows: it starts read-only and stays that
// way if the secret could not be read at all.
RowLayout {
    id: delegate

    required property Aspect aspect
    readonly property string labelText: aspect?.plainLabelText ?? ""
    readonly property string toolTip: aspect?.toolTip ?? ""
    readonly property bool aspectVisible: aspect?.visible ?? true
    readonly property bool editable: !(aspect?.readOnly ?? true)

    visible: aspectVisible
    spacing: Spacing.GapHM
    Layout.fillWidth: true

    Connections {
        target: delegate.aspect
        function onDisplayTextChanged() {
            field.text = delegate.aspect?.displayText ?? ""
        }
    }

    FormLabel {
        text: delegate.labelText
    }

    TextField {
        id: field

        echoMode: reveal.checked ? TextInput.Normal : TextInput.Password
        enabled: delegate.aspect?.enabled ?? false
        readOnly: !delegate.editable
        // Why it cannot be read, where it could not be.
        placeholderText: delegate.aspect?.placeholderText ?? ""
        ToolTip.text: delegate.toolTip
        ToolTip.visible: hovered && delegate.toolTip !== ""
        Layout.fillWidth: true

        onEditingFinished: if (delegate.aspect && !field.readOnly) delegate.aspect.value = field.text
    }

    QtcSwitch {
        id: reveal

        text: qsTr("Show")
        enabled: delegate.editable
    }
}
