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

    RowLayout {
        spacing: Spacing.GapHM
        Layout.fillWidth: true
        Layout.fillHeight: true

        TreeDelegate {
            id: commands

            aspect: root.aspects.Commands
            Layout.fillHeight: true

            // Which command the expression below is about. Where the cursor
            // is is the view's business; the aspect keeps the rest.
            onCurrentIndexChanged: root.aspects.Commands.setCurrentIndex(currentIndex)
        }

        ColumnLayout {
            spacing: Spacing.GapVXs
            Layout.alignment: Qt.AlignTop

            ButtonDelegate { aspect: root.aspects.ResetAll; Layout.fillWidth: false }
        }
    }

    AspectGroupBox {
        title: qsTr("Ex Command")

        RowLayout {
            spacing: Spacing.GapHM
            Layout.fillWidth: true

            StringDelegate { aspect: root.aspects.Regex }

            ButtonDelegate {
                aspect: root.aspects.Reset
                Layout.fillWidth: false
            }
        }
    }
}
