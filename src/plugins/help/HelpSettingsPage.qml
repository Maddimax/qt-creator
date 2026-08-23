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

    // The fallback font is a container of a family and a size.
    readonly property var fallbackFont: AspectModels.named(aspects.FallbackFont)

    AspectGroupBox {
        title: qsTr("Font")

        ColumnLayout {
            FontFamilyDelegate { aspect: root.fallbackFont.FallbackFontFamily }
            IntegerDelegate { aspect: root.fallbackFont.FallbackFontSize }
            TextDisplayDelegate { aspect: aspects.StyleSheetNote }

            RowLayout {
                IntegerDelegate { aspect: aspects.FontZoom }
                BoolDelegate { aspect: aspects.FontAntialias }
            }
        }
    }

    AspectGroupBox {
        title: qsTr("Startup")

        ColumnLayout {
            SelectionDelegate { aspect: aspects.ContextHelpOption }
            SelectionDelegate { aspect: aspects.StartOption }
            StringDelegate { aspect: aspects.HomePage }

            RowLayout {
                ButtonDelegate { aspect: aspects.UseCurrentPage }
                ButtonDelegate { aspect: aspects.UseBlankPage }
                ButtonDelegate { aspect: aspects.ResetHomePage }
            }
        }
    }

    AspectGroupBox {
        title: qsTr("Behavior")

        ColumnLayout {
            BoolDelegate { aspect: aspects.UseScrollWheelZooming }
            BoolDelegate { aspect: aspects.ReturnOnClose }
            SelectionDelegate { aspect: aspects.ViewerBackend }
        }
    }

    TextDisplayDelegate { aspect: aspects.ErrorLabel }

    RowLayout {
        ButtonDelegate { aspect: aspects.ImportBookmarks }
        ButtonDelegate { aspect: aspects.ExportBookmarks }
    }
}
