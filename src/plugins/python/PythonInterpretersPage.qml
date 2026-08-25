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

    RowLayout {
        spacing: Spacing.GapHM
        Layout.fillWidth: true
        Layout.fillHeight: true

        TableDelegate {
            aspect: root.aspects.Interpreters
            onCurrentRowChanged: root.aspects.Interpreters.setCurrentRow(currentRow)
            Layout.fillHeight: true
        }

        // What a new interpreter is made of is the form below, so Add is this
        // page's rather than the list's.
        ColumnLayout {
            spacing: Spacing.GapVXs
            Layout.alignment: Qt.AlignTop

            ButtonDelegate { aspect: root.aspects.Add; Layout.fillWidth: false }
            ButtonDelegate { aspect: root.aspects.Remove; Layout.fillWidth: false }
            ButtonDelegate { aspect: root.aspects.MakeDefault; Layout.fillWidth: false }
            ButtonDelegate { aspect: root.aspects.GenerateKit; Layout.fillWidth: false }
            ButtonDelegate { aspect: root.aspects.CleanUp; Layout.fillWidth: false }
        }
    }

    AspectGroupBox {
        title: qsTr("Interpreter")
        visible: root.aspects.Details.visible

        ColumnLayout {
            spacing: Spacing.GapVS

            StringDelegate { aspect: root.details.Name }
            StringDelegate { aspect: root.details.Executable }
        }
    }
}
