// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Layouts
import QtCreator.Ui
import QtCreator.TextEditor

AspectPage {
    id: root

    contentFillsHeight: true

    AspectGroupBox {
        checkAspect: root.aspects.Enabled
        title: qsTr("Use Python Language Server")
        Layout.fillHeight: true

        ColumnLayout {
            spacing: Spacing.GapVS
            Layout.fillHeight: true

            // The plugins are a reading of the configuration below; either one
            // is shown, never both.
            AspectItems {
                Layout.fillWidth: true
                model: AspectModels.container(root.aspects.Plugins)
                visible: root.aspects.Plugins.visible
            }

            TextDisplayDelegate { aspect: root.aspects.Documentation }
            TextDisplayDelegate { aspect: root.aspects.Error }

            SnippetEditor {
                aspect: root.aspects.Configuration
                mimeType: "application/json"
                Layout.fillHeight: true
            }

            BoolDelegate { aspect: root.aspects.Advanced }

            // The editor takes the height of the group when it is shown.
            // When the plugins are shown instead there is nothing that grows,
            // and without this the whole column is centred in the group -
            // which put the first plugin 115 pixels down an empty box.
            Item { Layout.fillHeight: !root.aspects.Configuration.visible }
        }
    }
}
