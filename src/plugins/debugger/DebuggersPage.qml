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

    readonly property var details: AspectModels.named(aspects.Details)

    SelectionDelegate { aspect: root.aspects.Device }

    RowLayout {
        spacing: Spacing.GapHM
        Layout.fillWidth: true
        Layout.fillHeight: true

        GroupedListDelegate {
            aspect: root.aspects.Debuggers
            Layout.fillWidth: true
            Layout.fillHeight: true
        }

        // What a new debugger is made of, and where to look for more of them,
        // is this page's business rather than the list's; the list's own Clone
        // and Remove sit between these two.
        ColumnLayout {
            spacing: Spacing.GapVXs
            Layout.alignment: Qt.AlignTop

            ButtonDelegate {
                aspect: root.aspects.Add
                Layout.fillWidth: false
            }

            ButtonDelegate {
                aspect: root.aspects.Redetect
                Layout.fillWidth: false
            }
        }
    }

    AspectGroupBox {
        title: qsTr("Debugger")
        visible: root.aspects.Details.visible

        ColumnLayout {
            spacing: Spacing.GapVS

            StringDelegate { aspect: root.details.DisplayName }
            StringDelegate { aspect: root.details.Binary }
            // Only CDB has anything to say here, and it says it with a link.
            TextDisplayDelegate { aspect: root.details.CdbHint }
            TextDisplayDelegate { aspect: root.details.Type }
            TextDisplayDelegate { aspect: root.details.Abis }
            TextDisplayDelegate { aspect: root.details.Version }
            StringDelegate { aspect: root.details.WorkingDirectory }
        }
    }
}
