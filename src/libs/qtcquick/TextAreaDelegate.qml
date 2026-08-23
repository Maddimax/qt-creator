// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCreator.Ui

// A string edited over several lines - a list of commands, an ignore pattern,
// the effective call of a build step. The single-line delegate showed only the
// first line's worth of it.
RowLayout {
    id: delegate

    required property Aspect aspect
    // Not readonly: an aspect can change what it wants drawn - the label on
    // Copilot's sign-in button is its state - and says so with
    // controlConfigurationChanged().
    property var pres: aspect ? AspectModels.presentation(aspect) : ({})
    readonly property string labelText: aspect?.labelText ?? ""
    readonly property string toolTip: aspect?.toolTip ?? ""
    readonly property bool aspectVisible: aspect?.visible ?? true

    Connections {
        target: delegate.aspect
        function onControlConfigurationChanged() {
            delegate.pres = AspectModels.presentation(delegate.aspect)
        }
    }

    visible: aspectVisible
    spacing: Spacing.GapHM
    Layout.fillWidth: true

    Label {
        text: delegate.labelText
        visible: text !== ""
        Layout.preferredWidth: Metrics.formLabelWidth
        Layout.alignment: Qt.AlignTop
        elide: Text.ElideRight
    }

    ScrollView {
        Layout.fillWidth: true
        Layout.preferredHeight: Metrics.formTextAreaHeight

        TextArea {
            id: area

            text: delegate.aspect?.value ?? ""
            placeholderText: delegate.pres.placeholderText ?? ""
            enabled: delegate.aspect?.enabled ?? false
            readOnly: delegate.aspect?.readOnly ?? true
            wrapMode: TextEdit.NoWrap
            ToolTip.text: delegate.toolTip
            ToolTip.visible: hovered && delegate.toolTip !== ""

            // TextArea has no editingFinished, and writing on every keystroke
            // would make one undo step per character.
            onActiveFocusChanged: {
                if (!activeFocus && delegate.aspect && !area.readOnly)
                    delegate.aspect.value = area.text
            }
        }
    }
}
