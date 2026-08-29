// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCreator.Ui

// The name in front of a control. Every delegate that has one draws it the
// same way, in the form's label column, so that the labels down a page line
// up whatever they belong to.
Label {
    id: root

    // Where the label belongs beside its own control rather than in that
    // column: a row that begins with a check box is a continuation of it, not
    // a row of the form. See BoolDelegate.
    property bool compact: false

    Layout.preferredWidth: root.compact ? implicitWidth : Metrics.formLabelWidth
    // An aspect with no label of its own reserves no room for one.
    visible: text !== ""
    elide: Text.ElideRight

    // The column is a fixed width, so a label longer than it is cut off - the
    // widget form grew its column to the widest label instead. What was cut
    // off is what this says: the QML Profiler page offers "Report items built
    // in a handler ab...".
    ToolTip.text: root.text
    ToolTip.visible: labelHover.hovered && root.truncated

    HoverHandler { id: labelHover }
}
