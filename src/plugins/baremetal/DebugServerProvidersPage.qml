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

    readonly property var providers: AspectModels.named(aspects.Providers)

    AspectGroupBox {
        title: root.aspects.Providers?.plainLabelText ?? ""
        Layout.fillHeight: true

        RowLayout {
            spacing: Spacing.GapHM
            Layout.fillWidth: true
            Layout.fillHeight: true

            ColumnLayout {
                spacing: Spacing.GapVXs
                Layout.fillHeight: true

                TreeDelegate {
                    id: list

                    aspect: root.providers.List
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    // What the current provider is made of is long, and a
                    // layout that gives it what it asks for leaves nothing to
                    // pick from.
                    Layout.minimumHeight: Metrics.chooserListHeight

                    // Which provider the form beside this is about. Where the
                    // cursor is is the view's business; what the provider
                    // holds is the aspect's.
                    onCurrentIndexChanged: root.providers.List.setCurrentIndex(currentIndex)
                }

                // What a new entry is is the page's business, and there is one
                // kind per debug server that can be added.
                RowLayout {
                    spacing: Spacing.GapHXs
                    Layout.fillWidth: true

                    ButtonDelegate {
                        aspect: root.providers.Add
                        Layout.fillWidth: false
                    }

                    ButtonDelegate {
                        aspect: root.providers.Clone
                        Layout.fillWidth: false
                    }

                    ButtonDelegate {
                        aspect: root.providers.Remove
                        Layout.fillWidth: false
                    }

                    Item { Layout.fillWidth: true }
                }
            }

            // What the selected provider asks for. Every kind answers with
            // aspects, so there is nothing here the page has to know about
            // them.
            AspectItems {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
                model: root.providers.Current.container
                       ? AspectModels.container(root.providers.Current.container)
                       : null
            }
        }
    }
}
