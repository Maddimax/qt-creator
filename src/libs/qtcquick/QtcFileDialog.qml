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

    enum Mode { OpenFile, OpenDirectory, SaveFile, OpenFiles }

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
    // Icons or a list, which the widget dialog also offers. Kept for next
    // time, because it is a preference and not a per-dialog decision.
    property bool showingIcons: false

    // What went wrong with the last thing the reader asked for, which the
    // browser's own error does not cover: it is about listing a directory,
    // and this is about renaming or binning something in one.
    property string trouble: ""

    // What the reader settled on. Empty until they do.
    property string selectedFile: ""
    property var selectedFiles: []

    // Always a list, even where only one can be chosen: a caller that wants
    // one takes the first, and there is one shape to remember rather than
    // two.
    signal accepted(list<string> paths)
    signal rejected()

    // What the buttons say, which is what the dialog is for.
    readonly property string acceptText: root.mode === QtcFileDialog.SaveFile
                                         ? qsTr("Save") : qsTr("Open")
    readonly property bool choosingDirectory: root.mode === QtcFileDialog.OpenDirectory
    readonly property bool naming: root.mode === QtcFileDialog.SaveFile
    // Several at once - what a list of paths is added to from.
    readonly property bool choosingSeveral: root.mode === QtcFileDialog.OpenFiles
    // The rows picked with Ctrl or Command held, when several may be. Plain
    // clicking picks one and forgets the rest.
    property var alsoPicked: []

    // What accepting now would choose: what is typed if anything is, then the
    // row that is selected, and for a directory dialog the directory itself.
    readonly property string wouldChoose: {
        if (root.naming || nameField.text !== "")
            return nameField.text === "" ? "" : browser.resolve(nameField.text)
        if (list.currentIndex >= 0 && list.currentIndex < browser.entries.rowCount())
            return browser.filePathAt(list.currentIndex)
        return root.choosingDirectory ? browser.directory : ""
    }

    // Everything accepting now would choose. One entry for every mode but
    // OpenFiles, where it is what was picked with Ctrl or Command held.
    readonly property var wouldChooseAll: {
        if (!root.choosingSeveral || root.alsoPicked.length === 0)
            return root.wouldChoose === "" ? [] : [root.wouldChoose]
        const all = []
        for (let i = 0; i < root.alsoPicked.length; ++i)
            all.push(browser.filePathAt(root.alsoPicked[i]))
        return all
    }

    // The rows an action acts on: everything picked when \a row is one of
    // them, and \a row alone otherwise - right-clicking an entry outside the
    // selection acts on that entry, as it does in a file manager.
    function pickedRows(row: int): list<int> {
        if (root.alsoPicked.indexOf(row) >= 0)
            return root.alsoPicked
        return row >= 0 ? [row] : []
    }

    function accept(): void {
        const chosen = root.wouldChooseAll
        if (chosen.length === 0)
            return
        // A directory chosen in a file dialog is somewhere to go, not an
        // answer: the reader clicked Open on a folder.
        if (!root.choosingDirectory && list.currentIndex >= 0
                && root.alsoPicked.length === 0
                && nameField.text === "" && browser.isDirectoryAt(list.currentIndex)) {
            browser.enter(list.currentIndex)
            return
        }
        root.selectedFile = chosen[0]
        root.selectedFiles = chosen
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

        // A copy that failed says so where the other troubles are said.
        onPasteFinished: (failed) => {
            if (failed !== "")
                root.trouble = failed
        }
        // Going somewhere else is a fresh question; what was typed was about
        // the directory that was being looked at.
        onDirectoryChanged: {
            list.currentIndex = -1
            root.alsoPicked = []
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

    // Getting about by keyboard, with the sequences the widget dialog binds
    // and the platform's own where there is one.
    Shortcut {
        objectName: "backShortcut"
        sequences: [StandardKey.Back]
        onActivated: browser.goBack()
    }

    Shortcut {
        objectName: "forwardShortcut"
        sequences: [StandardKey.Forward]
        onActivated: browser.goForward()
    }

    Shortcut {
        objectName: "parentShortcut"
        // Cmd+Up on macOS, Alt+Up on Windows, Ctrl+Up elsewhere - which is
        // what these two sequences come to.
        sequences: ["Ctrl+Up", "Alt+Up"]
        onActivated: browser.goUp()
    }

    Shortcut {
        objectName: "gotoShortcut"
        // The widget dialog drops a panel over itself to type a path into.
        // The path is always in a field here, so this puts the reader in it
        // with what is there selected, which is the same thing with less of
        // it.
        sequences: [Qt.platform.os === "osx" ? "Ctrl+Shift+G" : "Ctrl+L"]
        onActivated: {
            pathField.forceActiveFocus()
            pathField.selectAll()
        }
    }

    Shortcut {
        objectName: "renameShortcut"
        sequences: ["F2"]
        enabled: list.currentIndex >= 0
        onActivated: {
            renamePrompt.row = list.currentIndex
            renameName.text = browser.nameAt(list.currentIndex)
            entryMenu.entryName = renameName.text
            renamePrompt.open()
        }
    }

    Shortcut {
        objectName: "copyShortcut"
        sequences: [StandardKey.Copy]
        enabled: list.currentIndex >= 0
        onActivated: browser.copyToClipboard(root.pickedRows(list.currentIndex))
    }

    Shortcut {
        objectName: "pasteShortcut"
        sequences: [StandardKey.Paste]
        enabled: browser.canPaste && !browser.pasting
        onActivated: browser.startPaste()
    }

    Shortcut {
        objectName: "closeShortcut"
        sequences: [StandardKey.Cancel]
        onActivated: root.reject()
    }

    // What can be done to the entry under the pointer.
    Menu {
        id: entryMenu

        objectName: "entryMenu"
        property int row: -1
        property string entryName: ""

        MenuItem {
            objectName: "renameItem"
            text: qsTr("Rename...")
            onTriggered: {
                renamePrompt.row = entryMenu.row
                renameName.text = entryMenu.entryName
                renamePrompt.open()
            }
        }

        MenuItem {
            objectName: "copyItem"
            text: qsTr("Copy")
            onTriggered: browser.copyToClipboard(root.pickedRows(entryMenu.row))
        }

        MenuItem {
            objectName: "pasteItem"
            text: qsTr("Paste")
            enabled: browser.canPaste && !browser.pasting
            onTriggered: browser.startPaste()
        }

        MenuItem {
            objectName: "trashItem"
            text: qsTr("Move to Bin")
            onTriggered: {
                const failed = browser.moveToTrash(entryMenu.row)
                if (failed !== "")
                    root.trouble = failed
            }
        }
    }

    // Renaming. A prompt rather than editing in place: the row is a delegate
    // with three columns, and an editor in the middle of one is a lot of
    // machinery for something asked for rarely.
    Dialog {
        id: renamePrompt

        objectName: "renamePrompt"
        property int row: -1

        title: qsTr("Rename")
        anchors.centerIn: Overlay.overlay
        modal: true
        standardButtons: Dialog.Ok | Dialog.Cancel

        onOpened: renameName.forceActiveFocus()
        onAccepted: {
            if (!browser.rename(renamePrompt.row, renameName.text))
                root.trouble = qsTr("Could not rename %1.").arg(entryMenu.entryName)
        }

        QtcLineEdit {
            id: renameName

            objectName: "renameName"
            width: Metrics.lineEditWidth
            onAccepted: renamePrompt.accept()
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

            QtcButton {
                objectName: "viewModeButton"
                text: root.showingIcons ? qsTr("List") : qsTr("Icons")
                onClicked: root.showingIcons = !root.showingIcons
            }

            QtcButton {
                objectName: "hiddenButton"
                text: browser.showHiddenFiles ? qsTr("Hide Hidden") : qsTr("Show Hidden")
                onClicked: browser.showHiddenFiles = !browser.showHiddenFiles
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

                // What each column is. A file dialog that shows only names
                // makes the reader open a file to find out which one it is.
                RowLayout {
                    spacing: Spacing.GapHM
                    visible: !root.showingIcons
                    Layout.fillWidth: true

                    Label {
                        text: qsTr("Name")
                        font: Fonts.captionStrong
                        color: Tokens.textMuted
                        Layout.fillWidth: true
                    }

                    Label {
                        text: qsTr("Size")
                        font: Fonts.captionStrong
                        color: Tokens.textMuted
                        horizontalAlignment: Text.AlignRight
                        Layout.preferredWidth: Metrics.lineEditWidth / 2
                    }

                    Label {
                        text: qsTr("Date Modified")
                        font: Fonts.captionStrong
                        color: Tokens.textMuted
                        Layout.preferredWidth: Metrics.formControlWidth / 2
                    }
                }

                GridView {
                    id: grid

                    objectName: "entryGrid"
                    model: browser.entries
                    visible: root.showingIcons
                    clip: true
                    currentIndex: -1
                    cellWidth: Metrics.formControlWidth / 2
                    cellHeight: Metrics.formControlWidth / 2
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    ScrollBar.vertical: ScrollBar {}

                    delegate: ItemDelegate {
                        id: tile

                        required property int index
                        required property string name
                        required property bool isDir
                        required property string iconSource

                        width: grid.cellWidth
                        height: grid.cellHeight
                        highlighted: grid.currentIndex === tile.index

                        contentItem: ColumnLayout {
                            spacing: Spacing.GapVS

                            Image {
                                source: tile.iconSource
                                sourceSize.width: Metrics.colorSwatchSize
                                sourceSize.height: Metrics.colorSwatchSize
                                fillMode: Image.PreserveAspectFit
                                Layout.alignment: Qt.AlignHCenter
                                Layout.preferredWidth: Metrics.colorSwatchSize
                                Layout.preferredHeight: Metrics.colorSwatchSize
                            }

                            Label {
                                text: tile.name
                                elide: Text.ElideMiddle
                                horizontalAlignment: Text.AlignHCenter
                                Layout.fillWidth: true
                            }
                        }

                        onClicked: {
                            grid.currentIndex = tile.index
                            list.currentIndex = tile.index
                            if (!tile.isDir)
                                nameField.text = tile.name
                        }
                        onDoubleClicked: {
                            if (tile.isDir)
                                browser.enter(tile.index)
                            else
                                root.accept()
                        }
                    }
                }

                ListView {
                    id: list

                    objectName: "entryList"
                    model: browser.entries
                    visible: !root.showingIcons
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
                        required property string size
                        required property string modified
                        required property string type
                        required property string iconSource

                        width: list.width
                        highlighted: list.currentIndex === entry.index
                                     || root.alsoPicked.indexOf(entry.index) >= 0
                        // What it is, for a reader who wants to know without
                        // opening it. The columns say the rest.
                        ToolTip.text: entry.type
                        ToolTip.visible: hovered && entry.type !== ""

                        contentItem: RowLayout {
                            spacing: Spacing.GapHM

                            Image {
                                source: entry.iconSource
                                sourceSize.width: Metrics.listRowIconSize
                                sourceSize.height: Metrics.listRowIconSize
                                fillMode: Image.PreserveAspectFit
                                Layout.preferredWidth: Metrics.listRowIconSize
                                Layout.preferredHeight: Metrics.listRowIconSize
                            }

                            Label {
                                text: entry.isDir ? entry.name + "/" : entry.name
                                elide: Text.ElideMiddle
                                Layout.fillWidth: true
                            }

                            Label {
                                text: entry.size
                                color: Tokens.textMuted
                                horizontalAlignment: Text.AlignRight
                                Layout.preferredWidth: Metrics.lineEditWidth / 2
                            }

                            Label {
                                text: entry.modified
                                color: Tokens.textMuted
                                elide: Text.ElideRight
                                Layout.preferredWidth: Metrics.formControlWidth / 2
                            }
                        }

                        onClicked: (mouse) => {
                            const adding = root.choosingSeveral
                                && (mouse.modifiers & (Qt.ControlModifier | Qt.MetaModifier)) !== 0
                            if (adding) {
                                const picked = root.alsoPicked.slice()
                                const at = picked.indexOf(entry.index)
                                if (at >= 0)
                                    picked.splice(at, 1)
                                else
                                    picked.push(entry.index)
                                root.alsoPicked = picked
                            } else {
                                root.alsoPicked = []
                            }
                            list.currentIndex = entry.index
                            if (!entry.isDir && !adding)
                                nameField.text = entry.name
                        }
                        // Right-clicking acts on the entry under the pointer
                        // without changing what is selected, the way the
                        // widget dialog's menu does.
                        TapHandler {
                            acceptedButtons: Qt.RightButton
                            onTapped: {
                                entryMenu.row = entry.index
                                entryMenu.entryName = entry.name
                                entryMenu.popup()
                            }
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

                RowLayout {
                    objectName: "pasteProgress"
                    spacing: Spacing.GapHM
                    visible: browser.pasting
                    Layout.fillWidth: true

                    BusyIndicator {
                        running: browser.pasting
                        implicitWidth: Metrics.listRowIconSize
                        implicitHeight: Metrics.listRowIconSize
                    }

                    Label {
                        objectName: "pasteStatusLabel"
                        text: browser.pasteStatus !== ""
                              ? qsTr("Copying %1").arg(browser.pasteStatus)
                              : qsTr("Copying")
                        elide: Text.ElideMiddle
                        color: Tokens.textMuted
                        Layout.fillWidth: true
                    }

                    QtcButton {
                        objectName: "cancelPasteButton"
                        text: qsTr("Cancel")
                        onClicked: browser.cancelPaste()
                    }
                }

                Label {
                    objectName: "errorLabel"
                    text: browser.error !== "" ? browser.error : root.trouble
                    visible: text !== ""
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
                enabled: root.wouldChooseAll.length > 0
                onClicked: root.accept()
            }
        }
    }
}
