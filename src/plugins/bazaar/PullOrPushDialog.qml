// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtCreator.Ui

// Where a pull or push goes. Which options are shown depends on the direction
// and which fields can be typed into depends on the answer above them; both are
// the aspects' own doing, so nothing here says either.
AspectPage {
    id: root

    AspectGroupBox {
        title: qsTr("Branch Location")

        RadioGroupDelegate { aspect: root.aspects.Location }
        StringDelegate { aspect: root.aspects.LocalPath }
        StringDelegate { aspect: root.aspects.Url }
    }

    AspectGroupBox {
        title: qsTr("Options")

        BoolDelegate { aspect: root.aspects.Remember }
        BoolDelegate { aspect: root.aspects.Overwrite }
        BoolDelegate { aspect: root.aspects.Local }
        BoolDelegate { aspect: root.aspects.UseExistingDirectory }
        BoolDelegate { aspect: root.aspects.CreatePrefix }
        StringDelegate { aspect: root.aspects.Revision }
    }
}
