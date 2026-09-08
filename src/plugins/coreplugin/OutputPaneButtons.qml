// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Controls
import QtCreator.Ui

// The row of output-pane buttons that lives in the status bar. What a button
// says and what pressing it does are the controller's: see
// Core::Internal::OutputPaneButtons.
//
// A Row rather than a ListView: there are a dozen buttons, they never scroll,
// and the row is measured by what it asks for so that the status bar can put
// it beside everything else.
Row {
    id: root

    // Handed over before the source is set, by whichever front end hosts this.
    required property var controller

    objectName: "outputPaneButtons"
    spacing: Spacing.GapHXs

    Repeater {
        model: root.controller.model

        delegate: AbstractButton {
            id: button

            // The whole row, rather than a property per role: "checked" and
            // "display" are names AbstractButton has already.
            required property var model
            required property int index

            objectName: "outputPaneButton"
            // A pane with no priority in the status bar has no button until a
            // reader asks for one from the menu.
            visible: button.model.buttonVisible
            // Bound, not toggled: which button is checked is the manager's
            // answer to what is on screen, so a press asks rather than tells.
            checkable: false
            checked: button.model.checked
            ToolTip.text: button.model.toolTip
            ToolTip.visible: button.hovered && button.model.toolTip !== ""
            ToolTip.delay: 800

            onClicked: root.controller.activate(button.index)

            background: Rectangle {
                color: button.checked ? Tokens.backgroundSubtle
                     : button.hovered ? Tokens.backgroundMuted
                                      : "transparent"
                radius: Spacing.RadiusS

                // What a pane does to be noticed while its button is not the
                // one being read. An animation rather than the widget row's
                // QTimeLine, and the same shape: a second of fading in and
                // out, three times over.
                Rectangle {
                    id: flashOverlay

                    objectName: "outputPaneFlashOverlay"
                    anchors.fill: parent
                    radius: parent.radius
                    color: Tokens.notificationAlertDefault
                    opacity: 0
                }
            }

            SequentialAnimation {
                id: flash

                objectName: "outputPaneFlash"
                loops: 3

                NumberAnimation {
                    target: flashOverlay
                    property: "opacity"
                    from: 0
                    to: 0.36
                    duration: 500
                    easing.type: Easing.InOutSine
                }
                NumberAnimation {
                    target: flashOverlay
                    property: "opacity"
                    from: 0.36
                    to: 0
                    duration: 500
                    easing.type: Easing.InOutSine
                }
            }

            Connections {
                target: root.controller

                function onFlashRequested(row: int): void {
                    // Not the button being read: a reader looking at the pane
                    // does not need to be told about it.
                    if (row === button.index && !button.checked)
                        flash.restart()
                }
            }

            // Reading the pane is the answer to whatever the flash was
            // saying, which is where the widget row stops its timer too.
            onCheckedChanged: {
                if (button.checked) {
                    flash.stop()
                    flashOverlay.opacity = 0
                }
            }

            contentItem: Row {
                spacing: Spacing.GapHXs

                Label {
                    objectName: "outputPaneButtonNumber"
                    text: button.model.number
                    color: Tokens.textMuted
                    verticalAlignment: Text.AlignVCenter
                }

                Label {
                    objectName: "outputPaneButtonName"
                    text: button.model.display
                    color: button.checked ? Tokens.textDefault : Tokens.textMuted
                    verticalAlignment: Text.AlignVCenter
                }

                // What the pane has to report: how many issues, how many
                // failed tests. Nothing to report draws nothing at all.
                Rectangle {
                    objectName: "outputPaneButtonBadge"
                    visible: button.model.badge !== ""
                    width: badgeText.implicitWidth + Spacing.PaddingHS * 2
                    height: badgeText.implicitHeight
                    radius: height / 2
                    color: Tokens.notificationNeutralMuted

                    Label {
                        id: badgeText

                        objectName: "outputPaneButtonBadgeText"
                        anchors.centerIn: parent
                        text: button.model.badge
                        color: Tokens.textDefault
                    }
                }
            }

            // The menu that says which panes have a button at all, on the
            // buttons themselves as well as on the arrow at the end of the
            // row - the widget buttons answer a right-click the same way.
            TapHandler {
                acceptedButtons: Qt.RightButton
                onTapped: root.controller.showMenu()
            }
        }
    }
}
