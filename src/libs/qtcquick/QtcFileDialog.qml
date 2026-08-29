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
    // The kinds of file on offer, as Qt writes them:
    // "Sources (*.cpp *.h);;All files (*)". The reader picks one; the browser
    // is given that one's patterns. Assigning the patterns directly instead
    // would merge every group, and a group of "*" makes every other group
    // pointless.
    property string nameFilter: ""
    readonly property var filterGroups: {
        const groups = []
        const parts = root.nameFilter === "" ? [] : root.nameFilter.split(";;")
        for (let i = 0; i < parts.length; ++i) {
            const open = parts[i].indexOf("(")
            const close = parts[i].lastIndexOf(")")
            if (open < 0 || close < open)
                continue
            const patterns = []
            const each = parts[i].substring(open + 1, close).trim().split(" ")
            for (let j = 0; j < each.length; ++j) {
                if (each[j] !== "")
                    patterns.push(each[j])
            }
            groups.push({"label": parts[i].trim(), "patterns": patterns})
        }
        return groups
    }
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

    // One group and no choice to make: it still says which files are meant.
    onFilterGroupsChanged: {
        browser.nameFilters = root.filterGroups.length > 0
                ? root.filterGroups[0].patterns : []
        filterBox.currentIndex = root.filterGroups.length > 0 ? 0 : -1
    }

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
        // The browser ends a search when the directory changes; the box that
        // started it has to stop saying it is on.
        onSearchTextChanged: {
            if (browser.searchText === "")
                searchBox.text = ""
        }
    }

    // Naming a new directory. A dialog of its own rather than an inline row:
    // it is asked for rarely and answered at once.
    Dialog {
        id: newFolderPrompt

        objectName: "newFolderPrompt"
        title: qsTr("New Folder")
        anchors.centerIn: Overlay.overlay
        modal: true
        standardButtons: Dialog.Ok | Dialog.Cancel

        onOpened: {
            newFolderName.text = ""
            newFolderName.forceActiveFocus()
        }
        onAccepted: {
            const made = browser.createDirectory(newFolderName.text)
            if (made !== "")
                browser.directory = made
        }

        QtcLineEdit {
            id: newFolderName

            objectName: "newFolderName"
            width: Metrics.lineEditWidth
            onAccepted: newFolderPrompt.accept()
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
                objectName: "backButton"
                text: qsTr("Back")
                enabled: browser.canGoBack
                onClicked: browser.goBack()
            }

            QtcButton {
                objectName: "forwardButton"
                text: qsTr("Forward")
                enabled: browser.canGoForward
                onClicked: browser.goForward()
            }

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

            // Keeping a directory to hand. The same list the widget dialog
            // shows, so one kept here is kept there.
            QtcButton {
                objectName: "favoriteButton"
                text: browser.currentIsFavorite ? qsTr("Unkeep") : qsTr("Keep")
                enabled: browser.directory !== ""
                onClicked: {
                    if (browser.currentIsFavorite)
                        browser.removeFavorite(browser.directory)
                    else
                        browser.addFavorite(browser.directory)
                }
            }

            QtcButton {
                objectName: "newFolderButton"
                text: qsTr("New Folder")
                enabled: browser.directory !== ""
                onClicked: newFolderPrompt.open()
            }

            QtcSearchBox {
                objectName: "searchBox"
                placeholderText: qsTr("Search")
                Layout.preferredWidth: Metrics.lineEditWidth

                onTextChanged: browser.searchText = text
            }

            BusyIndicator {
                objectName: "busyIndicator"
                running: browser.busy || browser.searching
                visible: browser.busy || browser.searching
            }
        }

        RowLayout {
            spacing: Spacing.GapHM
            Layout.fillWidth: true
            Layout.fillHeight: true

            // Somewhere to start: this machine's usual places, and every
            // device that can be browsed.
            ColumnLayout {
                spacing: Spacing.GapVS
                Layout.preferredWidth: Metrics.formLabelWidth
                Layout.fillHeight: true

                Label {
                    text: qsTr("Kept")
                    font: Fonts.captionStrong
                    color: Tokens.textMuted
                    visible: favorites.count > 0
                }

                ListView {
                    id: favorites

                    objectName: "favoritesList"
                    model: browser.favorites
                    clip: true
                    visible: count > 0
                    Layout.fillWidth: true
                    Layout.preferredHeight: Math.min(contentHeight, parent.height / 3)

                    delegate: ItemDelegate {
                        required property string name
                        required property string filePath

                        width: favorites.width
                        text: name
                        onClicked: browser.directory = filePath
                    }
                }

                Label {
                    text: qsTr("Places")
                    font: Fonts.captionStrong
                    color: Tokens.textMuted
                }

                ListView {
                    id: places

                    objectName: "placesList"
                    model: browser.places
                    clip: true
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    ScrollBar.vertical: ScrollBar {}

                    delegate: ItemDelegate {
                        required property string name
                        required property string filePath

                        width: places.width
                        text: name
                        onClicked: browser.directory = filePath
                    }
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

            ComboBox {
                id: filterBox

                objectName: "filterBox"
                model: root.filterGroups
                textRole: "label"
                visible: root.filterGroups.length > 1
                Layout.preferredWidth: Metrics.formControlWidth

                onCurrentIndexChanged: {
                    if (currentIndex >= 0 && currentIndex < root.filterGroups.length)
                        browser.nameFilters = root.filterGroups[currentIndex].patterns
                }
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
