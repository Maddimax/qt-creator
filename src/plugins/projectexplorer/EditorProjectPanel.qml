// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Layouts
import QtCreator.Ui
import QtCreator.TextEditor

// The editor settings for one project: the same five the Text Editor Behavior
// page shows, and the margin. Whether any of it can be changed is the
// container's answer, not this form's.
AspectPage {
    id: root

    readonly property var margins: AspectModels.named(root.aspects.Margins)

    BoolWithOwnLabelDelegate { aspect: root.aspects.UseGlobalSettings }
    ButtonDelegate { aspect: root.aspects.RestoreGlobal }

    AspectGroupBox {
        title: qsTr("Display Settings")

        RowLayout {
            BoolDelegate { aspect: root.margins.ShowMargin }
            IntegerDelegate { aspect: root.margins.MarginColumn }
            BoolDelegate { aspect: root.margins.tintMarginArea }
            BoolDelegate { aspect: root.margins.UseIndenter }
            Item { Layout.fillWidth: true }
        }
    }

    TabSettingsForm { aspects: AspectModels.named(root.aspects.Tabs) }
    TypingSettingsForm { aspects: AspectModels.named(root.aspects.Typing) }
    StorageSettingsForm { aspects: AspectModels.named(root.aspects.Storage) }
    EncodingSettingsForm { aspects: AspectModels.named(root.aspects.Encoding) }
    BehaviorSettingsForm { aspects: AspectModels.named(root.aspects.Behavior) }
}
