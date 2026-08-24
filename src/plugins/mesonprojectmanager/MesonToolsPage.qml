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

        GroupedListDelegate {
            aspect: root.aspects.Tools
            Layout.fillWidth: true
            Layout.fillHeight: true
        }

        // Above the list's own Clone and Remove, as in the widget page: what a
        // new tool is made of is this page's business, not the list's.
        ColumnLayout {
            spacing: Spacing.GapVXs
            Layout.alignment: Qt.AlignTop

            ButtonDelegate {
                aspect: root.aspects.Add
                Layout.fillWidth: false
            }
        }
    }

    AspectGroupBox {
        title: qsTr("Tool")
        visible: root.aspects.Details.visible

        ColumnLayout {
            spacing: Spacing.GapVS

            StringDelegate { aspect: root.details.Name }
            StringDelegate { aspect: root.details.Executable }
        }
    }
}
