// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Layouts
import QtCreator.Ui

// The sessions, with what can be done to them beside the list. Which buttons
// are offered depends on what is selected, which the aspects say.
AspectPage {
    id: root

    contentFillsHeight: true

    RowLayout {
        spacing: Spacing.GapHM
        Layout.fillWidth: true
        Layout.fillHeight: true

        TableDelegate {
            id: sessions

            objectName: "sessionTable"
            aspect: root.aspects.Sessions
            Layout.fillWidth: true
            Layout.fillHeight: true

            onCurrentRowChanged: root.aspects.Sessions.setCurrentRow(sessions.currentRow)
            onSelectedRowsChanged: root.aspects.Sessions.setSelectedRows(sessions.selectedRows)
            onRowActivated: (row) => root.aspects.Sessions.activateRow(row)

            Connections {
                target: root.aspects.Sessions

                // The dialog puts the reader on a session it just made, or on
                // the active one when it opens.
                function onSelectRowRequested(row: int): void {
                    sessions.selectRow(row)
                }
            }
        }

        ColumnLayout {
            spacing: Spacing.GapVS
            Layout.alignment: Qt.AlignTop

            ButtonDelegate { aspect: root.aspects.CreateNew }
            ButtonDelegate { aspect: root.aspects.Open }
            ButtonDelegate { aspect: root.aspects.Rename }
            ButtonDelegate { aspect: root.aspects.Clone }
            ButtonDelegate { aspect: root.aspects.Delete }
        }
    }

    BoolDelegate { aspect: root.aspects.AutoLoad }
    TextDisplayDelegate { aspect: root.aspects.WhatIsASession }
}
