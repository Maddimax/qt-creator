// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Dialogs
import QtCreator.Ui

// Asking the reader for a path. Three delegates were doing this - a path
// field, a list of paths, and a list of strings that holds directories - and
// the copies had already begun to differ: one knew about saving, one about
// choosing several at once, and one had no way to reach a device at all.
//
// Nothing is drawn. The dialogs are windows and the device one is made when it
// is wanted, so a form carries no dialog per field.
QtObject {
    id: root

    // The aspect's descriptor. What kind of path is wanted, whether it may be
    // on another machine, and what the dialog is called all come from it.
    required property var pres
    // Whether the caller can take more than one answer. A list of paths can.
    property bool several: false

    // What is being asked for. The descriptor's, unless a caller knows better:
    // a list of paths that says nothing wants a directory, which is not what
    // "Any" means here.
    property string pathKind: root.pres.pathKind ?? ""
    // "Any" is the default and means the aspect never said it wanted a path.
    readonly property bool isPath: root.pathKind !== "" && root.pathKind !== "Any"
    readonly property bool wantsDirectory:
        root.pathKind === "ExistingDirectory" || root.pathKind === "Directory"
    readonly property bool allowsDevicePaths: root.pres.allowPathFromDevice ?? false

    // What was chosen. Always a list; a caller wanting one takes the first.
    signal chosen(list<string> paths)

    // Opens on \a start. Which dialog answers is decided from \a current - a
    // path already on a device can only be browsed by ours, because the
    // platform's knows only the machine it runs on - and \a remote asks for
    // ours whatever the path is, which is what the menu on a browse button
    // offers. The widget path chooser put the same choice on the same button.
    //
    // \a current and \a start are both given because they are different
    // questions: what the field holds says which dialog can answer, and where
    // to open is what the aspect makes of it.
    function browse(current: string, start: string, remote: bool): void {
        if (remote || root.deviceBrowse.wanted(current)) {
            root.deviceBrowse.open(start, root.pres.promptDialogFilter ?? "", root.several)
            return
        }

        const url = start !== "" ? Qt.resolvedUrl("file://" + start) : ""
        const dialog = root.wantsDirectory ? root.folderDialog : root.fileDialog
        if (url !== "")
            dialog.currentFolder = url
        dialog.open()
    }

    readonly property DeviceBrowse deviceBrowse: DeviceBrowse {
        allowed: root.allowsDevicePaths
        pathKind: root.pathKind

        onChosen: (paths) => root.chosen(paths)
    }

    readonly property FileDialog fileDialog: FileDialog {
        id: fileChooser

        title: (root.pres.promptDialogTitle ?? "") !== ""
               ? root.pres.promptDialogTitle
               : (root.several ? qsTr("Choose Files") : qsTr("Choose File"))
        // Qt's filters are one ";;"-separated string; QML wants them one by one.
        nameFilters: (root.pres.promptDialogFilter ?? "") !== ""
                     ? root.pres.promptDialogFilter.split(";;") : []
        // A path that does not have to exist yet is being saved to, not opened.
        fileMode: root.several
                  ? FileDialog.OpenFiles
                  : (root.pathKind === "SaveFile" ? FileDialog.SaveFile : FileDialog.OpenFile)

        onAccepted: {
            if (!root.several) {
                root.chosen([AspectModels.localPath(fileChooser.selectedFile)])
                return
            }
            const paths = []
            for (let i = 0; i < fileChooser.selectedFiles.length; ++i)
                paths.push(AspectModels.localPath(fileChooser.selectedFiles[i]))
            root.chosen(paths)
        }
    }

    readonly property FolderDialog folderDialog: FolderDialog {
        id: folderChooser

        title: (root.pres.promptDialogTitle ?? "") !== ""
               ? root.pres.promptDialogTitle : qsTr("Choose Directory")

        onAccepted: root.chosen([AspectModels.localPath(folderChooser.selectedFolder)])
    }
}
