// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCreator.Ui

// The changes on a Gerrit server, and what the one being looked at says. The
// split is the reader's to move, as the widget dialog's was.
AspectPage {
    id: root

    contentFillsHeight: true

    RowLayout {
        spacing: Spacing.GapHM
        Layout.fillWidth: true

        TextDisplayDelegate { aspect: root.aspects.Repository }
        Item { Layout.fillWidth: true }
        InlineGroupDelegate { aspect: root.aspects.Remote }
    }

    StringDelegate { aspect: root.aspects.Query }

    SplitView {
        orientation: Qt.Vertical
        Layout.fillWidth: true
        Layout.fillHeight: true

        AspectGroupBox {
            title: qsTr("Changes")
            SplitView.fillHeight: true
            SplitView.minimumHeight: Metrics.formListHeight

            // The indicator sits over the tree rather than in the column, so
            // it needs a parent that is not a layout: anchors inside one are
            // undefined behaviour and Qt says so at load time.
            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true

                TreeDelegate {
                    id: changes

                    objectName: "changeTree"
                    anchors.fill: parent
                    aspect: root.aspects.Changes
                }

                // The list is fetched from the server, which takes a while.
                BusyIndicator {
                    objectName: "fetchingChanges"
                    anchors.centerIn: parent
                    running: root.aspects.Fetching.value ?? false
                    visible: running
                }
            }
        }

        AspectGroupBox {
            title: qsTr("Details")
            SplitView.minimumHeight: Metrics.formTextAreaHeight

            TextDisplayDelegate { aspect: root.aspects.Details }
        }
    }

    RowLayout {
        spacing: Spacing.GapHXs
        Layout.fillWidth: true

        Item { Layout.fillWidth: true }

        ButtonDelegate { aspect: root.aspects.Display }
        ButtonDelegate { aspect: root.aspects.CherryPick }
        ButtonDelegate { aspect: root.aspects.Checkout }
        ButtonDelegate { aspect: root.aspects.Refresh }
    }
}
