// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCreator.Ui

AspectPage {
    TextWithActionDelegate { aspect: aspects.EnvironmentChanges }
    TextWithActionDelegate { aspect: aspects.EnvVarSeparators }

    // One row: what the three fields come to, the button that opens them, and
    // the emulators this machine has. The fields are in a container of their
    // own that is hidden here and shown by the dialog.
    InlineGroupDelegate { aspect: aspects.Terminal }

    StringDelegate { aspect: aspects.FileBrowser }
    BoolDelegate { aspect: aspects.SupportDbusFileManagers }
    StringDelegate { aspect: aspects.PatchCommand }
    IntegerDelegate { aspect: aspects.MaxRecentFiles }
    SelectionDelegate { aspect: aspects.ReloadBehavior }

    RowLayout {
        BoolDelegate { aspect: aspects.AutoSaveEnabled }
        IntegerDelegate { aspect: aspects.AutoSaveInterval }
    }

    BoolDelegate { aspect: aspects.AutoSaveAfterRefactoring }

    BoolDelegate { aspect: aspects.DisableAtomicSave }

    RowLayout {
        BoolDelegate { aspect: aspects.AutoSuspendEnabled }
        IntegerDelegate { aspect: aspects.AutoSuspendMinDocuments }
    }

    RowLayout {
        BoolDelegate { aspect: aspects.WarnBeforeOpeningBigTextFiles }
        IntegerDelegate { aspect: aspects.BigTextFileSizeLimitInMB }
    }

    BoolDelegate { aspect: aspects.AskBeforeExit }

    RowLayout {
        BoolDelegate { aspect: aspects.CrashReportingEnabled }
        ButtonDelegate { aspect: aspects.CrashNow }
    }
}
