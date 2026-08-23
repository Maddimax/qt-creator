// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCreator.Ui

// A list of sub-aspects with the selected one's settings below it, and Add and
// Remove beside. The counterpart of AspectList's list-with-details style.
RowLayout {
    id: root

    required property Aspect aspect
    // Derived rather than taken as a model role, so that a hand-written page
    // can use this delegate with nothing but the aspect.
    readonly property var itemListModel: aspect ? AspectModels.itemList(aspect) : null
    readonly property string labelText: aspect?.labelText ?? ""
    readonly property bool aspectVisible: aspect?.visible ?? true
    readonly property bool editable: (aspect?.enabled ?? false) && !(aspect?.readOnly ?? false)

    visible: aspectVisible
    spacing: Spacing.GapHM
    Layout.fillWidth: true

    ColumnLayout {
        spacing: Spacing.GapVS
        Layout.fillWidth: true

        Label {
            text: root.labelText
            visible: root.labelText !== ""
        }

        Frame {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: Metrics.formListHeight

            ListView {
                id: view

                anchors.fill: parent
                clip: true
                model: root.itemListModel
                currentIndex: -1

                delegate: ItemDelegate {
                    id: row

                    required property int index
                    required property string label
                    // Carried on the row so that the details pane can read it
                    // from currentItem, rather than calling data() with a role
                    // enum that QML would have to know.
                    required property var itemModel
                    // Not applied yet: struck through when it is on its way
                    // out, bold when it is new, as in the widget editor.
                    required property bool added
                    required property bool removed

                    width: view.width
                    text: label
                    font.strikeout: row.removed
                    font.bold: row.added
                    highlighted: ListView.isCurrentItem
                    onClicked: view.currentIndex = row.index
                }
            }
        }

        // The selected item's own aspects. Loaded by URL because AspectItems
        // instantiates this delegate, and QML rejects two files that name each
        // other as types.
        Loader {
            id: details

            Layout.fillWidth: true
            active: view.currentIndex >= 0

            readonly property var currentItemModel: view.currentItem?.itemModel ?? null

            onCurrentItemModelChanged: {
                if (currentItemModel)
                    setSource("AspectItems.qml", {"model": currentItemModel})
                else
                    setSource("")
            }
        }
    }

    ColumnLayout {
        spacing: Spacing.GapVXs
        Layout.alignment: Qt.AlignTop

        Button {
            text: qsTr("Add")
            enabled: root.editable
            onClicked: view.currentIndex = root.itemListModel.addItem()
        }

        Button {
            text: qsTr("Remove")
            enabled: root.editable && view.currentIndex >= 0
                     && !(view.currentItem?.removed ?? false)
            onClicked: {
                const row = view.currentIndex
                view.currentIndex = -1
                root.itemListModel.removeItem(row)
            }
        }

        Item { Layout.fillHeight: true }
    }
}
