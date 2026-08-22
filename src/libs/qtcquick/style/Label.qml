// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Templates as T
import QtCreator.Ui

T.Label {
    color: enabled ? Tokens.textDefault : Tokens.textSubtle
    font: Fonts.body2
    linkColor: Tokens.textAccent
    verticalAlignment: T.Label.AlignVCenter
}
