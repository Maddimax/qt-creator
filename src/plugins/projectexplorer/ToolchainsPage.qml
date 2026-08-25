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

    SelectionDelegate { aspect: root.aspects.Device }

    RowLayout {
        spacing: Spacing.GapHM
        Layout.fillWidth: true
        Layout.fillHeight: true

        GroupedListDelegate {
            aspect: root.aspects.Toolchains
            Layout.fillWidth: true
            Layout.fillHeight: true
        }

        // Which kinds of toolchain there are is the page's business rather than
        // the list's; the list's own Clone and Remove sit between Add and the
        // three that act on all of them.
        ColumnLayout {
            spacing: Spacing.GapVXs
            Layout.alignment: Qt.AlignTop

            ButtonDelegate {
                aspect: root.aspects.Add
                Layout.fillWidth: false
            }

            Item { Layout.preferredHeight: Spacing.GapVL }

            ButtonDelegate {
                aspect: root.aspects.RemoveAll
                Layout.fillWidth: false
            }

            ButtonDelegate {
                aspect: root.aspects.Redetect
                Layout.fillWidth: false
            }

            ButtonDelegate {
                aspect: root.aspects.DetectionSettings
                Layout.fillWidth: false
            }
        }
    }

    // What the kind of toolchain being looked at asks for. Every kind answers
    // with aspects, so there is nothing here the page has to know about them.
    AspectItems {
        Layout.fillWidth: true
        model: root.aspects.Configuration.container
               ? AspectModels.container(root.aspects.Configuration.container)
               : null
    }
}
