// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Layouts
import QtCreator.Ui

// What stopped a Qt for MCUs kit from being made, one message at a time. The
// buttons that move through them are beside the message, with how far along
// the reader is under them.
AspectPage {
    id: root

    contentFillsHeight: true

    RowLayout {
        spacing: Spacing.GapHM
        Layout.fillWidth: true
        Layout.fillHeight: true

        ColumnLayout {
            spacing: Spacing.GapVS
            Layout.fillWidth: true
            Layout.fillHeight: true

            TextDisplayDelegate { aspect: root.aspects.Headline }
            TextDisplayDelegate { aspect: root.aspects.Information }

            Item { Layout.fillHeight: true }

            TextDisplayDelegate { aspect: root.aspects.QtMcusPath }
        }

        ColumnLayout {
            spacing: Spacing.GapVS
            Layout.alignment: Qt.AlignTop

            ButtonDelegate { aspect: root.aspects.Previous }
            ButtonDelegate { aspect: root.aspects.Next }
            ButtonDelegate { aspect: root.aspects.Fix }
            ButtonDelegate { aspect: root.aspects.Help }
            TextDisplayDelegate { aspect: root.aspects.Counter }
        }
    }
}
