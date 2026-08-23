// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCreator.Ui

// A group whose title is optionally the check box of a BoolAspect, which greys
// out the contents when unchecked. Qt Quick's GroupBox has no checkable
// property, unlike QGroupBox, so the check box goes in the label.
GroupBox {
    id: root

    // When set, the group is checkable and this aspect is what it checks.
    property Aspect checkAspect: null

    readonly property bool checked: !checkAspect || checkAspect.value === true

    Layout.fillWidth: true

    label: CheckBox {
        id: check

        visible: root.checkAspect !== null
        text: root.title
        checked: root.checked
        enabled: root.checkAspect?.enabled ?? true
        onToggled: if (root.checkAspect) root.checkAspect.value = checked
    }

    // Only the contents grey out; the check box has to stay usable.
    contentItem.enabled: root.checked
}
