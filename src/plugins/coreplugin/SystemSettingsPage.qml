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

    TextWithActionDelegate { aspect: root.aspects.EnvironmentChanges }
    TextWithActionDelegate { aspect: root.aspects.EnvVarSeparators }

    // One row: what the three fields come to, the button that opens them, and
    // the emulators this machine has. The fields are in a container of their
    // own that is hidden here and shown by the dialog.
    InlineGroupDelegate { aspect: root.aspects.Terminal }

    StringDelegate { aspect: root.aspects.FileBrowser }
    BoolDelegate { aspect: root.aspects.SupportDbusFileManagers }
    StringDelegate { aspect: root.aspects.PatchCommand }
    IntegerDelegate { aspect: root.aspects.MaxRecentFiles }
    SelectionDelegate { aspect: root.aspects.ReloadBehavior }

    RowLayout {
        BoolDelegate { aspect: root.aspects.AutoSaveEnabled }
        IntegerDelegate { aspect: root.aspects.AutoSaveInterval; compact: true }
    }

    BoolDelegate { aspect: root.aspects.AutoSaveAfterRefactoring }

    BoolDelegate { aspect: aspects.DisableAtomicSave }

    RowLayout {
        BoolDelegate { aspect: root.aspects.AutoSuspendEnabled }
        IntegerDelegate { aspect: root.aspects.AutoSuspendMinDocuments; compact: true }
    }

    RowLayout {
        BoolDelegate { aspect: root.aspects.WarnBeforeOpeningBigTextFiles }
        IntegerDelegate { aspect: root.aspects.BigTextFileSizeLimitInMB; compact: true }
    }

    BoolDelegate { aspect: root.aspects.AskBeforeExit }

    RowLayout {
        BoolDelegate { aspect: root.aspects.CrashReportingEnabled }
        ButtonDelegate { aspect: root.aspects.CrashNow }
    }
}
