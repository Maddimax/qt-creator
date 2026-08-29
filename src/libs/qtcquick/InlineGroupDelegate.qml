// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCreator.Ui

// A nested AspectContainer whose aspects read as one value: one labelled row,
// with no group box around it. The five parts of an ABI are what this is for -
// as a group box they are five settings, and they are not.
RowLayout {
    id: root

    required property Aspect aspect
    readonly property string labelText: aspect?.plainLabelText ?? ""
    readonly property bool aspectVisible: aspect?.visible ?? true
    // Derived rather than taken as a model role, so that a hand-written page
    // can use this delegate with nothing but the aspect.
    readonly property var childModel: aspect ? AspectModels.container(aspect) : null

    visible: aspectVisible
    enabled: aspect?.enabled ?? false
    spacing: Spacing.GapHM
    Layout.fillWidth: true

    FormLabel {
        text: root.labelText
    }

    // Filling, so that a row of form-width controls is shrunk to fit rather
    // than pushing the page wider than it is: what is in it reads as one
    // value, so the parts may be narrower than a form control.
    Loader {
        Layout.fillWidth: true
        // Loaded by URL rather than named as a type: AspectItems instantiates
        // this delegate, and QML cannot resolve two files that refer to each
        // other. setSource() also lets the required model be set at creation.
        Component.onCompleted: setSource("AspectItems.qml",
                                         {"model": root.childModel, "inRow": true})
    }
}
