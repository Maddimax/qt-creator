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
            aspect: root.aspects.Kits
            makeDefaultToolTip: qsTr("Set as the default kit to use when creating a new project.")
            Layout.fillWidth: true
            Layout.fillHeight: true
            // What the current item is made of is long, and a layout that
            // gives it what it asks for leaves nothing to pick from.
            Layout.minimumHeight: Metrics.chooserListHeight
        }

        // Where kits come from is the page's business rather than the list's;
        // the list's own Clone, Remove and Make Default sit between Add and
        // the two that choose what a kit is asked.
        ColumnLayout {
            spacing: Spacing.GapVXs
            Layout.alignment: Qt.AlignTop

            ButtonDelegate {
                aspect: root.aspects.Add
                Layout.fillWidth: false
            }

            Item { Layout.preferredHeight: Spacing.GapVL }

            ButtonDelegate {
                aspect: root.aspects.Filter
                Layout.fillWidth: false
            }

            ButtonDelegate {
                aspect: root.aspects.DefaultFilter
                Layout.fillWidth: false
            }
        }
    }

    // What the current kit is called and what it is made of. Hidden when no
    // kit is current, which the container says rather than the page.
    ColumnLayout {
        spacing: Spacing.GapVS
        visible: root.aspects.Details?.visible ?? false
        Layout.fillWidth: true

        // The name and the icon that stands for it read as one thing.
        InlineGroupDelegate { aspect: root.details.NameRow }

        StringDelegate { aspect: root.details.FileSystemName }

        // Whatever the kinds of thing a kit points at ask to be told. The page
        // knows about none of them.
        AspectItems {
            Layout.fillWidth: true
            model: root.details.KitAspects.container
                   ? AspectModels.container(root.details.KitAspects.container)
                   : null
        }
    }
}
