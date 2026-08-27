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

    AspectGroupBox {
        title: qsTr("Font")

        RowLayout {
            FontFamilyDelegate { aspect: root.aspects.Family }
            SelectionDelegate { aspect: root.aspects.Size }
        }

        RowLayout {
            IntegerDelegate { aspect: root.aspects.Zoom }
            IntegerDelegate { aspect: root.aspects.LineSpacing }
        }

        TextDisplayDelegate { aspect: root.aspects.LineSpacingWarning }
        BoolDelegate { aspect: root.aspects.Antialias }
    }

    AspectGroupBox {
        // The theme's name is the container's to say, not the page's.
        title: root.aspects.Formats.groupTitle

        RowLayout {
            SelectionDelegate { aspect: root.aspects.Scheme }
            ButtonDelegate { aspect: root.aspects.CopyScheme }
            ButtonDelegate { aspect: root.aspects.DeleteScheme }
            ButtonDelegate { aspect: root.aspects.ImportScheme }
            ButtonDelegate { aspect: root.aspects.ExportScheme }
        }

        TextDisplayDelegate { aspect: root.aspects.BuiltinSchemeNote }

        RowLayout {
            spacing: Spacing.GapHM
            Layout.fillHeight: true

            TableDelegate {
                aspect: root.aspects.Formats
                Layout.fillHeight: true
                // The list is long and is the thing being read, so it gets
                // room rather than the height of a list editor.
                Layout.minimumHeight: 320

                // The properties below are those of whichever format is
                // current, so the aspect has to be told which that is.
                onCurrentRowChanged: root.aspects.Formats.setCurrentRow(currentRow)
            }

            // What the selected format looks like. Each control is shown only
            // where the format has that property; the aspect says so.
            ColumnLayout {
                spacing: Spacing.GapVXs
                Layout.alignment: Qt.AlignTop

                ColorDelegate { aspect: root.aspects.Foreground }
                ColorDelegate { aspect: root.aspects.Background }
                DoubleDelegate { aspect: root.aspects.ForegroundSaturation }
                DoubleDelegate { aspect: root.aspects.ForegroundLightness }
                DoubleDelegate { aspect: root.aspects.BackgroundSaturation }
                DoubleDelegate { aspect: root.aspects.BackgroundLightness }
                BoolDelegate { aspect: root.aspects.Bold }
                BoolDelegate { aspect: root.aspects.Italic }
                ColorDelegate { aspect: root.aspects.UnderlineColor }
                SelectionDelegate { aspect: root.aspects.UnderlineStyle }
            }
        }
    }
}
