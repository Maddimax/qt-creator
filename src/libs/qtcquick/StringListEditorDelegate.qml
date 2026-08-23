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
    required property bool allowAdding
    required property bool allowRemoving
    required property bool allowEditing
    readonly property string labelText: aspect?.labelText ?? ""
    readonly property string toolTip: aspect?.toolTip ?? ""
    readonly property bool aspectVisible: aspect?.visible ?? true
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

    Label {
        text: root.labelText
        Layout.preferredWidth: Metrics.formLabelWidth
        elide: Text.ElideRight
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
                    text: modelData
                    readOnly: !root.allowEditing || !root.editable
                    onActiveFocusChanged: if (activeFocus) view.currentIndex = field.index
                    onEditingFinished: root.replaceAt(field.index, text)
                }
            }
        }

        RowLayout {
            spacing: Spacing.GapHXs

            Button {
                text: qsTr("Add")
                visible: root.allowAdding
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
                visible: root.allowRemoving
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
