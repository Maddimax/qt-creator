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
    // Whether an entry may name somewhere that is not this machine.
    readonly property bool allowsDevicePaths: pres.allowPathFromDevice ?? false

    DeviceBrowse {
        id: deviceBrowse

        allowed: root.allowsDevicePaths
        pathKind: root.pathKind

        onChosen: (paths) => root.append(paths)
    }

    // Where browsing starts: beside the last entry in the list, which is the
    // one the reader most likely wants another beside. A list has no single
    // value for the aspect to answer about, so this asks about that entry.
    readonly property string startFolder: {
        const values = root.aspect?.value ?? []
        if (values.length === 0)
            return ""
        return root.aspect.browseStartDirectory(String(values[values.length - 1]))
    }

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
        onClicked: (mouse) => {
            // The same rule the path field follows: our own dialog for a path
            // on a device, or when Shift asks for one; the platform's
            // otherwise, so that its usual picker is not taken away.
            const values = root.aspect?.value ?? []
            const last = values.length > 0 ? String(values[values.length - 1]) : ""
            if (deviceBrowse.wanted(last, mouse.modifiers)) {
                deviceBrowse.open(root.startFolder,
                                  root.pres.promptDialogFilter ?? "", true)
                return
            }

            const url = root.startFolder !== ""
                      ? Qt.resolvedUrl("file://" + root.startFolder) : ""
            if (root.wantsDirectory) {
                if (url !== "")
                    folderDialog.currentFolder = url
                folderDialog.open()
            } else {
                if (url !== "")
                    fileDialog.currentFolder = url
                fileDialog.open()
            }
        }
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
