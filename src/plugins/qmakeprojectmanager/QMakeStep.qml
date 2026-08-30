// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtCreator.Ui

AspectPage {
    id: root

    SelectionDelegate { aspect: root.aspects.QmakeBuildConfig }

    // A field and its buttons are one row, which is the aspect's own doing.
    InlineGroupDelegate { aspect: root.aspects.QmakeArguments }

    // Read-only: what the step will actually run.
    TextAreaDelegate { aspect: root.aspects.EffectiveCall }

    // Only where the Qt version has more than one, which the aspect decides.
    MultiSelectionDelegate { aspect: root.aspects.Abis }
}
