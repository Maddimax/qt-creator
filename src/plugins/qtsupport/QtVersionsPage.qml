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
            aspect: root.aspects.Versions
            Layout.fillWidth: true
            Layout.fillHeight: true
        }

        // Where Qt versions come from is the page's business rather than the
        // list's; the list's own Clone and Remove sit between these and the
        // two that act on the whole installation.
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

            Item { Layout.preferredHeight: Spacing.GapVL }

            ButtonDelegate {
                aspect: root.aspects.LinkWithQt
                Layout.fillWidth: false
            }

            ButtonDelegate {
                aspect: root.aspects.CleanUp
                Layout.fillWidth: false
            }
        }
    }

    ColumnLayout {
        spacing: Spacing.GapVS
        Layout.fillWidth: true

        StringDelegate { aspect: root.details.Name }
        TextWithActionDelegate { aspect: root.details.QmakePath }
        TextDisplayDelegate { aspect: root.details.Error }

        // Whatever else this kind of version has to be told - only a QNX
        // version has anything, and only its SDP path.
        AspectItems {
            Layout.fillWidth: true
            model: root.details.Configuration.container
                   ? AspectModels.container(root.details.Configuration.container)
                   : null
        }

        TextDisplayDelegate { aspect: root.details.Description }
        BoolDelegate { aspect: root.details.ShowDetails }
        TextDisplayDelegate { aspect: root.details.Info }
    }

    SelectionDelegate { aspect: root.aspects.Documentation }
}
