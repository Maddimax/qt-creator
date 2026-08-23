// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCreator.Ui

// A nested AspectContainer: its label is the group title and its own aspects
// are laid out inside. This is how a page expresses grouping without a
// Layouting closure.
GroupBox {
    id: root

    required property Aspect aspect
    readonly property string labelText: aspect?.labelText ?? ""
    readonly property bool aspectVisible: aspect?.visible ?? true
    // Derived rather than taken as a model role, so that a hand-written page
    // can use this delegate with nothing but the aspect.
    readonly property var childModel: aspect ? AspectModels.container(aspect) : null

    title: labelText
    visible: aspectVisible
    enabled: aspect?.enabled ?? false
    Layout.fillWidth: true

    ColumnLayout {
        // Loaded by URL rather than named as a type: AspectItems instantiates
        // this delegate, and QML cannot resolve two files that refer to each
        // other. setSource() also lets the required model be set at creation.
        Loader {
            Layout.fillWidth: true
            Component.onCompleted: setSource("AspectItems.qml", {"model": root.childModel})
        }
    }
}
