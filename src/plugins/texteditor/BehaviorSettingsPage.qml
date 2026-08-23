// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCreator.Ui

// Five settings objects share this page, so their aspects are reached through
// AspectModels.named() rather than through the page's own "aspects".
AspectPage {
    id: root

    readonly property var tabs: AspectModels.named(aspects.Tabs)
    readonly property var typing: AspectModels.named(aspects.Typing)
    readonly property var storage: AspectModels.named(aspects.Storage)
    readonly property var encoding: AspectModels.named(aspects.Encoding)
    readonly property var behavior: AspectModels.named(aspects.Behavior)

    AspectGroupBox {
        title: qsTr("Tabs And Indentation")

        ColumnLayout {
            TextDisplayDelegate { aspect: root.tabs.CodingStyleWarning }
            BoolDelegate { aspect: root.tabs.AutoDetect }
            SelectionDelegate { aspect: root.tabs.TabPolicy }
            IntegerDelegate { aspect: root.tabs.IndentSize }
            IntegerDelegate { aspect: root.tabs.TabSize }
            SelectionDelegate { aspect: root.tabs.ContinuationAlignBehavior }
        }
    }

    AspectGroupBox {
        title: qsTr("Typing")

        ColumnLayout {
            BoolDelegate { aspect: root.typing.AutoIndent }
            SelectionDelegate { aspect: root.typing.SmartBackspaceBehavior }
            SelectionDelegate { aspect: root.typing.TabKeyBehavior }
            BoolDelegate { aspect: root.typing.PreferSingleLineComments }
            SelectionDelegate { aspect: root.typing.PreferAfterWhitespaceComments }
        }
    }

    AspectGroupBox {
        title: qsTr("Cleanups Upon Saving")

        ColumnLayout {
            BoolDelegate { aspect: root.storage.cleanWhitespace }

            BoolDelegate {
                aspect: root.storage.inEntireDocument
                Layout.leftMargin: Spacing.PaddingHL
            }

            BoolDelegate {
                aspect: root.storage.cleanIndentation
                Layout.leftMargin: Spacing.PaddingHL
            }

            RowLayout {
                Layout.leftMargin: Spacing.PaddingHL

                BoolDelegate { aspect: root.storage.skipTrailingWhitespace }
                StringDelegate { aspect: root.storage.ignoreFileTypes }
            }

            BoolDelegate { aspect: root.storage.addFinalNewLine }
        }
    }

    AspectGroupBox {
        title: qsTr("File Encodings")

        ColumnLayout {
            SelectionDelegate { aspect: root.encoding.DefaultFileEncoding }
            SelectionDelegate { aspect: root.encoding.Utf8BomBehavior }
            SelectionDelegate { aspect: root.encoding.LineEndingBehavior }
        }
    }

    AspectGroupBox {
        title: qsTr("Mouse and Keyboard")

        ColumnLayout {
            BoolDelegate { aspect: root.behavior.MouseHiding }
            BoolDelegate { aspect: root.behavior.MouseNavigation }
            BoolDelegate { aspect: root.behavior.ScrollWheelZooming }
            BoolDelegate { aspect: root.behavior.CamelCaseNavigation }
            BoolDelegate { aspect: root.behavior.SmartSelectionChanging }
            BoolDelegate { aspect: root.behavior.KeyboardTooltips }
            SelectionDelegate { aspect: root.behavior.ConstrainTooltips }
        }
    }
}
