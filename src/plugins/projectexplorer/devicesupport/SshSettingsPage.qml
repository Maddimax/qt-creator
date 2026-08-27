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

    BoolDelegate { aspect: root.aspects.UseConnectionSharing }
    IntegerDelegate { aspect: root.aspects.ConnectionSharingTimeout }
    StringDelegate { aspect: root.aspects.SshFilePath }
    StringDelegate { aspect: root.aspects.SftpFilePath }
    StringDelegate { aspect: root.aspects.AskpassFilePath }
    StringDelegate { aspect: root.aspects.KeygenFilePath }
}
