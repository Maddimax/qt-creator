// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCreator.Ui

AspectPage {
    id: root

    contentFillsHeight: true

    AspectGroupBox {
        title: qsTr("Registered Documentation")
        Layout.fillHeight: true

        RowLayout {
            spacing: Spacing.GapHM

            TableDelegate {
                aspect: root.aspects.Docs
                Layout.fillHeight: true
            }

            ColumnLayout {
                spacing: Spacing.GapVS
                Layout.alignment: Qt.AlignTop

                ButtonDelegate { aspect: root.aspects.AddDocumentation }
            }
        }
    }
}
