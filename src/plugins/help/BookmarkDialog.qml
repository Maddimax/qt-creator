// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Layouts
import QtCreator.Ui

// What to call the bookmark and where to keep it. The folder is named in the
// box; the button beside the rule opens the tree of folders, which is the same
// answer given by pointing at one.
AspectPage {
    id: root

    contentFillsHeight: true

    StringDelegate { aspect: root.aspects.Name }
    SelectionDelegate { aspect: root.aspects.Folder }

    RowLayout {
        spacing: Spacing.GapHM
        Layout.fillWidth: true

        ButtonDelegate { aspect: root.aspects.ShowFolders }

        // The rule the widget dialog drew beside that button, which is what
        // says the tree below belongs to it.
        Rectangle {
            color: Tokens.strokeSubtle
            Layout.preferredHeight: 1
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignVCenter
        }
    }

    TreeDelegate {
        objectName: "bookmarkFolderTree"
        aspect: root.aspects.Folders
        Layout.fillWidth: true
        Layout.fillHeight: true
    }

    ButtonDelegate { aspect: root.aspects.NewFolder }
}
