// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCreator.Ui

AspectPage {
    ColorDelegate { aspect: aspects.Color }
    SelectionDelegate { aspect: aspects.CreatorTheme }
    SelectionDelegate { aspect: aspects.ToolbarStyle }
    SelectionDelegate { aspect: aspects.OverrideLanguage }
    SelectionDelegate { aspect: aspects.HighDpiScaleFactorRoundingPolicy }
    TextDisplayDelegate { aspect: aspects.EnvVarInfo }
    SelectionDelegate { aspect: aspects.OverrideCodecForLocale }

    BoolDelegate { aspect: aspects.ShowShortcutsInContextMenu }
    BoolDelegate { aspect: aspects.OverrideSplitterCursors }
    BoolDelegate { aspect: aspects.PreferInfoBarOverPopup }
    BoolDelegate { aspect: aspects.UseTabsInEditorViews }
    BoolDelegate { aspect: aspects.ShowOkAndCancelInSettingsMode }

    ButtonDelegate { aspect: aspects.ResetWarnings }
}
