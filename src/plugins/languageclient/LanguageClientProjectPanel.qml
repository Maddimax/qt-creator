// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Layouts
import QtCreator.Ui
import QtCreator.TextEditor

// Language servers for one project. What the servers themselves add is not
// known here: each hands over a container and the page shows what it is given,
// which is why the last item is drawn from the model rather than named.
AspectPage {
    id: root

    contentFillsHeight: true

    TextDisplayDelegate { aspect: aspects.GlobalLink }

    GroupDelegate { aspect: aspects.Overrides }

    AspectGroupBox {
        title: qsTr("Workspace Configuration")
        Layout.fillHeight: true

        ColumnLayout {
            TextDisplayDelegate { aspect: root.aspects.WorkspaceNote }

            SnippetEditor {
                aspect: root.aspects.Json
                mimeType: "application/json"
                Layout.fillHeight: true
            }
        }
    }

    FlattenedGroupDelegate { aspect: aspects.ClientSettings }
}
