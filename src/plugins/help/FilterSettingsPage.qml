// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Layouts
import QtCreator.Ui

AspectPage {
    id: root

    // Named so that the page census counts the aspect as drawn, and so that a
    // misspelling here is caught rather than showing an empty page.
    readonly property Aspect filters: root.aspects.Filters

    contentFillsHeight: true

    AspectGroupBox {
        title: qsTr("Filters")
        Layout.fillHeight: true

        RowLayout {
            spacing: Spacing.GapHM

            TableDelegate {
                id: filterList

                objectName: "filterList"
                aspect: root.filters
                Layout.fillHeight: true
                Layout.preferredWidth: 1
                Layout.fillWidth: true
            }

            // A filter's components and versions only mean anything next to
            // the name they belong to, so both lists follow the name list's
            // current row rather than having a selection of their own.
            Binding {
                target: root.filters
                property: "currentFilter"
                value: filterList.currentRow
            }

            // The two lists a filter is made of. They are on the Help
            // plugin's own aspect, which the page is handed as a BaseAspect.
            FilterOptionList {
                objectName: "componentList"
                title: qsTr("Components")
                // qmllint disable missing-property
                options: root.filters?.components ?? null
                // qmllint enable missing-property
                Layout.fillHeight: true
                Layout.preferredWidth: 1
                Layout.fillWidth: true
            }

            FilterOptionList {
                objectName: "versionList"
                title: qsTr("Versions")
                // qmllint disable missing-property
                options: root.filters?.versions ?? null
                // qmllint enable missing-property
                Layout.fillHeight: true
                Layout.preferredWidth: 1
                Layout.fillWidth: true
            }
        }
    }
}
