// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import QtCreator.Ui
import QtCreator.TextEditor

// A JSON message being written: the text, highlighted and checked by the
// controller, and a way to put a variable into it. The controller's:
// see LanguageClient::JsonMessageBox.
ColumnLayout {
    id: root

    required property var controller

    spacing: Spacing.GapVXs

    CodeViewport {
        id: view

        objectName: "jsonMessageView"
        Layout.fillWidth: true
        Layout.fillHeight: true
        source: root.controller.buffer
        // A message is written here, not shown; the view is read-only until
        // told otherwise.
        readOnly: false
        // The parse error is a mark with an annotation, and this is where it
        // is read.
        showAnnotations: true

        // Inside the view, not beside it: a popup positions itself in its
        // parent, and the layout's children are the layout's to place.
        QtcVariableChooser {
            id: chooser

            variables: root.controller.variables
            y: view.height

            onChose: (text) => view.insert(text)
        }
    }

    QtcButton {
        objectName: "insertVariable"
        text: qsTr("Insert Variable...")
        role: QtcButton.Role.SmallSecondary
        onClicked: chooser.offer()
    }
}
