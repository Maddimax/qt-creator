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
// with nothing.
RowLayout {
    id: delegate

    required property Aspect aspect
    readonly property string labelText: aspect?.labelText ?? ""
    readonly property string toolTip: aspect?.toolTip ?? ""
    readonly property bool aspectVisible: aspect?.visible ?? true

    // Whether the value has arrived. displayText is empty until it does, and
    // an empty secret is a legitimate answer, so this follows the signal
    // rather than the text.
    property bool arrived: false

    visible: aspectVisible
    spacing: Spacing.GapHM
    Layout.fillWidth: true

    Component.onCompleted: delegate.aspect?.requestDisplayText()

    Connections {
        target: delegate.aspect
        function onDisplayTextChanged() {
            delegate.arrived = true
            field.text = delegate.aspect?.displayText ?? ""
        }
    }

    Label {
        text: delegate.labelText
        Layout.preferredWidth: Metrics.formLabelWidth
        // An aspect with no label of its own reserves no room for one.
        visible: text !== ""
        elide: Text.ElideRight
    }

    TextField {
        id: field

        echoMode: reveal.checked ? TextInput.Normal : TextInput.Password
        enabled: (delegate.aspect?.enabled ?? false) && delegate.arrived
        readOnly: (delegate.aspect?.readOnly ?? true) || !delegate.arrived
        ToolTip.text: delegate.toolTip
        ToolTip.visible: hovered && delegate.toolTip !== ""
        Layout.fillWidth: true

        onEditingFinished: if (delegate.aspect && !field.readOnly) delegate.aspect.value = field.text
    }

    QtcSwitch {
        id: reveal

        text: qsTr("Show")
        enabled: delegate.arrived
    }
}
