// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Layouts
import QtCreator.Ui

// A nested AspectContainer with no box of its own: what it holds reads as rows
// of the page around it. An executable and the alternative to it on a device
// are two rows, not a group of two settings - the grouping is the aspect's
// business, not the page's, which is why it is a container at all.
ColumnLayout {
    id: root

    required property Aspect aspect
    readonly property bool aspectVisible: aspect?.visible ?? true
    // Derived rather than taken as a model role, so that a hand-written page
    // can use this delegate with nothing but the aspect.
    readonly property var childModel: aspect ? AspectModels.container(aspect) : null
    // A container that names QML of its own is drawn with it, exactly as a
    // page is. Drawn generically it would lose whatever that file draws.
    readonly property url ownSource: aspect ? AspectModels.qmlSource(aspect) : ""

    function loadContents(loader: Loader): void {
        if (root.ownSource.toString() !== "")
            loader.setSource(root.ownSource, {"aspects": AspectModels.named(root.aspect)})
        else
            loader.setSource("AspectItems.qml", {"model": root.childModel})
    }

    visible: aspectVisible
    enabled: aspect?.enabled ?? false
    spacing: Spacing.GapVM
    Layout.fillWidth: true

    // Loaded by URL rather than named as a type: AspectItems instantiates this
    // delegate, and QML cannot resolve two files that refer to each other.
    // setSource() also lets the required model be set at creation.
    Loader {
        Layout.fillWidth: true
        Component.onCompleted: root.loadContents(this)
    }
}
