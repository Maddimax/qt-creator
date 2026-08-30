// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Layouts
import QtCreator.Ui

// The licence question, when one is asked, and how far the SDK manager has
// got. The output above it is the dialog's, not this form's.
AspectPage {
    id: root

    RowLayout {
        spacing: Spacing.GapHM
        Layout.fillWidth: true

        Item { Layout.fillWidth: true }

        TextDisplayDelegate { aspect: root.aspects.Question }
        ButtonDelegate { aspect: root.aspects.No }
        ButtonDelegate { aspect: root.aspects.Yes }
    }

    ProgressDelegate { aspect: root.aspects.Progress }
}
