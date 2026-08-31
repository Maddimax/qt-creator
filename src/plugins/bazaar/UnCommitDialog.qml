// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Layouts
import QtCreator.Ui

// Taking back the last commit. Dry Run says what would happen without doing
// any of it, so it sits with the settings it would use rather than among the
// buttons that close the dialog.
AspectPage {
    id: root

    BoolWithOwnLabelDelegate { aspect: root.aspects.KeepTags }
    BoolWithOwnLabelDelegate { aspect: root.aspects.Local }
    StringDelegate { aspect: root.aspects.Revision }

    RowLayout {
        Layout.fillWidth: true

        Item { Layout.fillWidth: true }

        ButtonDelegate { aspect: root.aspects.DryRun }
    }
}
