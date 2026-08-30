// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCreator.Ui

// A string edited over several lines - a list of commands, an ignore pattern,
// the effective call of a build step. The single-line delegate showed only the
// first line's worth of it.
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

    // The word the cursor is in. A page showing what the option under the
    // cursor means - Beautifier's configuration editor - has no other way to
    // ask: where the cursor is is the view's business and nothing else's.
    readonly property string currentWord: area.word

    Connections {
        target: delegate.aspect
        function onControlConfigurationChanged() {
            delegate.pres = AspectModels.presentation(delegate.aspect)
        }
    }

    visible: aspectVisible
    spacing: Spacing.GapHM
    Layout.fillWidth: true

    FormLabel {
        text: delegate.labelText
        Layout.alignment: Qt.AlignTop
    }

    // Framed: the style dresses a TextField but not a TextArea, so without one
    // a multi-line value is an invisible box under its label.
    Frame {
        id: frame

        Layout.fillWidth: true
        Layout.preferredHeight: Metrics.formTextAreaHeight

        ScrollView {
            anchors.fill: parent
            clip: true

            TextArea {
                id: area

                objectName: "textArea"

                text: delegate.aspect?.value ?? ""
                placeholderText: delegate.pres.placeholderText ?? ""
                enabled: delegate.aspect?.enabled ?? false
                readOnly: delegate.aspect?.readOnly ?? true
                wrapMode: TextEdit.NoWrap
                ToolTip.text: delegate.toolTip
                ToolTip.visible: hovered && delegate.toolTip !== ""

                // TextArea has no editingFinished, and writing on every keystroke
                // would make one undo step per character.
                onActiveFocusChanged: {
                    if (!activeFocus && delegate.aspect && !area.readOnly)
                        delegate.aspect.value = area.text
                }

                // Several lines of text complete against the word the cursor
                // is in, not against everything typed so far.
                readonly property int wordStart: {
                    let at = area.cursorPosition
                    while (at > 0 && /[\w-]/.test(area.text.charAt(at - 1)))
                        --at
                    return at
                }
                readonly property string word:
                    area.text.substring(area.wordStart, area.cursorPosition)

                onTextChanged: if (area.activeFocus) completion.offer()

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
                    prefix: area.word
                    x: area.cursorRectangle.x
                    y: area.cursorRectangle.y + area.cursorRectangle.height

                    onAccepted: (text) => {
                        const before = area.text.substring(0, area.wordStart)
                        const after = area.text.substring(area.cursorPosition)
                        area.text = before + text + after
                        area.cursorPosition = before.length + text.length
                    }
                }
            }
        }

        // The widget chooser puts its button in the top right corner of a
        // text edit, not beside it, and shows it while the edit has the
        // focus. In the frame rather than in the scrolled content: what is
        // scrolled moves, and the button does not.
        QtcIconDisplay {
            objectName: "insertVariableButton"
            anchors.right: parent.right
            anchors.top: parent.top
            z: 1
            iconSource: "image://qtcreator/@name/REPLACE"
            visible: area.activeFocus && !area.readOnly && area.enabled
            ToolTip.text: qsTr("Insert Variable")
            ToolTip.visible: variableHover.hovered

            HoverHandler { id: variableHover }

            TapHandler {
                onTapped: {
                    chooser.variables = AspectModels.variables(delegate.aspect)
                    chooser.offer()
                }
            }
        }

        QtcVariableChooser {
            id: chooser

            variables: null
            x: area.cursorRectangle.x
            y: area.cursorRectangle.y + area.cursorRectangle.height

            onChose: (text) => {
                area.insert(area.cursorPosition, text)
                area.forceActiveFocus()
            }
        }
    }
}
