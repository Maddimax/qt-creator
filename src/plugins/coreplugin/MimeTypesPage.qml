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

    AspectGroupBox {
        title: qsTr("Registered MIME Types")
        Layout.fillHeight: true

        ColumnLayout {
            spacing: Spacing.GapVS

            TableDelegate {
                aspect: root.aspects.Types
                onCurrentRowChanged: root.aspects.Types.setCurrentRow(currentRow)
            }

            RowLayout {
                spacing: Spacing.GapHXs

                ButtonDelegate {
                    aspect: root.aspects.ResetMimeTypes
                    Layout.fillWidth: false
                }

                ButtonDelegate {
                    aspect: root.aspects.ResetHandlers
                    Layout.fillWidth: false
                }

                Item { Layout.fillWidth: true }
            }
        }
    }

    AspectGroupBox {
        title: qsTr("Details")
        visible: root.aspects.Details.visible
        Layout.fillHeight: true

        ColumnLayout {
            spacing: Spacing.GapVS

            StringDelegate { aspect: root.details.Patterns }
            TableDelegate { aspect: root.details.Magic }
        }
    }
}
