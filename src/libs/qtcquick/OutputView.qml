// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Controls

// What a pane's output looks like. The text itself is a document handed over
// from C++ - see QtcQuick::OutputView - so nothing here holds or formats it.
Item {
    id: root

    // The font the output is read in. A pane sets it, and its zoom is applied
    // to it before it arrives.
    property font baseFont: Fonts.body1

    ScrollView {
        anchors.fill: parent
        clip: true

        TextArea {
            objectName: "outputText"

            readOnly: true
            // Selectable, because copying a compiler error out of the pane is
            // half of what the pane is for.
            selectByMouse: true
            font: root.baseFont
            wrapMode: TextArea.NoWrap
            background: null
        }
    }
}
