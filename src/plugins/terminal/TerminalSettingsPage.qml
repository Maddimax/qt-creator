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
            BoolDelegate { aspect: root.aspects.EnableTerminal }
            BoolDelegate { aspect: root.aspects.SendEscapeToTerminal }
            BoolDelegate { aspect: root.aspects.LockKeyboard }
            BoolDelegate { aspect: root.aspects.AudibleBell }
            BoolDelegate { aspect: root.aspects.AllowBlinkingCursor }
            BoolDelegate { aspect: root.aspects.EnableMouseTracking }
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
            FontFamilyDelegate { aspect: root.aspects.FontFamily }
            IntegerDelegate { aspect: root.aspects.FontSize }
        }
    }

    AspectGroupBox {
        title: qsTr("Colors")

        ColumnLayout {
            ColorDelegate { aspect: root.aspects.ColorForeground }
            ColorDelegate { aspect: root.aspects.ColorBackground }
            ColorDelegate { aspect: root.aspects.ColorSelection }
            ColorDelegate { aspect: root.aspects.ColorFindmatches }

            // The ANSI palette: swatches only, eight to a row.
            Grid {
                Layout.fillWidth: true
                columns: 8
                spacing: Spacing.GapHXs

                ColorDelegate { aspect: root.aspects.Color0 }
                ColorDelegate { aspect: root.aspects.Color1 }
                ColorDelegate { aspect: root.aspects.Color2 }
                ColorDelegate { aspect: root.aspects.Color3 }
                ColorDelegate { aspect: root.aspects.Color4 }
                ColorDelegate { aspect: root.aspects.Color5 }
                ColorDelegate { aspect: root.aspects.Color6 }
                ColorDelegate { aspect: root.aspects.Color7 }
                ColorDelegate { aspect: root.aspects.Color8 }
                ColorDelegate { aspect: root.aspects.Color9 }
                ColorDelegate { aspect: root.aspects.Color10 }
                ColorDelegate { aspect: root.aspects.Color11 }
                ColorDelegate { aspect: root.aspects.Color12 }
                ColorDelegate { aspect: root.aspects.Color13 }
                ColorDelegate { aspect: root.aspects.Color14 }
                ColorDelegate { aspect: root.aspects.Color15 }
            }

            RowLayout {
                ButtonDelegate { aspect: root.aspects.LoadTheme }
                ButtonDelegate { aspect: root.aspects.ResetTheme }
                ButtonDelegate { aspect: root.aspects.CopyTheme }
            }
        }
    }

    AspectGroupBox {
        title: qsTr("Default Shell")

        ColumnLayout {
            StringDelegate { aspect: root.aspects.ShellPath }
            StringDelegate { aspect: root.aspects.ShellArguments }
        }
    }
}
