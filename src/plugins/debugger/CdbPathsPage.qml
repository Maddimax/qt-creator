// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick.Layouts
import QtCreator.Ui

// Where CDB looks for symbols and for sources. The three buttons write a
// symbol path without the user having to spell "srv*<cache>*<url>" out.
AspectPage {
    id: root

    AspectGroupBox {
        title: qsTr("Symbol Paths")

        StringListEditorDelegate { aspect: root.aspects.SymbolPaths }

        RowLayout {
            spacing: Spacing.GapHM
            ButtonDelegate { aspect: root.aspects.InsertSymbolServer }
            ButtonDelegate { aspect: root.aspects.InsertSymbolCache }
            ButtonDelegate { aspect: root.aspects.SetUpSymbolPaths }
        }
    }

    AspectGroupBox {
        title: qsTr("Source Paths")

        StringListEditorDelegate { aspect: root.aspects.SourcePaths }
    }
}
