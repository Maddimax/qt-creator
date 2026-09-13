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
            BoolDelegate { aspect: root.aspects.AllowClipboardWrite }
            BoolDelegate { aspect: root.aspects.ConfirmUnsafePaste }
        }
    }

    AspectGroupBox {
        title: qsTr("Console Host")
        visible: root.aspects.ConsoleHostStatus.visible

        ColumnLayout {
            TextDisplayDelegate { aspect: root.aspects.ConsoleHostStatus }
            StringDelegate { aspect: root.aspects.ConsoleHostDirectory }
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

                ColorDelegate { aspect: root.aspects.Color0; compact: true }
                ColorDelegate { aspect: root.aspects.Color1; compact: true }
                ColorDelegate { aspect: root.aspects.Color2; compact: true }
                ColorDelegate { aspect: root.aspects.Color3; compact: true }
                ColorDelegate { aspect: root.aspects.Color4; compact: true }
                ColorDelegate { aspect: root.aspects.Color5; compact: true }
                ColorDelegate { aspect: root.aspects.Color6; compact: true }
                ColorDelegate { aspect: root.aspects.Color7; compact: true }
                ColorDelegate { aspect: root.aspects.Color8; compact: true }
                ColorDelegate { aspect: root.aspects.Color9; compact: true }
                ColorDelegate { aspect: root.aspects.Color10; compact: true }
                ColorDelegate { aspect: root.aspects.Color11; compact: true }
                ColorDelegate { aspect: root.aspects.Color12; compact: true }
                ColorDelegate { aspect: root.aspects.Color13; compact: true }
                ColorDelegate { aspect: root.aspects.Color14; compact: true }
                ColorDelegate { aspect: root.aspects.Color15; compact: true }
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
