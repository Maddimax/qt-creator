// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCreator.Ui

AspectPage {
    RowLayout {
        BoolDelegate { aspect: aspects.ShowVcsStatus }
        IntegerDelegate { aspect: aspects.ShowVcsStatusInterval }
    }

    RowLayout {
        BoolDelegate { aspect: aspects.LineWrap }
        IntegerDelegate { aspect: aspects.LineWrapWidth }
    }

    RowLayout {
        BoolDelegate { aspect: aspects.SpellCheck }
        SelectionDelegate { aspect: aspects.SpellCheckLanguage }
    }

    StringDelegate { aspect: aspects.SubmitMessageCheckScript }
    StringDelegate { aspect: aspects.NickNameMailMap }
    StringDelegate { aspect: aspects.NickNameFieldListFile }
    StringDelegate { aspect: aspects.SshPasswordPrompt }

    ButtonDelegate { aspect: aspects.ResetCache }
}
