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
            id: clients

            aspect: root.aspects.Clients
            Layout.fillWidth: true
            Layout.fillHeight: true
            // What the current client is made of is long, and a layout that
            // gives it what it asks for leaves nothing to pick from.
            Layout.minimumHeight: Metrics.chooserListHeight

            // Which client the form below is about. Where the cursor is is the
            // view's business; what the client holds is the aspect's.
            onCurrentIndexChanged: root.aspects.Clients.setCurrentIndex(currentIndex)
        }

        // What a new entry is is the page's business, and there is one kind
        // per language server that can be added by hand.
        ColumnLayout {
            spacing: Spacing.GapVXs
            Layout.alignment: Qt.AlignTop

            ButtonDelegate {
                aspect: root.aspects.Add
                Layout.fillWidth: false
            }

            ButtonDelegate {
                aspect: root.aspects.Delete
                Layout.fillWidth: false
            }
        }
    }

    // What the selected client asks for. Every kind answers with aspects, so
    // there is nothing here the page has to know about them.
    AspectItems {
        Layout.fillWidth: true
        model: root.aspects.Current.container
               ? AspectModels.container(root.aspects.Current.container)
               : null
    }
}
