// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import QtCreator.Ui

RowLayout {
    id: root

    required property Aspect aspect
    readonly property var pres: aspect ? AspectModels.presentation(aspect) : ({})
    readonly property string labelText: aspect?.plainLabelText ?? ""
    readonly property string toolTip: aspect?.toolTip ?? ""
    readonly property bool aspectVisible: aspect?.visible ?? true
    readonly property bool editable:
        (aspect?.enabled ?? false) && !(aspect?.readOnly ?? true)
    // What the list holds, so that Add... asks for the right thing. A list of
    // mount points wants directories; a list of valgrind suppressions wants
    // the files that valgrind reads.
    readonly property string pathKind: pres.pathKind ?? ""
    readonly property bool wantsDirectory:
        pathKind === "" || pathKind === "Any"
        || pathKind === "ExistingDirectory" || pathKind === "Directory"

    function append(paths: list<string>): void {
        if (!root.aspect || paths.length === 0)
            return
        root.aspect.value = (root.aspect.value ?? []).concat(paths)
    }

    visible: aspectVisible
    spacing: Spacing.GapHM
    Layout.fillWidth: true

    Label {
        text: root.labelText
        Layout.preferredWidth: Metrics.formLabelWidth
        // An aspect with no label of its own reserves no room for one.
        visible: text !== ""
        elide: Text.ElideRight
    }

    TextField {
        text: (root.aspect?.value ?? []).join("; ")
        enabled: root.editable
        readOnly: root.aspect?.readOnly ?? true
        placeholderText: (root.pres.placeholderText ?? "") !== ""
                         ? root.pres.placeholderText : qsTr("Semicolon-separated paths")
        ToolTip.text: root.toolTip
        ToolTip.visible: hovered && root.toolTip !== ""
        Layout.fillWidth: true

        onEditingFinished: {
            if (!root.aspect)
                return

            const parts = text.split(";")
            const values = []
            for (let i = 0; i < parts.length; ++i) {
                const trimmedPart = parts[i].trim()
                if (trimmedPart.length > 0)
                    values.push(trimmedPart)
            }
            root.aspect.value = values
        }
    }

    Button {
        objectName: "addButton"
        text: qsTr("Add...")
        visible: root.pres.allowAdding ?? true
        enabled: root.editable
        onClicked: root.wantsDirectory ? folderDialog.open() : fileDialog.open()
    }

    FileDialog {
        id: fileDialog

        title: (root.pres.promptDialogTitle ?? "") !== ""
               ? root.pres.promptDialogTitle : qsTr("Choose Files")
        // Qt's filters are one ";;"-separated string; QML wants them one by one.
        nameFilters: (root.pres.promptDialogFilter ?? "") !== ""
                     ? root.pres.promptDialogFilter.split(";;") : []
        fileMode: FileDialog.OpenFiles

        onAccepted: {
            const paths = []
            for (let i = 0; i < selectedFiles.length; ++i)
                paths.push(AspectModels.localPath(selectedFiles[i]))
            root.append(paths)
        }
    }

    FolderDialog {
        id: folderDialog

        title: (root.pres.promptDialogTitle ?? "") !== ""
               ? root.pres.promptDialogTitle : qsTr("Choose Directory")

        onAccepted: root.append([AspectModels.localPath(selectedFolder)])
    }
}
