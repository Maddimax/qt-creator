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

    SelectionDelegate { aspect: root.aspects.Kit }
    TextDisplayDelegate { aspect: root.aspects.Profile }

    RowLayout {
        spacing: Spacing.GapHM
        Layout.fillWidth: true
        Layout.fillHeight: true

        TreeDelegate {
            id: properties

            aspect: root.aspects.Properties
            Layout.fillWidth: true
            Layout.fillHeight: true
        }

        // Opening and closing branches is the view's business, so these are
        // buttons rather than aspects: there is nothing about them to store.
        ColumnLayout {
            spacing: Spacing.GapVXs
            Layout.alignment: Qt.AlignTop

            Button {
                text: qsTr("E&xpand All")
                onClicked: properties.expandAll()
            }

            Button {
                text: qsTr("&Collapse All")
                onClicked: properties.collapseAll()
            }
        }
    }
}
