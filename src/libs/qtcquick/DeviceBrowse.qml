// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtCreator.Ui

// Browsing with Qt Creator's own dialog, which reaches a device. Shared by the
// two delegates that browse - a path field and a list of paths - because they
// were deciding the same thing twice and had already begun to differ: one knew
// about saving and the other did not.
//
// Nothing is drawn here. The dialog is a window, made when it is wanted and
// destroyed with it, so that a form does not carry one per field.
QtObject {
    id: root

    // Whether the field will take a path that is not on this machine, and
    // what kind of path it is after.
    required property bool allowed
    required property string pathKind

    // What was chosen. A list, because a dialog choosing several answers with
    // one and a caller wanting one takes the first.
    signal chosen(list<string> paths)

    // Whether \a current calls for our own dialog rather than the platform's:
    // a path already on a device cannot be browsed by the platform's, which
    // knows only the machine it runs on. Asking for one deliberately is the
    // other way in - see the menu on the browse button, which is where the
    // widget path chooser also put the choice.
    //
    //     remote = remote || !filePath().isLocal();   PathChooser::slotBrowse
    //
    // And where the platform has no dialog to offer there is nothing to give
    // up by using ours, which is what the widget did as well.
    function wanted(current: string): bool {
        if (!root.allowed)
            return false
        return !AspectModels.hasNativeFileDialog()
            || (current !== "" && !AspectModels.isLocalPath(current))
    }

    // Opens it at \a start, offering \a nameFilter, and choosing \a several at
    // once when the caller can take several.
    function open(start: string, nameFilter: string, several: bool): void {
        const wantsDirectory = root.pathKind === "ExistingDirectory"
                            || root.pathKind === "Directory"
        const dialog = component.createObject(null, {
            "mode": wantsDirectory
                    ? QtcFileDialog.OpenDirectory
                    : (root.pathKind === "SaveFile"
                       ? QtcFileDialog.SaveFile
                       : (several ? QtcFileDialog.OpenFiles : QtcFileDialog.OpenFile)),
            "currentFolder": start,
            "nameFilter": nameFilter
        })
        if (!dialog)
            return
        dialog.accepted.connect((paths) => {
            root.chosen(paths)
            dialog.destroy()
        })
        dialog.rejected.connect(() => dialog.destroy())
        dialog.show()
    }

    readonly property Component component: Component {
        QtcFileDialog {}
    }
}
