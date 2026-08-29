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
    id: delegate

    required property Aspect aspect
    // Not readonly: an aspect can change what it wants drawn - the label on
    // Copilot's sign-in button is its state - and says so with
    // controlConfigurationChanged().
    property var pres: aspect ? AspectModels.presentation(aspect) : ({})
    readonly property string labelText: aspect?.plainLabelText ?? ""
    readonly property string toolTip: aspect?.toolTip ?? ""
    readonly property bool aspectVisible: aspect?.visible ?? true
    // A path aspect shares this delegate with a plain string; what it adds is
    // somewhere to browse from. "Any" is the default and means the aspect
    // never said it wanted a path.
    readonly property string pathKind: delegate.pres.pathKind ?? ""
    readonly property bool isPath: pathKind !== "" && pathKind !== "Any"
    readonly property bool wantsDirectory:
        pathKind === "ExistingDirectory" || pathKind === "Directory"
    // Whether this field will take a path that is not on this machine.
    readonly property bool allowsDevicePaths: delegate.pres.allowPathFromDevice ?? false

    // Qt Creator's own file dialog, which reaches devices.
    DeviceBrowse {
        id: deviceBrowse

        allowed: delegate.allowsDevicePaths
        pathKind: delegate.pathKind

        onChosen: (paths) => {
            if (delegate.aspect && paths.length > 0)
                delegate.aspect.value = paths[0]
        }
    }

    // An answer that had to be fetched has arrived, so the field asks again.
    // Bumping a counter the binding reads is what makes it re-evaluate: what
    // is wrong with a value is asked for, not a property to bind to.
    property int validationTick: 0

    Connections {
        target: delegate.aspect

        function onControlConfigurationChanged(): void {
            delegate.pres = AspectModels.presentation(delegate.aspect)
        }

        function onValidationMessageChanged(): void {
            delegate.validationTick++
        }

        // Something outside asked for the value to be checked again - a build
        // system reporting what is wrong with a build directory. The widget
        // side re-runs its validator here; this side has to ask the aspect
        // again, which is what bumping the tick does.
        function onControlValidationRequested(): void {
            delegate.validationTick++
        }

        // A page that has just made something for the user to name puts the
        // cursor in it. focus rather than forceActiveFocus(): active focus
        // needs the window to be the active one, which is not this delegate's
        // business and not something a test can rely on.
        function onControlFocusRequested(): void {
            field.focus = true
            field.selectAll()
        }
    }

    // Whether this is a row of the form, whose label takes the form's label
    // column, or a continuation of a check box beside it.
    property bool compact: false

    visible: aspectVisible
    spacing: Spacing.GapHM
    Layout.fillWidth: true

    FormLabel {
        text: delegate.labelText
        compact: delegate.compact
    }

    ColumnLayout {
        spacing: Spacing.GapVXs
        Layout.fillWidth: true

        TextField {
            id: field

            // What is wrong with what is in the field, from the aspect: QML
            // cannot reach a validation function, so it asks. Empty when the
            // aspect has none, which is most of them.
            readonly property string error: {
                delegate.validationTick // Re-ask once the aspect has been to look.
                return delegate.aspect?.validationMessage(text) ?? ""
            }

            text: delegate.aspect?.value ?? ""
            // What the field says when it holds nothing. The widget line edit
            // has shown it all along; a field that dropped it is an empty box
            // with no clue what belongs in it.
            placeholderText: delegate.pres.placeholderText ?? ""
            // A field shows the text around its cursor, and text set from the
            // aspect leaves it at the end - so a value too long for the field
            // was drawn from its tail with its beginning scrolled out of
            // sight. The widget line edit shows the start of what it holds.
            onTextChanged: if (!activeFocus) cursorPosition = 0
            echoMode: delegate.pres.password ? TextInput.Password : TextInput.Normal
            enabled: delegate.aspect?.enabled ?? false
            readOnly: delegate.aspect?.readOnly ?? true
            // A path to a command says what version it is, which costs a
            // process and so is asked for when the pointer arrives rather
            // than kept up to date. Aspects with nothing of the kind never
            // answer and the tooltip stays what the aspect says.
            onHoveredChanged: {
                if (hovered)
                    delegate.aspect?.requestExtendedToolTip(field.text)
            }

            readonly property string extended: delegate.aspect?.extendedToolTip ?? ""
            ToolTip.text: field.extended !== ""
                          ? (delegate.toolTip !== ""
                             ? delegate.toolTip + "\n\n" + field.extended : field.extended)
                          : delegate.toolTip
            ToolTip.visible: hovered && ToolTip.text !== ""
            Layout.fillWidth: true

            // A value the aspect has said is wrong does not go into it. The
            // text stays in the field with the reason under it, so nothing is
            // lost and nothing invalid is stored.
            onEditingFinished: {
                if (delegate.aspect && field.error === "") {
                    delegate.aspect.value = text
                    // What a field remembers is what was entered into it, so
                    // this is the moment: a value the aspect refused above is
                    // not worth offering again. Aspects with no history to
                    // keep do nothing here.
                    delegate.aspect.rememberValue()
                }
            }

            // A one-line field completes against the whole of what it holds.
            onTextEdited: completion.offer()

            Keys.onPressed: (event) => {
                if (!completion.visible)
                    return
                switch (event.key) {
                case Qt.Key_Down: completion.moveDown(); event.accepted = true; break
                case Qt.Key_Up: completion.moveUp(); event.accepted = true; break
                case Qt.Key_Return:
                case Qt.Key_Enter:
                case Qt.Key_Tab: completion.acceptCurrent(); event.accepted = true; break
                }
            }

            CompletionPopup {
                id: completion

                completions: delegate.pres.completions ?? []
                prefix: field.text
                y: field.height
                width: field.width

                onAccepted: (text) => {
                    field.text = text
                    field.cursorPosition = text.length
                }
            }
        }

        Label {
            objectName: "validationMessage"
            text: field.error
            visible: field.error !== "" && field.enabled
            color: Tokens.notificationDangerDefault
            font: Fonts.body2
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
    }

    // A way back to what the setting is when nobody has touched it. Only for
    // the aspects that ask; a default alone is not a reason to offer one.
    Button {
        objectName: "resetButton"
        text: qsTr("Reset")
        visible: delegate.pres.withResetButton ?? false
        enabled: (delegate.aspect?.enabled ?? false)
                 && !(delegate.aspect?.readOnly ?? true)
                 && field.text !== String(delegate.pres.defaultValue ?? "")
        onClicked: {
            if (delegate.aspect)
                delegate.aspect.value = delegate.pres.defaultValue ?? ""
        }
    }

    Button {
        objectName: "browseButton"
        text: qsTr("Browse...")
        visible: delegate.isPath
        enabled: (delegate.aspect?.enabled ?? false) && !(delegate.aspect?.readOnly ?? true)
        onClicked: (mouse) => {
            // Where to open is worked out now rather than bound: it depends on
            // what is in the field, and on what the filesystem says about it.
            const start = delegate.aspect
                        ? delegate.aspect.browseStartDirectory(field.text) : ""

            // A path on a device cannot be browsed by the platform's dialog,
            // which knows only the machine it runs on - the widget path
            // chooser reaches for Qt Creator's own for exactly this.
            if (deviceBrowse.wanted(field.text, mouse.modifiers)) {
                deviceBrowse.open(start, delegate.pres.promptDialogFilter ?? "", false)
                return
            }

            const url = start !== "" ? Qt.resolvedUrl("file://" + start) : ""
            if (delegate.wantsDirectory) {
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

        title: (delegate.pres.promptDialogTitle ?? "") !== ""
               ? delegate.pres.promptDialogTitle : qsTr("Choose File")
        // Qt's filters are one ";;"-separated string; QML wants them one by one.
        nameFilters: (delegate.pres.promptDialogFilter ?? "") !== ""
                     ? delegate.pres.promptDialogFilter.split(";;") : []
        // A path that does not have to exist yet is being saved to, not opened.
        fileMode: delegate.pathKind === "SaveFile"
                  ? FileDialog.SaveFile : FileDialog.OpenFile

        onAccepted: {
            if (delegate.aspect)
                delegate.aspect.value = AspectModels.localPath(selectedFile)
        }
    }

    FolderDialog {
        id: folderDialog

        title: (delegate.pres.promptDialogTitle ?? "") !== ""
               ? delegate.pres.promptDialogTitle : qsTr("Choose Directory")

        onAccepted: {
            if (delegate.aspect)
                delegate.aspect.value = AspectModels.localPath(selectedFolder)
        }
    }
}
