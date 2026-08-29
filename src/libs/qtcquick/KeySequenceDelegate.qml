// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCreator.Ui

// A key sequence, typed in or recorded. The recording is the aspect's: what is
// being recorded is exactly the keys that would otherwise be shortcuts, and no
// control can see those - they never reach it. See the Keyboard page.
RowLayout {
    id: delegate

    // Generic, unlike the other single-aspect delegates: the aspect that drives
    // this one lives in coreplugin, which this library cannot see, so
    // recording() and setRecording() stay unchecked.
    required property Aspect aspect
    readonly property string labelText: aspect?.plainLabelText ?? ""
    readonly property string toolTip: aspect?.toolTip ?? ""
    readonly property bool aspectVisible: aspect?.visible ?? true
    readonly property bool editable: (aspect?.enabled ?? false) && !(aspect?.readOnly ?? false)
    readonly property bool recording: aspect?.recording ?? false

    visible: aspectVisible
    spacing: Spacing.GapHM
    Layout.fillWidth: true

    FormLabel {
        text: delegate.labelText
    }

    QtcLineEdit {
        id: field

        objectName: "keySequenceField"
        text: delegate.aspect?.value ?? ""
        enabled: delegate.editable && !delegate.recording
        placeholderText: qsTr("Enter key sequence as text")
        ToolTip.text: delegate.toolTip
        ToolTip.visible: hovered && delegate.toolTip !== ""
        Layout.fillWidth: true

        onEditingFinished: if (delegate.aspect && text !== delegate.aspect.value)
                               delegate.aspect.value = text
    }

    Button {
        objectName: "keySequenceRecordButton"
        // The width of the wider of the two, so the row does not jump when
        // recording starts.
        text: delegate.recording ? qsTr("Stop Recording") : qsTr("Record")
        checkable: true
        checked: delegate.recording
        enabled: delegate.editable
        ToolTip.text: qsTr("Click and type the new key sequence.")
        ToolTip.visible: hovered
        onClicked: delegate.aspect.setRecording(checked)
    }
}
