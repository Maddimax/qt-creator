// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtCreator.Ui

// Starting an analysis of one file. The command may be left empty to derive it
// from the active project - which is why the warning below is shown when there
// is no active project to derive from.
AspectPage {
    id: root

    StringDelegate { aspect: root.aspects.BauhausConfig }
    TextDisplayDelegate { aspect: root.aspects.BauhausConfigHint }

    StringDelegate { aspect: root.aspects.SfaCommand }
    TextDisplayDelegate { aspect: root.aspects.SfaCommandHint }
    TextDisplayDelegate { aspect: root.aspects.NoProjectWarning }
}
