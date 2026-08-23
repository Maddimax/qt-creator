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

    AspectGroupBox {
        title: qsTr("General")

        ColumnLayout {
            BoolDelegate { aspect: aspects.EnableTerminal }
            BoolDelegate { aspect: aspects.SendEscapeToTerminal }
            BoolDelegate { aspect: aspects.LockKeyboard }
            BoolDelegate { aspect: aspects.AudibleBell }
            BoolDelegate { aspect: aspects.AllowBlinkingCursor }
            BoolDelegate { aspect: aspects.EnableMouseTracking }
        }
    }

    AspectGroupBox {
        title: qsTr("Console Host")
        visible: root.aspects.ConsoleHostStatus.visible

        ColumnLayout {
            TextDisplayDelegate { aspect: root.aspects.ConsoleHostStatus }
            AspectDelegate { aspect: root.aspects.ConsoleHostDirectory }
            ButtonDelegate { aspect: root.aspects.DownloadConsoleHost }
        }
    }

    AspectGroupBox {
        title: qsTr("Font")

        ColumnLayout {
            FontFamilyDelegate { aspect: aspects.FontFamily }
            IntegerDelegate { aspect: aspects.FontSize }
        }
    }

    AspectGroupBox {
        title: qsTr("Colors")

        ColumnLayout {
            ColorDelegate { aspect: aspects.ColorForeground }
            ColorDelegate { aspect: aspects.ColorBackground }
            ColorDelegate { aspect: aspects.ColorSelection }
            ColorDelegate { aspect: aspects.ColorFindmatches }

            // The ANSI palette: swatches only, eight to a row.
            Grid {
                Layout.fillWidth: true
                columns: 8
                spacing: Spacing.GapHXs

                ColorDelegate { aspect: aspects.Color0 }
                ColorDelegate { aspect: aspects.Color1 }
                ColorDelegate { aspect: aspects.Color2 }
                ColorDelegate { aspect: aspects.Color3 }
                ColorDelegate { aspect: aspects.Color4 }
                ColorDelegate { aspect: aspects.Color5 }
                ColorDelegate { aspect: aspects.Color6 }
                ColorDelegate { aspect: aspects.Color7 }
                ColorDelegate { aspect: aspects.Color8 }
                ColorDelegate { aspect: aspects.Color9 }
                ColorDelegate { aspect: aspects.Color10 }
                ColorDelegate { aspect: aspects.Color11 }
                ColorDelegate { aspect: aspects.Color12 }
                ColorDelegate { aspect: aspects.Color13 }
                ColorDelegate { aspect: aspects.Color14 }
                ColorDelegate { aspect: aspects.Color15 }
            }

            RowLayout {
                ButtonDelegate { aspect: aspects.LoadTheme }
                ButtonDelegate { aspect: aspects.ResetTheme }
                ButtonDelegate { aspect: aspects.CopyTheme }
            }
        }
    }

    AspectGroupBox {
        title: qsTr("Default Shell")

        ColumnLayout {
            StringDelegate { aspect: aspects.ShellPath }
            StringDelegate { aspect: aspects.ShellArguments }
        }
    }
}
