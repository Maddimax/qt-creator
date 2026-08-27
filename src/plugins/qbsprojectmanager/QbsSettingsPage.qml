// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCreator.Ui

AspectPage {
    id: root

    BoolDelegate { aspect: root.aspects.useCreatorDir }

    RowLayout {
        StringDelegate { aspect: root.aspects.QbsExecutable }
        ButtonDelegate { aspect: root.aspects.ResetExecutablePath }
    }

    StringDelegate { aspect: root.aspects.DefaultInstallDir }
    TextDisplayDelegate { aspect: root.aspects.Version }
}
