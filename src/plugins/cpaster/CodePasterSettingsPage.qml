// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCreator.Ui

AspectPage {
    SelectionDelegate { aspect: aspects.DefaultProtocol }
    StringDelegate { aspect: aspects.UserName }
    IntegerDelegate { aspect: aspects.ExpiryDays }
    BoolDelegate { aspect: aspects.CopyToClipboard }
    BoolDelegate { aspect: aspects.DisplayOutput }
}
