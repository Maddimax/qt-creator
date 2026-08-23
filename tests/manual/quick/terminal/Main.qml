// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

import QtQuick
import TerminalManualTest

Window {
    id: window
    width: 1400
    height: 834
    visible: true
    color: "#1e1e1e"
    title: "Qt Quick terminal manual test"

    TerminalQuickItem {
        id: term
        objectName: "term"
        anchors.fill: parent
        focus: true
        Component.onCompleted: term.forceActiveFocus()
    }
}
