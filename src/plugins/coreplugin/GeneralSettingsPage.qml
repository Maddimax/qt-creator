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

    ColorDelegate { aspect: root.aspects.Color }
    SelectionDelegate { aspect: root.aspects.CreatorTheme }
    SelectionDelegate { aspect: root.aspects.ToolbarStyle }
    SelectionDelegate { aspect: root.aspects.OverrideLanguage }
    SelectionDelegate { aspect: root.aspects.HighDpiScaleFactorRoundingPolicy }
    TextDisplayDelegate { aspect: root.aspects.EnvVarInfo }
    SelectionDelegate { aspect: root.aspects.OverrideCodecForLocale }

    BoolDelegate { aspect: root.aspects.ShowShortcutsInContextMenu }
    BoolDelegate { aspect: root.aspects.OverrideSplitterCursors }
    BoolDelegate { aspect: root.aspects.PreferInfoBarOverPopup }
    BoolDelegate { aspect: root.aspects.UseTabsInEditorViews }
    BoolDelegate { aspect: root.aspects.ShowOkAndCancelInSettingsMode }

    ButtonDelegate { aspect: root.aspects.ResetWarnings }
}
