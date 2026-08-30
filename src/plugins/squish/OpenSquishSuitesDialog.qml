// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Layouts
import QtCreator.Ui

// Choosing which test suites under a directory to open. The list is whatever
// is found there, so it changes under the reader as they pick a directory -
// and everything found starts chosen, because asking for the directory is
// what asked for its suites.
AspectPage {
    id: root

    contentFillsHeight: true

    StringDelegate { aspect: root.aspects.Directory }

    RowLayout {
        spacing: Spacing.GapHM
        Layout.fillWidth: true
        Layout.fillHeight: true

        TableDelegate {
            objectName: "suitesTable"

            aspect: root.aspects.Suites
            Layout.fillHeight: true
        }

        ColumnLayout {
            spacing: Spacing.GapVS
            Layout.alignment: Qt.AlignTop

            ButtonDelegate { aspect: root.aspects.SelectAll }
            ButtonDelegate { aspect: root.aspects.DeselectAll }
        }
    }
}
