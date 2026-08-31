// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Controls
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
    // What the list holds - a list of mount points wants directories, a list
    // of valgrind suppressions the files valgrind reads - comes from the
    // aspect, which defaults to directories. It used to be decided here, and
    // the two dialogs read the same silence differently: the platform's asked
    // for a directory and ours for a file.
    PathBrowser {
        id: browser

        pres: root.pres
        several: true

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

    FormLabel {
        text: root.labelText
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

    function browse(remote: bool): void {
        const values = root.aspect?.value ?? []
        const last = values.length > 0 ? String(values[values.length - 1]) : ""
        browser.browse(last, root.startFolder, remote)
    }

    Button {
        objectName: "addButton"
        text: qsTr("Add...")
        visible: root.pres.allowAdding ?? true
        enabled: root.editable
        onClicked: root.browse(false)
    }

}
