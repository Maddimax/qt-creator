// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Layouts
import QtCreator.Ui

// The changes p4 has pending, one of which is submitted. A list rather than a
// drop-down because that is what this dialog has always shown, and because the
// descriptions are long enough to want the room.
AspectPage {
    id: root

    readonly property Aspect changes: root.aspects.Changes

    contentFillsHeight: true

    TableDelegate {
        id: list

        aspect: root.changes
        Layout.fillHeight: true
    }

    // Which row is current is the aspect's, so that the dialog can read the
    // change number without asking the form anything.
    Binding {
        target: root.changes
        property: "currentRow"
        value: list.currentRow
    }
}
