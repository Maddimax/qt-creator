// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Layouts
import QtCreator.Ui

// How tabs and indentation are written. The global page and a project's Editor panel show
// the same things and differ only in whose container they are shown for.
AspectGroupBox {
    id: root

    // A NamedAspects for whichever container is being shown.
    required property var aspects

    title: qsTr("Tabs And Indentation")

    ColumnLayout {
        TextDisplayDelegate { aspect: root.aspects.CodingStyleWarning }
        BoolDelegate { aspect: root.aspects.AutoDetect }
        SelectionDelegate { aspect: root.aspects.TabPolicy }
        IntegerDelegate { aspect: root.aspects.IndentSize }
        IntegerDelegate { aspect: root.aspects.TabSize }
        SelectionDelegate { aspect: root.aspects.ContinuationAlignBehavior }
    }
}
