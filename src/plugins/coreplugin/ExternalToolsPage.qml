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

        ColumnLayout {
            spacing: Spacing.GapVXs
            Layout.fillHeight: true

            TreeDelegate {
                id: tools

                aspect: root.aspects.Tools
                Layout.fillHeight: true

                // Which tool the form beside this is about. Where the cursor
                // is is the view's business; what the tool holds is the
                // aspect's.
                onCurrentIndexChanged: root.aspects.Tools.setCurrentIndex(currentIndex)

                Component.onCompleted: tools.expandAll()
            }

            // What a new entry is is the page's business, and there are two
            // kinds of them.
            RowLayout {
                spacing: Spacing.GapHXs
                Layout.fillWidth: true

                ButtonDelegate { aspect: root.aspects.AddTool; Layout.fillWidth: false }
                ButtonDelegate { aspect: root.aspects.AddCategory; Layout.fillWidth: false }
                ButtonDelegate { aspect: root.aspects.Remove; Layout.fillWidth: false }

                Item { Layout.fillWidth: true }

                ButtonDelegate { aspect: root.aspects.Revert; Layout.fillWidth: false }
            }
        }

        ColumnLayout {
            spacing: Spacing.GapVS
            enabled: root.aspects.Details.enabled
            Layout.fillWidth: true
            Layout.fillHeight: true

            StringDelegate { aspect: root.details.Description }
            StringDelegate { aspect: root.details.Executable }
            StringDelegate { aspect: root.details.Arguments }
            StringDelegate { aspect: root.details.WorkingDirectory }
            SelectionDelegate { aspect: root.details.Output }
            SelectionDelegate { aspect: root.details.ErrorOutput }
            SelectionDelegate { aspect: root.details.BaseEnvironment }
            TextWithActionDelegate { aspect: root.details.Environment }
            BoolDelegate { aspect: root.details.ModifiesDocument }

            TextAreaDelegate {
                aspect: root.details.Input
                Layout.fillHeight: true
            }
        }
    }
}
