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

    required property AspectList aspect
    // Derived rather than taken as a model role, so that a hand-written page
    // can use this delegate with nothing but the aspect.
    readonly property var itemListModel: aspect ? AspectModels.itemList(aspect) : null
    readonly property var pres: aspect ? AspectModels.presentation(aspect) : ({})
    readonly property string labelText: aspect?.plainLabelText ?? ""
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

                // Which item is current is the aspect's answer, not the
                // view's: the buttons act on it, and Move Up and Move Down put
                // it somewhere else.
                onCurrentIndexChanged: if (root.aspect) root.aspect.currentIndex = currentIndex

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
                    // What the list says about the item besides its name: the
                    // icon it stands for, and the colour it means. Both unset
                    // unless the aspect's listViewDataCallback answers them.
                    required property url decoration
                    required property var itemForeground

                    width: view.width
                    // Not drawn - the content item below is - but read out, and
                    // the delegate's own text is what an accessible name comes
                    // from.
                    text: label
                    highlighted: ListView.isCurrentItem
                    onClicked: view.currentIndex = row.index

                    contentItem: RowLayout {
                        spacing: Spacing.GapHXs

                        Image {
                            source: row.decoration
                            visible: String(row.decoration) !== ""
                            fillMode: Image.PreserveAspectFit
                            sourceSize.width: Metrics.listRowIconSize
                            sourceSize.height: Metrics.listRowIconSize
                            Layout.preferredWidth: Metrics.listRowIconSize
                            Layout.preferredHeight: Metrics.listRowIconSize
                        }

                        Label {
                            objectName: "aspectListRowLabel"
                            text: row.label
                            // The list's own colour where it has one; a
                            // keyword shown in the colour it marks code with.
                            color: row.itemForeground ?? Tokens.textDefault
                            font.strikeout: row.removed
                            font.bold: row.added
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                    }
                }
            }
        }

        // The selected item's own aspects. Loaded by URL because AspectItems
        // instantiates this delegate, and QML rejects two files that name each
        // other as types.
        // The aspect can move the current item - Move Up and Move Down do -
        // and the view has to follow, which is the same round trip the widget
        // list makes.
        Connections {
            target: root.aspect

            function onCurrentIndexChanged(index: int): void {
                if (view.currentIndex !== index)
                    view.currentIndex = index
            }
        }

        Loader {
            id: details

            Layout.fillWidth: true
            active: view.currentIndex >= 0

            // The row carries these; a ListView hands back a QQuickItem.
            // qmllint disable missing-property
            readonly property var currentItemModel: view.currentItem?.itemModel ?? null
            // qmllint enable missing-property

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
            // qmllint disable missing-property
            enabled: root.editable && view.currentIndex >= 0
                     && !(view.currentItem?.removed ?? false)
            // qmllint enable missing-property
            onClicked: {
                const row = view.currentIndex
                view.currentIndex = -1
                root.itemListModel.removeItem(row)
            }
        }

        // Order is the user's on a list where it means something - path
        // mappings are tried in order. What may be moved where is the aspect's
        // answer, the same one the widget list gets.
        Button {
            objectName: "aspectListMoveUpButton"
            text: qsTr("Move Up")
            visible: root.pres.allowReordering ?? false
            enabled: root.editable && (root.aspect?.canMoveUp ?? false)
            onClicked: root.aspect.moveCurrentUp()
        }

        Button {
            objectName: "aspectListMoveDownButton"
            text: qsTr("Move Down")
            visible: root.pres.allowReordering ?? false
            enabled: root.editable && (root.aspect?.canMoveDown ?? false)
            onClicked: root.aspect.moveCurrentDown()
        }

        Repeater {
            model: root.itemListModel?.extraButtons ?? []

            delegate: Button {
                id: extra

                required property int index
                required property string modelData

                text: extra.modelData
                enabled: root.editable
                onClicked: root.itemListModel.triggerExtraButton(extra.index)
            }
        }

        Item { Layout.fillHeight: true }
    }
}
