// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtCreator.Ui

// Building a project locally with the Axivion Suite. The two warnings come
// first because they are about what the build will do to the working tree,
// which is worth reading before choosing anything.
AspectPage {
    id: root

    TextDisplayDelegate { aspect: root.aspects.ModifyWarning }
    TextDisplayDelegate { aspect: root.aspects.ConfigWarning }
    TextDisplayDelegate { aspect: root.aspects.VersionHint }

    StringDelegate { aspect: root.aspects.Suite }
    StringDelegate { aspect: root.aspects.Command }
    SelectionDelegate { aspect: root.aspects.BuildType }
}
