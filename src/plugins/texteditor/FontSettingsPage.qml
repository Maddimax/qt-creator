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
            FontFamilyDelegate { aspect: aspects.Family }
            SelectionDelegate { aspect: aspects.Size }
        }

        RowLayout {
            IntegerDelegate { aspect: aspects.Zoom }
            IntegerDelegate { aspect: aspects.LineSpacing }
        }

        TextDisplayDelegate { aspect: aspects.LineSpacingWarning }
        BoolDelegate { aspect: aspects.Antialias }
    }

    AspectGroupBox {
        // The theme's name is the container's to say, not the page's.
        title: root.aspects.Formats.groupTitle

        RowLayout {
            SelectionDelegate { aspect: aspects.Scheme }
            ButtonDelegate { aspect: aspects.CopyScheme }
            ButtonDelegate { aspect: aspects.DeleteScheme }
            ButtonDelegate { aspect: aspects.ImportScheme }
            ButtonDelegate { aspect: aspects.ExportScheme }
        }

        TextDisplayDelegate { aspect: aspects.BuiltinSchemeNote }

        RowLayout {
            spacing: Spacing.GapHM
            Layout.fillHeight: true

            TableDelegate {
                aspect: aspects.Formats
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

                ColorDelegate { aspect: aspects.Foreground }
                ColorDelegate { aspect: aspects.Background }
                DoubleDelegate { aspect: aspects.ForegroundSaturation }
                DoubleDelegate { aspect: aspects.ForegroundLightness }
                DoubleDelegate { aspect: aspects.BackgroundSaturation }
                DoubleDelegate { aspect: aspects.BackgroundLightness }
                BoolDelegate { aspect: aspects.Bold }
                BoolDelegate { aspect: aspects.Italic }
                ColorDelegate { aspect: aspects.UnderlineColor }
                SelectionDelegate { aspect: aspects.UnderlineStyle }
            }
        }
    }
}
