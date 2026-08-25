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
    }

    visible: aspectVisible
    spacing: Spacing.GapHM
    Layout.fillWidth: true

    Label {
        text: delegate.labelText
        Layout.preferredWidth: Metrics.formLabelWidth
        // An aspect with no label of its own reserves no room for one.
        visible: text !== ""
        elide: Text.ElideRight
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
            echoMode: delegate.pres.password ? TextInput.Password : TextInput.Normal
            enabled: delegate.aspect?.enabled ?? false
            readOnly: delegate.aspect?.readOnly ?? true
            ToolTip.text: delegate.toolTip
            ToolTip.visible: hovered && delegate.toolTip !== ""
            Layout.fillWidth: true

            // A value the aspect has said is wrong does not go into it. The
            // text stays in the field with the reason under it, so nothing is
            // lost and nothing invalid is stored.
            onEditingFinished: {
                if (delegate.aspect && field.error === "")
                    delegate.aspect.value = text
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

    Button {
        objectName: "browseButton"
        text: qsTr("Browse...")
        visible: delegate.isPath
        enabled: (delegate.aspect?.enabled ?? false) && !(delegate.aspect?.readOnly ?? true)
        onClicked: delegate.wantsDirectory ? folderDialog.open() : fileDialog.open()
    }

    FileDialog {
        id: fileDialog

        title: (delegate.pres.promptDialogTitle ?? "") !== ""
               ? delegate.pres.promptDialogTitle : qsTr("Choose File")
        nameFilters: (delegate.pres.promptDialogFilter ?? "") !== ""
                     ? [delegate.pres.promptDialogFilter] : []
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
