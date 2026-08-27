// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick.Layouts
import QtCreator.Ui

// Where NuGet and the Windows App SDK are, and how to fetch them if they are
// not. The summary at the bottom says which of the three paths hold.
AspectPage {
    id: root

    AspectGroupBox {
        title: qsTr("Download Path")

        StringDelegate { aspect: root.aspects.DownloadLocation }
    }

    AspectGroupBox {
        title: qsTr("NuGet")

        RowLayout {
            spacing: Spacing.GapHM
            StringDelegate { aspect: root.aspects.NugetLocation }
            ButtonDelegate { aspect: root.aspects.DownloadNuget }
        }
    }

    AspectGroupBox {
        title: qsTr("Windows App SDK Settings")

        RowLayout {
            spacing: Spacing.GapHM
            StringDelegate { aspect: root.aspects.WindowsAppSdkLocation }
            ButtonDelegate { aspect: root.aspects.DownloadWindowsAppSdk }
        }
    }

    // A container of its own: the label is whether everything holds and the
    // rows are the individual checks.
    InlineGroupDelegate { aspect: root.aspects.Summary }
}
