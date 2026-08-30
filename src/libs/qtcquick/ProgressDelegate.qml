// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCreator.Ui

// How far along something running is. Read, never set: there is nothing here
// for the reader to change.
RowLayout {
    id: delegate

    required property Aspect aspect
    property var pres: aspect ? AspectModels.presentation(aspect) : ({})
    readonly property string labelText: aspect?.plainLabelText ?? ""
    readonly property bool aspectVisible: aspect?.visible ?? true

    Connections {
        target: delegate.aspect
        function onControlConfigurationChanged(): void {
            delegate.pres = AspectModels.presentation(delegate.aspect)
        }
    }

    visible: aspectVisible
    spacing: Spacing.GapHM
    Layout.fillWidth: true

    FormLabel { text: delegate.labelText }

    ProgressBar {
        objectName: "progressBar"

        from: delegate.pres.minimum ?? 0
        to: delegate.pres.maximum ?? 100
        value: delegate.aspect?.value ?? 0
        // Work that cannot say how much there is runs on the spot instead.
        indeterminate: (delegate.pres.maximum ?? 100) === 0
        ToolTip.text: delegate.aspect?.toolTip ?? ""
        ToolTip.visible: hovered && ToolTip.text !== ""
        Layout.fillWidth: true
    }
}
