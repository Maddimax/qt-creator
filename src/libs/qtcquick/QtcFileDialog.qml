// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCreator.Ui

// Choosing a file, on this machine or on a device. QtQuick.Dialogs' FileDialog
// asks the platform, which only knows the machine it runs on, so a field that
// accepts a path on a device has nowhere to send the reader. This asks
// FileBrowser instead, which reaches whatever Utils::FilePath reaches.
//
// A window rather than a Popup: a dialog inside a QQuickWidget cannot be
// larger than the page that opened it.
Window {
    id: root

    enum Mode { OpenFile, OpenDirectory, SaveFile }

    property int mode: QtcFileDialog.OpenFile
    // Where it opens, and what it is called.
    property alias currentFolder: browser.directory
    property alias nameFilters: browser.nameFilters
    // What the reader settled on. Empty until they do.
    property string selectedFile: ""

    signal accepted(string path)
    signal rejected()

    // What the buttons say, which is what the dialog is for.
    readonly property string acceptText: root.mode === QtcFileDialog.SaveFile
                                         ? qsTr("Save") : qsTr("Open")
    readonly property bool choosingDirectory: root.mode === QtcFileDialog.OpenDirectory
    readonly property bool naming: root.mode === QtcFileDialog.SaveFile

    // What accepting now would choose: what is typed if anything is, then the
    // row that is selected, and for a directory dialog the directory itself.
    readonly property string wouldChoose: {
        if (root.naming || nameField.text !== "")
            return nameField.text === "" ? "" : browser.resolve(nameField.text)
        if (list.currentIndex >= 0 && list.currentIndex < browser.entries.rowCount())
            return browser.filePathAt(list.currentIndex)
        return root.choosingDirectory ? browser.directory : ""
    }

    function accept(): void {
        const chosen = root.wouldChoose
        if (chosen === "")
            return
        // A directory chosen in a file dialog is somewhere to go, not an
        // answer: the reader clicked Open on a folder.
        if (!root.choosingDirectory && list.currentIndex >= 0
                && nameField.text === "" && browser.isDirectoryAt(list.currentIndex)) {
            browser.enter(list.currentIndex)
            return
        }
        root.selectedFile = chosen
        root.accepted(chosen)
        root.close()
    }

    function reject(): void {
        root.rejected()
        root.close()
    }

    width: 720
    height: 480
    minimumWidth: 480
    minimumHeight: 320
    modality: Qt.ApplicationModal
    title: root.choosingDirectory ? qsTr("Choose Directory") : qsTr("Choose File")
    color: Tokens.backgroundDefault

    FileBrowser {
        id: browser

        objectName: "fileBrowser"
        // Going somewhere else is a fresh question; what was typed was about
        // the directory that was being looked at.
        onDirectoryChanged: {
            list.currentIndex = -1
            if (!root.naming)
                nameField.text = ""
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Spacing.PaddingVL
        spacing: Spacing.GapVM

        RowLayout {
            spacing: Spacing.GapHM
            Layout.fillWidth: true

            QtcButton {
                objectName: "upButton"
                text: qsTr("Up")
                enabled: browser.canGoUp
                onClicked: browser.goUp()
            }

            QtcLineEdit {
                id: pathField

                objectName: "pathField"
                text: browser.directory
                Layout.fillWidth: true

                onEditingFinished: browser.directory = text
            }

            BusyIndicator {
                objectName: "busyIndicator"
                running: browser.busy
                visible: browser.busy
                implicitWidth: Metrics.formControlHeight
                implicitHeight: Metrics.formControlHeight
            }
        }

        RowLayout {
            spacing: Spacing.GapHM
            Layout.fillWidth: true
            Layout.fillHeight: true

            // Somewhere to start: this machine's usual places, and every
            // device that can be browsed.
            ListView {
                id: places

                objectName: "placesList"
                model: browser.places
                clip: true
                Layout.preferredWidth: Metrics.formLabelWidth
                Layout.fillHeight: true

                ScrollBar.vertical: ScrollBar {}

                delegate: ItemDelegate {
                    required property int index
                    required property string name
                    required property string filePath

                    width: places.width
                    text: name
                    onClicked: browser.directory = filePath
                }
            }

            ColumnLayout {
                spacing: Spacing.GapVS
                Layout.fillWidth: true
                Layout.fillHeight: true

                ListView {
                    id: list

                    objectName: "entryList"
                    model: browser.entries
                    clip: true
                    currentIndex: -1
                    focus: true
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    ScrollBar.vertical: ScrollBar {}

                    delegate: ItemDelegate {
                        id: entry

                        required property int index
                        required property string name
                        required property bool isDir

                        width: list.width
                        text: entry.isDir ? entry.name + "/" : entry.name
                        highlighted: list.currentIndex === entry.index

                        onClicked: {
                            list.currentIndex = entry.index
                            if (!entry.isDir)
                                nameField.text = entry.name
                        }
                        onDoubleClicked: {
                            if (entry.isDir)
                                browser.enter(entry.index)
                            else
                                root.accept()
                        }
                    }

                    Keys.onReturnPressed: root.accept()
                    Keys.onEnterPressed: root.accept()
                }

                Label {
                    objectName: "errorLabel"
                    text: browser.error
                    visible: browser.error !== ""
                    color: Tokens.notificationDangerDefault
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
            }
        }

        RowLayout {
            spacing: Spacing.GapHM
            Layout.fillWidth: true

            Label {
                text: root.naming ? qsTr("Save as:") : qsTr("File name:")
                visible: !root.choosingDirectory
            }

            QtcLineEdit {
                id: nameField

                objectName: "nameField"
                visible: !root.choosingDirectory
                Layout.fillWidth: true

                onAccepted: root.accept()
            }

            Item { Layout.fillWidth: root.choosingDirectory }

            QtcButton {
                objectName: "cancelButton"
                text: qsTr("Cancel")
                onClicked: root.reject()
            }

            QtcButton {
                objectName: "acceptButton"
                text: root.acceptText
                enabled: root.wouldChoose !== ""
                onClicked: root.accept()
            }
        }
    }
}
