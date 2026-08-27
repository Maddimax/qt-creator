// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtCreator.Ui

// What one compiler in a Compiler Explorer document is asked to do. Shown in a
// popup from the editor rather than in Preferences, which is why it is a form
// with no page furniture around it.
AspectPage {
    id: root

    SelectionDelegate { aspect: root.aspects.Id }
    StringDelegate { aspect: root.aspects.Options }
    TextWithActionDelegate { aspect: root.aspects.Libraries }

    AspectGroupBox {
        title: qsTr("Filters")

        BoolDelegate { aspect: root.aspects.ExecuteCode }
        BoolDelegate { aspect: root.aspects.CompileToBinaryObject }
        BoolDelegate { aspect: root.aspects.IntelAsmSyntax }
        BoolDelegate { aspect: root.aspects.DemangleIdentifiers }
    }
}
