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
        // The classic row has a place for the kinds whether or not the caller
        // named any, and an empty box beside "Kind:" says nothing. The widget
        // dialog falls back to the same entry.
        if (groups.length === 0 && root.classic)
            groups.push({"label": qsTr("All files (*)"), "patterns": ["*"]})
        return groups
    }
    // Which arrangement to draw. Classic names the file below the listing and
    // offers the kinds beside it, whatever the dialog is for; the other only
    // names it when saving, above the listing, the way a Mac dialog does. The
    // browser holds the preference because the widget dialog keeps it in the
    // same place.
    // Not readonly: the browser holds what the reader chose, and a test can
    // ask for either arrangement without writing to their settings.
    property bool classic: browser.classicLayout

    // What the reader typed, wherever they typed it. Each arrangement has its
    // own field and only one is ever visible, so this reads whichever that is
    // rather than driving it: the text is an alias of what is being typed
    // into, and a binding on it would fight the typing.
    readonly property string typedName: root.classic ? nameField.text : saveAsField.text

    function setTypedName(name: string): void {
        nameField.text = name
        saveAsField.text = name
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
    // Whether the enclosing folders are listed by name or by path.
    property bool showingFullPaths: false

    // What accepting now would choose: what is typed if anything is, then the
    // row that is selected, and for a directory dialog the directory itself.
    readonly property string wouldChoose: {
        if (root.naming || root.typedName !== "")
            return root.typedName === "" ? "" : browser.resolve(root.typedName)
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
                && root.typedName === "" && browser.isDirectoryAt(list.currentIndex)) {
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

    width: 900
    height: 560
    minimumWidth: 560
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
                root.setTypedName("")
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

    // What the toolbar used to carry as four more buttons. The widget dialog
    // keeps its view options behind a menu too, and a row of eight buttons
    // was wider than the dialog.
    Menu {
        id: ancestorsMenu

        objectName: "ancestorsMenu"

        Repeater {
            model: browser.ancestors

            delegate: MenuItem {
                required property string name
                required property string filePath
                required property string iconSource

                // What the reader asked to see: the folder's own name, or the
                // whole path of it. "Show full paths in ComboBox", as the
                // widget dialog words the same choice.
                text: root.showingFullPaths ? filePath : name
                icon.source: iconSource
                onTriggered: browser.directory = filePath
            }
        }
    }

    Menu {
        id: optionsMenu

        objectName: "optionsMenu"

        // One entry per view rather than one that changes its wording, and
        // each says whether it is the one in use - as the widget dialog's
        // action group does. Its two icons are a download arrow and a
        // magnifier, which say nothing about either view, so they are left
        // out rather than copied.
        MenuItem {
            objectName: "iconsViewItem"
            text: qsTr("Icons view")
            checkable: true
            checked: root.showingIcons
            onTriggered: {
                root.showingIcons = true
                // Choosing the view already in use flips this entry off and
                // then sets a value that does not change, so nothing
                // re-evaluates the binding above and the entry is left saying
                // the opposite of what is true. The plain toggles below need
                // no such thing: what they write always changes.
                checked = Qt.binding(() => root.showingIcons)
            }
        }

        MenuItem {
            objectName: "listViewItem"
            text: qsTr("List view")
            checkable: true
            checked: !root.showingIcons
            onTriggered: {
                root.showingIcons = false
                checked = Qt.binding(() => !root.showingIcons)
            }
        }

        MenuSeparator {}

        MenuItem {
            objectName: "hiddenItem"
            text: qsTr("Show hidden files")
            icon.source: "image://qtcreator/utils/images/eye_open.png?color=IconsBaseColor"
            checkable: true
            checked: browser.showHiddenFiles
            onTriggered: browser.showHiddenFiles = checked
        }

        MenuItem {
            objectName: "fullPathsItem"
            text: qsTr("Show full paths in enclosing folders")
            checkable: true
            checked: root.showingFullPaths
            onTriggered: root.showingFullPaths = checked
        }

        MenuItem {
            objectName: "hideFilteredItem"
            text: qsTr("Hide filtered files")
            icon.source: "image://qtcreator/utils/images/filtericon.png?color=IconsBaseColor"
            checkable: true
            checked: browser.hideFilteredFiles
            // Only where there is a filter to hide anything: with none, every
            // file matches and the entry would do nothing.
            enabled: browser.nameFilters.length > 0
            onTriggered: browser.hideFilteredFiles = checked
        }

        MenuItem {
            objectName: "classicLayoutItem"
            text: qsTr("Use classic layout")
            checkable: true
            checked: root.classic
            // Kept, so the next dialog opens the way this one was left - and
            // the widget dialog reads the same setting.
            onTriggered: browser.classicLayout = checked
        }

        MenuSeparator {}

        MenuItem {
            objectName: "favoriteItem"
            text: browser.currentIsFavorite ? qsTr("Stop Keeping This Folder")
                                            : qsTr("Keep This Folder")
            enabled: browser.directory !== ""
            onTriggered: {
                if (browser.currentIsFavorite)
                    browser.removeFavorite(browser.directory)
                else
                    browser.addFavorite(browser.directory)
            }
        }

        MenuItem {
            objectName: "newFolderItem"
            text: qsTr("New Folder...")
            enabled: browser.directory !== ""
            onTriggered: newFolderPrompt.open()
        }
    }

    // What can be done to a kept directory.
    Menu {
        id: favoriteMenu

        objectName: "favoriteMenu"
        property int row: -1
        property string path: ""

        MenuItem {
            objectName: "favoriteUpItem"
            text: qsTr("Move Up")
            enabled: favoriteMenu.row > 0
            onTriggered: browser.moveFavorite(favoriteMenu.row, favoriteMenu.row - 1)
        }

        MenuItem {
            objectName: "favoriteDownItem"
            text: qsTr("Move Down")
            enabled: favoriteMenu.row >= 0
                     && favoriteMenu.row < browser.favorites.rowCount() - 1
            onTriggered: browser.moveFavorite(favoriteMenu.row, favoriteMenu.row + 1)
        }

        MenuItem {
            objectName: "favoriteForgetItem"
            text: qsTr("Stop Keeping")
            onTriggered: browser.removeFavorite(favoriteMenu.path)
        }
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

            // Icons, and the same ones the widget dialog uses: a row of four
            // words where it has four glyphs is the first thing that tells
            // the two dialogs apart. Ghost, because its buttons have no
            // border until the pointer is over them.
            QtcButton {
                objectName: "backButton"
                role: QtcButton.Role.MediumGhost
                iconSource: "image://qtcreator/utils/images/prev.png?color=IconsBaseColor"
                ToolTip.text: qsTr("Back")
                ToolTip.visible: hovered
                enabled: browser.canGoBack
                onClicked: browser.goBack()
            }

            QtcButton {
                objectName: "forwardButton"
                role: QtcButton.Role.MediumGhost
                iconSource: "image://qtcreator/utils/images/next.png?color=IconsBaseColor"
                ToolTip.text: qsTr("Forward")
                ToolTip.visible: hovered
                enabled: browser.canGoForward
                onClicked: browser.goForward()
            }

            QtcButton {
                objectName: "upButton"
                role: QtcButton.Role.MediumGhost
                iconSource: "image://qtcreator/utils/images/arrowup.png?color=IconsBaseColor"
                ToolTip.text: qsTr("Go to parent directory")
                ToolTip.visible: hovered
                enabled: browser.canGoUp
                onClicked: browser.goUp()
            }

            // Typing a path rather than walking to it. The widget dialog drops
            // a panel over itself for this and offers it in the arrangement
            // whose path is a bare field; here the field is always there, so
            // the button puts the reader in it.
            QtcButton {
                objectName: "gotoButton"
                role: QtcButton.Role.MediumGhost
                iconSource: "image://qtcreator/utils/images/slash.png?color=PanelTextColorMid"
                ToolTip.text: qsTr("Go to folder")
                ToolTip.visible: hovered
                visible: !root.classic
                onClicked: {
                    pathField.forceActiveFocus()
                    pathField.selectAll()
                }
            }

            QtcLineEdit {
                id: pathField

                objectName: "pathField"
                text: browser.directory
                // The widest thing in the row: it says where you are, and the
                // buttons beside it are two words each.
                Layout.fillWidth: true
                Layout.minimumWidth: Metrics.formControlWidth

                onEditingFinished: browser.directory = text
            }

            // Where you are, and everywhere above it. The widget dialog's
            // path is a combo box whose entries are the ancestors; the path
            // here is a field to type in, so the ancestors hang off a button
            // beside it rather than replacing it.
            QtcButton {
                id: ancestorsButton

                objectName: "ancestorsButton"
                role: QtcButton.Role.MediumGhost
                iconSource: "image://qtcreator/utils/images/arrowdown.png?color=Token_Text_Muted"
                ToolTip.text: qsTr("Go to an enclosing folder")
                ToolTip.visible: hovered
                // Somewhere to be is somewhere to come up from: the chain
                // always holds at least the directory itself.
                enabled: browser.directory !== ""
                onClicked: ancestorsMenu.popup(ancestorsButton, 0, ancestorsButton.height)
            }

            QtcButton {
                objectName: "optionsButton"
                role: QtcButton.Role.MediumGhost
                iconSource: "image://qtcreator/utils/images/settings.png?color=PanelTextColorMid"
                ToolTip.text: qsTr("View options")
                ToolTip.visible: hovered
                onClicked: optionsMenu.popup(0, height)
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

        // The other arrangement names the file here, above the listing and
        // centred over it, and only when there is a name to give. Opening a
        // file needs no field: the listing is the answer.
        RowLayout {
            objectName: "saveAsRow"
            spacing: Spacing.GapHM
            Layout.fillWidth: true
            visible: !root.classic && root.naming

            Item { Layout.fillWidth: true }

            Label {
                objectName: "saveAsLabel"
                text: qsTr("Save As:")
            }

            QtcLineEdit {
                id: saveAsField

                objectName: "saveAsField"
                Layout.preferredWidth: Metrics.formControlWidth * 2

                onAccepted: root.accept()
            }

            Item { Layout.fillWidth: true }
        }

        RowLayout {
            spacing: Spacing.GapHM
            Layout.fillWidth: true
            Layout.fillHeight: true

            // Somewhere to start: this machine's usual places, and every
            // device that can be browsed.
            ColumnLayout {
                spacing: Spacing.GapVS
                // Wide enough for a place's name and no wider: the listing is
                // what the reader came for.
                Layout.preferredWidth: Metrics.lineEditWidth
                Layout.maximumWidth: Metrics.formLabelWidth
                Layout.fillHeight: true

                Label {
                    text: qsTr("Favorites")
                    font: Fonts.captionStrong
                    color: Tokens.textMuted
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
                        id: favorite

                        required property int index
                        required property string name
                        required property string filePath
                        required property string iconSource

                        width: favorites.width
                        text: favorite.name
                        onClicked: browser.directory = favorite.filePath

                        // The model hands out the same icon the widget
                        // dialog's sidebar draws - what the path is, not a
                        // decoration. The delegate's own content item is a
                        // bare Text, so the row is built here.
                        contentItem: SidebarRow {
                            iconSource: favorite.iconSource
                            text: favorite.name
                        }

                        // The order is the reader's own, so there has to be a
                        // way to change it. The widget dialog drags them.
                        TapHandler {
                            acceptedButtons: Qt.RightButton
                            onTapped: {
                                favoriteMenu.row = favorite.index
                                favoriteMenu.path = favorite.filePath
                                favoriteMenu.popup()
                            }
                        }
                    }
                }

                Label {
                    text: qsTr("Locations")
                    font: Fonts.captionStrong
                    color: Tokens.textMuted
                }

                ListView {
                    id: places

                    objectName: "placesList"
                    model: browser.places
                    clip: true
                    Layout.fillWidth: true
                    Layout.preferredHeight: contentHeight

                    delegate: ItemDelegate {
                        id: place

                        required property string name
                        required property string filePath
                        required property string iconSource

                        width: places.width
                        text: place.name
                        onClicked: browser.directory = place.filePath

                        contentItem: SidebarRow {
                            iconSource: place.iconSource
                            text: place.name
                        }
                    }
                }

                // Kept apart from this machine's own places, as the widget
                // dialog keeps them: a device is somewhere else, and saying
                // so is half of what this browser is for.
                Label {
                    text: qsTr("Devices")
                    font: Fonts.captionStrong
                    color: Tokens.textMuted
                    visible: devices.count > 0
                }

                ListView {
                    id: devices

                    objectName: "devicesList"
                    model: browser.devices
                    clip: true
                    visible: count > 0
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    ScrollBar.vertical: ScrollBar {}

                    delegate: ItemDelegate {
                        id: device

                        required property string name
                        required property string filePath
                        required property string iconSource

                        width: devices.width
                        text: device.name
                        onClicked: browser.directory = device.filePath

                        contentItem: SidebarRow {
                            iconSource: device.iconSource
                            text: device.name
                        }
                    }
                }

                // Holds the headings at the top when there is more room than
                // places to put in it, which is where the widget dialog's
                // sidebar keeps them.
                Item { Layout.fillHeight: true }
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
                        text: qsTr("Type")
                        font: Fonts.captionStrong
                        color: Tokens.textMuted
                        Layout.preferredWidth: Metrics.formControlWidth / 2
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
                        required property bool selectable

                        enabled: tile.selectable
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
                                color: tile.selectable ? Tokens.textDefault
                                                       : Tokens.textSubtle
                                elide: Text.ElideMiddle
                                horizontalAlignment: Text.AlignHCenter
                                Layout.fillWidth: true
                            }
                        }

                        onClicked: {
                            grid.currentIndex = tile.index
                            list.currentIndex = tile.index
                            if (!tile.isDir)
                                root.setTypedName(tile.name)
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
                        required property bool selectable

                        width: list.width
                        // Listed but not offered: the reader can see the file
                        // is there and that the filter is why it cannot be
                        // chosen. The widget dialog greys it the same way.
                        enabled: entry.selectable
                        highlighted: list.currentIndex === entry.index
                                     || root.alsoPicked.indexOf(entry.index) >= 0
                        // What it is, for a reader who wants to know without
                        // opening it. The columns say the rest.
                        ToolTip.text: entry.type
                        ToolTip.visible: hovered && entry.type !== ""

                        // Every other row is shaded, as the widget dialog's
                        // view is: on a listing this wide the eye needs
                        // something to carry it from the name to the date.
                        // A shade of its own, not the one hovering uses, or
                        // the pointer would leave half the rows unchanged.
                        background: Rectangle {
                            color: entry.highlighted ? Tokens.accentDefault
                                 : entry.hovered ? Tokens.foregroundSubtle
                                 : entry.index % 2 ? Tokens.backgroundMuted
                                                   : "transparent"
                        }

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
                                text: entry.name
                                // A custom content item draws what it is told
                                // rather than what the delegate's state says.
                                color: entry.selectable ? Tokens.textDefault
                                                        : Tokens.textSubtle
                                elide: Text.ElideMiddle
                                Layout.fillWidth: true
                            }

                            Label {
                                text: entry.size
                                color: Tokens.textMuted
                                horizontalAlignment: Text.AlignRight
                                Layout.preferredWidth: Metrics.lineEditWidth / 2
                            }

                            // What it is, in a column of its own. It was only
                            // a tooltip, which is a thing you have to go
                            // looking for.
                            Label {
                                text: entry.type
                                color: Tokens.textMuted
                                elide: Text.ElideRight
                                Layout.preferredWidth: Metrics.formControlWidth / 2
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
                                root.setTypedName(entry.name)
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
                role: QtcButton.Role.MediumSecondary
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

        // Label beside field, one row each, as the widget dialog's grid has
        // them: on one line the kind's label read as the name field's unit.
        // An invisible item takes no cell, so the arrangement that shows only
        // the kind draws it on the first row rather than under a gap.
        GridLayout {
            columns: 2
            columnSpacing: Spacing.GapHM
            rowSpacing: Spacing.GapVS
            Layout.fillWidth: true

            Label {
                objectName: "nameLabel"
                text: root.naming ? qsTr("Save As:") : qsTr("File name:")
                visible: nameField.visible
            }

            QtcLineEdit {
                id: nameField

                objectName: "nameField"
                // The other arrangement names the file above the listing, and
                // only when there is a name to give.
                visible: root.classic && !root.choosingDirectory
                Layout.fillWidth: true

                onAccepted: root.accept()
            }

            Label {
                objectName: "kindLabel"
                text: qsTr("Files of type:")
                visible: filterBox.visible
            }

            ComboBox {
                id: filterBox

                objectName: "filterBox"
                model: root.filterGroups
                textRole: "label"
                // Classic offers the kinds whatever the caller asked for -
                // falling back to every file - because the row is there
                // anyway. The other only offers a choice worth making.
                visible: root.classic ? root.filterGroups.length > 0
                                      : root.filterGroups.length > 1
                Layout.fillWidth: true

                onCurrentIndexChanged: {
                    if (currentIndex >= 0 && currentIndex < root.filterGroups.length)
                        browser.nameFilters = root.filterGroups[currentIndex].patterns
                }
            }
        }

        RowLayout {
            spacing: Spacing.GapHM
            Layout.fillWidth: true

            // Making one is a thing you do while choosing where to save, so
            // the widget dialog gives it a button of its own rather than only
            // an entry in a menu. It has both; so has this.
            QtcButton {
                objectName: "newFolderButton"
                role: QtcButton.Role.MediumSecondary
                text: qsTr("New Folder")
                enabled: browser.directory !== ""
                onClicked: newFolderPrompt.open()
            }

            Item { Layout.fillWidth: true }

            QtcButton {
                objectName: "cancelButton"
                role: QtcButton.Role.MediumSecondary
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
