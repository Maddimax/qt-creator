// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Layouts
import QtCreator.Ui

AspectPage {
    id: root

    contentFillsHeight: true

    readonly property var shortcut: AspectModels.named(aspects.Shortcut)

    RowLayout {
        spacing: Spacing.GapHM
        Layout.fillWidth: true
        Layout.fillHeight: true

        TreeDelegate {
            id: commands

            aspect: root.aspects.Commands
            Layout.fillHeight: true

            // Which command the box below is about. Where the cursor is is the
            // view's business; what is mapped to what is the aspect's.
            onCurrentIndexChanged: root.aspects.Commands.setCurrentIndex(currentIndex)

            // "Show conflicts" puts the colliding sequence in the filter field,
            // which is how the other commands using it are found.
            Connections {
                target: root.aspects.Commands

                function onFilterTextChanged(text: string): void {
                    commands.setFilter(text)
                }
            }
        }

        ColumnLayout {
            spacing: Spacing.GapVXs
            Layout.alignment: Qt.AlignTop

            ButtonDelegate { aspect: root.aspects.ResetAll; Layout.fillWidth: false }
            ButtonDelegate { aspect: root.aspects.Reset; Layout.fillWidth: false }
            ButtonDelegate { aspect: root.aspects.Import; Layout.fillWidth: false }
            ButtonDelegate { aspect: root.aspects.Export; Layout.fillWidth: false }
        }
    }

    AspectGroupBox {
        title: qsTr("Shortcut")
        enabled: root.aspects.Shortcut.enabled

        AspectInlineListDelegate { aspect: root.shortcut.Keys }
    }
}
