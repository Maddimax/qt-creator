// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtCreator.Ui

// Where a pull or push goes. Which field can be typed into follows the answer
// above it, and that is the aspects' own doing - nothing here says so.
AspectPage {
    id: root

    AspectGroupBox {
        title: qsTr("Remote Location")

        RadioGroupDelegate { aspect: root.aspects.Location }
        StringDelegate { aspect: root.aspects.LocalPath }
        StringDelegate { aspect: root.aspects.Url }
    }

    AspectGroupBox {
        title: qsTr("Options")

        BoolDelegate { aspect: root.aspects.Remember }
        BoolDelegate { aspect: root.aspects.IncludePrivate }
    }
}
