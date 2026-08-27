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

    SelectionDelegate { aspect: root.aspects.DefaultProtocol }
    StringDelegate { aspect: root.aspects.UserName }
    IntegerDelegate { aspect: root.aspects.ExpiryDays }
    BoolDelegate { aspect: root.aspects.CopyToClipboard }
    BoolDelegate { aspect: root.aspects.DisplayOutput }
}
