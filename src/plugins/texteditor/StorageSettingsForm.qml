// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Layouts
import QtCreator.Ui

// What is tidied up when a file is saved. The global page and a project's Editor panel show
// the same things and differ only in whose container they are shown for.
AspectGroupBox {
    id: root

    // A NamedAspects for whichever container is being shown.
    required property var aspects

    title: qsTr("Cleanups Upon Saving")
    toolTip: qsTr("Cleanup actions which are automatically performed right before the file is saved to disk.")

    ColumnLayout {
        BoolDelegate { aspect: root.aspects.cleanWhitespace }

        BoolDelegate {
            aspect: root.aspects.inEntireDocument
            Layout.leftMargin: Spacing.PaddingHL
        }

        BoolDelegate {
            aspect: root.aspects.cleanIndentation
            Layout.leftMargin: Spacing.PaddingHL
        }

        RowLayout {
            Layout.leftMargin: Spacing.PaddingHL

            BoolDelegate { aspect: root.aspects.skipTrailingWhitespace }
            StringDelegate { aspect: root.aspects.ignoreFileTypes }
        }

        BoolDelegate { aspect: root.aspects.addFinalNewLine }
    }
}
