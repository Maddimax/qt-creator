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
                text: qsTr("Add")
                visible: root.pres.allowAdding
                enabled: root.editable
                onClicked: {
                    const next = root.entries()
                    next.push("")
                    root.aspect.value = next
                    view.currentIndex = next.length - 1
                    view.itemAtIndex(view.currentIndex)?.forceActiveFocus()
                }
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

            Item { Layout.fillWidth: true }
        }
    }
}
