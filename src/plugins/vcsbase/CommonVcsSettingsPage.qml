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

    RowLayout {
        BoolDelegate { aspect: root.aspects.ShowVcsStatus }
        IntegerDelegate { aspect: root.aspects.ShowVcsStatusInterval; compact: true }
    }

    RowLayout {
        BoolDelegate { aspect: root.aspects.LineWrap }
        IntegerDelegate { aspect: root.aspects.LineWrapWidth; compact: true }
    }

    RowLayout {
        BoolDelegate { aspect: root.aspects.SpellCheck }
        SelectionDelegate { aspect: root.aspects.SpellCheckLanguage }
    }

    StringDelegate { aspect: root.aspects.SubmitMessageCheckScript }
    StringDelegate { aspect: root.aspects.NickNameMailMap }
    StringDelegate { aspect: root.aspects.NickNameFieldListFile }
    StringDelegate { aspect: root.aspects.SshPasswordPrompt }

    ButtonDelegate { aspect: root.aspects.ResetCache }
}
