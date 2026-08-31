// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCreator.Ui

// A list of strings with add and remove. The aspect's value is the whole list,
// so every edit assigns a modified copy back rather than mutating in place.
RowLayout {
    id: root

    required property Aspect aspect
    // The descriptor, read from the aspect rather than taken as model roles, so
    // that a hand-written page can use this delegate with nothing but the
    // aspect. See AspectModels::presentation().
    // Not readonly: an aspect can change what it wants drawn - the label on
    // Copilot's sign-in button is its state - and says so with
    // controlConfigurationChanged().
    property var pres: aspect ? AspectModels.presentation(aspect) : ({})
    readonly property string labelText: aspect?.plainLabelText ?? ""
    readonly property string toolTip: aspect?.toolTip ?? ""
    readonly property bool aspectVisible: aspect?.visible ?? true

    Connections {
        target: root.aspect
        function onControlConfigurationChanged() {
            root.pres = AspectModels.presentation(root.aspect)
        }
    }
    readonly property bool editable: (aspect?.enabled ?? false) && !(aspect?.readOnly ?? false)

    // Entries that are paths are picked rather than typed: Add opens the same
    // dialog the widget list's Add... did, and Edit reopens it on the row the
    // reader is on. "Any" - the default - means plain strings and no Edit.
    readonly property string pathKind: root.pres.pathKind ?? ""
    readonly property bool browsable: root.pathKind !== "" && root.pathKind !== "Any"
    // Which row the open dialog is answering for. -1 appends.
    property int browsingFor: -1

    // A copy, deliberately: a QStringList property reaches QML as a sequence
    // that writes through, so mutating it in place would set the aspect once
    // per element. Every edit below changes the copy and assigns it once.
    function entries(): list<string> {
        return (root.aspect?.value ?? []).slice()
    }

    function replaceAt(index: int, text: string) {
        const next = root.entries()
        if (index < 0 || index >= next.length || next[index] === text)
            return
        next[index] = text
        root.aspect.value = next
    }

    function browse(index: int): void {
        root.browsingFor = index
        // Where the entry already points is where to open, which is what the
        // widget list passed to its folder chooser.
        const current = index >= 0 ? String(root.entries()[index] ?? "") : ""
        browser.browse(current, current, false)
    }

    // What the dialog answered goes where the browse started: a new entry at
    // the end, or over the one being edited. Cancelling changes nothing, which
    // is what the widget list did with an empty path.
    function chose(path: string): void {
        const next = root.entries()
        if (root.browsingFor < 0)
            next.push(path)
        else if (root.browsingFor < next.length)
            next[root.browsingFor] = path
        else
            return
        root.aspect.value = next
    }

    PathBrowser {
        id: browser

        pres: root.pres

        onChosen: (paths) => { if (paths.length > 0) root.chose(paths[0]) }
    }

    // Order is the user's on a list where it means something - the URLs a web
    // search filter offers are listed in the order they are tried.
    function moveBy(index: int, offset: int): void {
        const next = root.entries()
        const to = index + offset
        if (index < 0 || index >= next.length || to < 0 || to >= next.length)
            return
        next.splice(to, 0, next.splice(index, 1)[0])
        root.aspect.value = next
    }

    visible: aspectVisible
    spacing: Spacing.GapHM
    Layout.fillWidth: true

    FormLabel {
        text: root.labelText
        Layout.alignment: Qt.AlignTop
    }

    ColumnLayout {
        spacing: Spacing.GapVXs
        Layout.fillWidth: true

        Frame {
            Layout.fillWidth: true
            Layout.preferredHeight: Metrics.formListHeight

            ListView {
                id: view

                anchors.fill: parent
                clip: true
                model: root.entries()
                currentIndex: -1
                ToolTip.text: root.toolTip
                ToolTip.visible: false

                delegate: TextField {
                    id: field

                    required property int index
                    required property string modelData

                    width: view.width
                    text: field.modelData
                    readOnly: !root.pres.allowEditing || !root.editable
                    onActiveFocusChanged: if (activeFocus) view.currentIndex = field.index
                    onEditingFinished: {
                        // Removing a row destroys its field, which loses focus,
                        // which emits this - with the text of the row that is
                        // going away and an index that now belongs to another
                        // row. Writing that back undid the removal: taking out
                        // the first entry put its text on the second. Only a
                        // field the user actually changed has anything to say.
                        if (field.text === field.modelData)
                            return
                        root.replaceAt(field.index, field.text)
                    }
                }
            }
        }

        RowLayout {
            spacing: Spacing.GapHXs

            Button {
                objectName: "stringListAddButton"
                text: root.browsable ? qsTr("Add...") : qsTr("Add")
                visible: root.pres.allowAdding
                enabled: root.editable
                onClicked: {
                    if (root.browsable) {
                        root.browse(-1)
                        return
                    }
                    const next = root.entries()
                    next.push(root.pres.newEntryText ?? "")
                    root.aspect.value = next
                    view.currentIndex = next.length - 1
                    view.itemAtIndex(view.currentIndex)?.forceActiveFocus()
                }
            }

            Button {
                objectName: "stringListEditButton"
                text: qsTr("Edit...")
                visible: root.browsable && root.pres.allowEditing
                enabled: root.editable && view.currentIndex >= 0
                onClicked: root.browse(view.currentIndex)
            }

            Button {
                text: qsTr("Remove")
                visible: root.pres.allowRemoving
                enabled: root.editable && view.currentIndex >= 0
                onClicked: {
                    const next = root.entries()
                    next.splice(view.currentIndex, 1)
                    root.aspect.value = next
                    view.currentIndex = -1
                }
            }

            Button {
                objectName: "stringListMoveUpButton"
                text: qsTr("Move Up")
                visible: root.pres.allowReordering ?? false
                enabled: root.editable && view.currentIndex > 0
                onClicked: {
                    const to = view.currentIndex - 1
                    root.moveBy(view.currentIndex, -1)
                    view.currentIndex = to
                }
            }

            Button {
                objectName: "stringListMoveDownButton"
                text: qsTr("Move Down")
                visible: root.pres.allowReordering ?? false
                enabled: root.editable && view.currentIndex >= 0
                         && view.currentIndex < view.count - 1
                onClicked: {
                    const to = view.currentIndex + 1
                    root.moveBy(view.currentIndex, 1)
                    view.currentIndex = to
                }
            }

            Item { Layout.fillWidth: true }
        }
    }
}
