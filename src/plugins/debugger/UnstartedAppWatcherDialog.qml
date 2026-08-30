// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Layouts
import QtCreator.Ui

// An executable to watch for, and what to do once it starts. What may be
// changed depends on whether the dialog is watching, which the aspects say.
AspectPage {
    id: root

    InlineGroupDelegate { aspect: root.aspects.Kit }

    RowLayout {
        spacing: Spacing.GapHM
        Layout.fillWidth: true

        StringDelegate {
            aspect: root.aspects.Executable
            Layout.fillWidth: true
        }

        ButtonDelegate { aspect: root.aspects.Reset }
    }

    BoolDelegate { aspect: root.aspects.HideOnAttach }
    BoolDelegate { aspect: root.aspects.ContinueOnAttach }
    TextDisplayDelegate { aspect: root.aspects.Waiting }
}
