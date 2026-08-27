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
    readonly property var fallbackFont: AspectModels.named(root.aspects.FallbackFont)

    AspectGroupBox {
        title: qsTr("Font")

        ColumnLayout {
            FontFamilyDelegate { aspect: root.fallbackFont.FallbackFontFamily }
            IntegerDelegate { aspect: root.fallbackFont.FallbackFontSize }
            TextDisplayDelegate { aspect: root.aspects.StyleSheetNote }

            RowLayout {
                IntegerDelegate { aspect: root.aspects.FontZoom }
                BoolDelegate { aspect: root.aspects.FontAntialias }
            }
        }
    }

    AspectGroupBox {
        title: qsTr("Startup")

        ColumnLayout {
            SelectionDelegate { aspect: root.aspects.ContextHelpOption }
            SelectionDelegate { aspect: root.aspects.StartOption }
            StringDelegate { aspect: root.aspects.HomePage }

            RowLayout {
                ButtonDelegate { aspect: root.aspects.UseCurrentPage }
                ButtonDelegate { aspect: root.aspects.UseBlankPage }
                ButtonDelegate { aspect: root.aspects.ResetHomePage }
            }
        }
    }

    AspectGroupBox {
        title: qsTr("Behavior")

        ColumnLayout {
            BoolDelegate { aspect: root.aspects.UseScrollWheelZooming }
            BoolDelegate { aspect: root.aspects.ReturnOnClose }
            SelectionDelegate { aspect: root.aspects.ViewerBackend }
        }
    }

    TextDisplayDelegate { aspect: root.aspects.ErrorLabel }

    RowLayout {
        ButtonDelegate { aspect: root.aspects.ImportBookmarks }
        ButtonDelegate { aspect: root.aspects.ExportBookmarks }
    }
}
