// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Layouts
import QtCreator.Ui

// Build output to run a kit's parsers over. The buttons sit beside the text
// rather than under it, which is where the widget dialog put them.
AspectPage {
    id: root

    AspectGroupBox {
        title: qsTr("Build Output")

        RowLayout {
            spacing: Spacing.GapHM

            TextAreaDelegate {
                aspect: root.aspects.Output
                Layout.fillWidth: true
                Layout.fillHeight: true
            }

            ColumnLayout {
                spacing: Spacing.GapVS
                Layout.alignment: Qt.AlignTop

                ButtonDelegate { aspect: root.aspects.LoadFile }
                BoolDelegate { aspect: root.aspects.Stderr }
            }
        }
    }

    AspectGroupBox {
        title: qsTr("Parsing Options")

        InlineGroupDelegate { aspect: root.aspects.Kit }
        BoolDelegate { aspect: root.aspects.ClearTasks }
    }
}
