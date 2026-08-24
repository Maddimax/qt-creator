// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Layouts
import QtCreator.Ui
import QtCreator.TextEditor

// Nim code style. The language has nothing to set beyond the tab settings, so
// the page is the selector, those, and the preview.
AspectPage {
    id: root

    contentFillsHeight: true

    readonly property var settings: AspectModels.named(aspects.Settings)
    readonly property var tabs: AspectModels.named(settings.TabSettings)

    CodeStyleSelector { aspects: root.aspects }

    RowLayout {
        spacing: Spacing.GapHM
        Layout.fillWidth: true
        Layout.fillHeight: true

        ColumnLayout {
            spacing: Spacing.GapVS
            Layout.fillWidth: true
            Layout.fillHeight: true
            // An even split, asked for rather than worked out from what is in
            // the two columns; see QmlJSCodeStylePage.
            Layout.preferredWidth: 1

            AspectGroupBox {
                title: qsTr("Tabs And Indentation")

                ColumnLayout {
                    spacing: Spacing.GapVS

                    TextDisplayDelegate { aspect: root.tabs.CodingStyleWarning }
                    BoolDelegate { aspect: root.tabs.AutoDetect }
                    SelectionDelegate { aspect: root.tabs.TabPolicy }
                    IntegerDelegate { aspect: root.tabs.IndentSize }
                    IntegerDelegate { aspect: root.tabs.TabSize }
                    SelectionDelegate { aspect: root.tabs.ContinuationAlignBehavior }
                }
            }

            Item { Layout.fillHeight: true }
        }

        AspectGroupBox {
            title: qsTr("Preview")
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredWidth: 1

            CodeStylePreview { aspects: root.aspects }
        }
    }
}
