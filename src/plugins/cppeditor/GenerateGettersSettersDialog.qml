// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick.Layouts
import QtCreator.Ui

// One row per data member and one column per thing that can be generated for
// it. The four boxes above the tree are the same answer given for every row at
// once, and say "neither" when the rows disagree.
AspectPage {
    id: root

    contentFillsHeight: true

    TextDisplayDelegate { aspect: root.aspects.Intro }

    TriStateDelegate { aspect: root.aspects.AllGetters }
    TriStateDelegate { aspect: root.aspects.AllSetters }
    TriStateDelegate { aspect: root.aspects.AllSignals }
    TriStateDelegate { aspect: root.aspects.AllProperties }

    TreeDelegate {
        objectName: "candidateTree"
        aspect: root.aspects.Members
        Layout.fillWidth: true
        Layout.fillHeight: true
    }
}
