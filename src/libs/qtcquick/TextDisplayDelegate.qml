// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCreator.Ui

Label {
    required property Aspect aspect
    required property string labelText
    required property bool aspectVisible

    text: labelText
    visible: aspectVisible && labelText !== ""
    wrapMode: Text.WordWrap
    color: Tokens.textMuted
    Layout.fillWidth: true
}
