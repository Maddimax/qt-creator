// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "qtcquick_global.h"

#include <utils/filepath.h>

#include <QAbstractListModel>
#include <QObject>
#include <QQmlEngine>

#include <memory>

namespace Utils {
class FileSystemModel;
class FileSystemProxyModel;
}

namespace QtcQuick {

class FileBrowserPrivate;

// What is in a directory, as rows a Quick view can draw. The file system model
// underneath is a tree and asks for a directory's contents in the background;
// this is the one directory the browser is looking at, flattened, filtered and
// sorted the way the widget file dialog sorts it.
class QTCQUICK_EXPORT FileEntries : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("FileEntries comes from a FileBrowser")

public:
    enum Role {
        NameRole = Qt::UserRole + 1,
        FilePathRole,
        IsDirRole,
        SizeRole,
        TypeRole,
        ModifiedRole,
        // What to draw beside the name, as something an Image can load. The
        // provider hands out a QIcon, which QML cannot show.
        IconRole,
        // Whether it can be chosen. A file the name filters do not match is
        // listed but not offered, which is what the widget dialog does when
        // it is not hiding them outright.
        SelectableRole,
    };

    using QAbstractListModel::QAbstractListModel;

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    struct Entry
    {
        QString name;
        Utils::FilePath path;
        bool isDir = false;
        bool selectable = true;
        // Where the row is in the model. What a view shows in its columns is
        // asked for through this when the row is drawn, rather than for every
        // file in the directory: on a directory of eight thousand, asking for
        // all three up front is three quarters of the time it takes to list
        // it, and a dozen rows are on screen.
        QPersistentModelIndex source;
        // The same three for a row that has no index to ask - what a search
        // found, which is not in the directory being looked at. As the model
        // writes them for a reader, "1.2 MB" and a date in this locale,
        // rather than as numbers a view would format again and differently
        // from the widget dialog.
        QString size;
        QString type;
        QString modified;
        bool operator==(const Entry &other) const = default;
    };
    void setEntries(const QList<Entry> &entries);

private:
    QList<Entry> m_entries;
};

// Browsing a filesystem, without a view. A device's filesystem as much as this
// machine's: the paths are Utils::FilePath, so a directory on a remote device
// is listed the same way as one here, which is what the widget file dialog
// does and what a Qt Quick FileDialog cannot do at all.
//
// Everything a dialog needs to *decide* is here; what it looks like is the
// form's business. Nothing in it is a widget, so a Quick dialog can use it.
class QTCQUICK_EXPORT FileBrowser : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    // The directory being looked at, as a path a person would type.
    Q_PROPERTY(QString directory READ directory WRITE setDirectory NOTIFY directoryChanged)
    // What is in it.
    Q_PROPERTY(QtcQuick::FileEntries *entries READ entries CONSTANT)
    // Where to start from: this machine's usual places. The sidebar of the
    // widget dialog, without the widget.
    Q_PROPERTY(QtcQuick::FileEntries *places READ places CONSTANT)
    // Every device that can be browsed, kept apart from the places on this
    // machine because the widget dialog's sidebar lists them under their own
    // heading.
    Q_PROPERTY(QtcQuick::FileEntries *devices READ devices CONSTANT)
    // The directory being looked at and every directory above it, nearest
    // first. The widget dialog's path combo is this: somewhere to jump to
    // that is more than one level up.
    Q_PROPERTY(QtcQuick::FileEntries *ancestors READ ancestors CONSTANT)
    // The directories the reader put there, kept where the widget dialog
    // keeps them so that both show the same ones.
    Q_PROPERTY(QtcQuick::FileEntries *favorites READ favorites CONSTANT)
    Q_PROPERTY(bool currentIsFavorite READ currentIsFavorite NOTIFY favoritesChanged)
    // Which names to show, as glob patterns - ["*.cpp", "*.h"]. Empty shows
    // everything. Directories are always shown: they are how you get to a file
    // that does match.
    Q_PROPERTY(QStringList nameFilters READ nameFilters WRITE setNameFilters
                   NOTIFY nameFiltersChanged)
    // Whether a file the filters do not match is left out of the listing or
    // shown in it and not offered. The widget dialog leaves them out
    // everywhere but macOS, where its listing shows them greyed.
    Q_PROPERTY(bool hideFilteredFiles READ hideFilteredFiles WRITE setHideFilteredFiles
                   NOTIFY hideFilteredFilesChanged)
    // Whether only a directory can be chosen. A file is still listed - it says
    // what is in here - but it is not an answer, which is what the widget
    // dialog does when it is asked for a directory.
    Q_PROPERTY(bool directoriesOnly READ directoriesOnly WRITE setDirectoriesOnly
                   NOTIFY directoriesOnlyChanged)
    Q_PROPERTY(bool showHiddenFiles READ showHiddenFiles WRITE setShowHiddenFiles
                   NOTIFY showHiddenFilesChanged)
    // Which of the two arrangements the dialog draws: the file's name below
    // the listing with the kinds beside it, or - the Mac habit - only above
    // it and only when saving. The widget dialog offers both and keeps the
    // choice under the same key, so the two agree about it.
    Q_PROPERTY(bool classicLayout READ classicLayout WRITE setClassicLayout
                   NOTIFY classicLayoutChanged)
    // What to look for below the directory being looked at. While it is set,
    // entries() is what was found rather than what is in the directory - the
    // same swap the widget dialog makes. Clearing it goes back to the listing.
    Q_PROPERTY(QString searchText READ searchText WRITE setSearchText NOTIFY searchTextChanged)
    Q_PROPERTY(bool searching READ isSearching NOTIFY searchingChanged)
    // Whether a listing is still being fetched. A directory on a device can
    // take a moment, and a dialog that says nothing looks broken.
    Q_PROPERTY(bool busy READ isBusy NOTIFY busyChanged)
    // Why the last directory could not be listed, or empty.
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    // Whether there is anywhere above here to go.
    Q_PROPERTY(bool canGoUp READ canGoUp NOTIFY directoryChanged)
    // Where the reader has been in this dialog, which is what back and
    // forward walk. The widget dialog shares one list between every dialog it
    // opens; this is one browser's own.
    Q_PROPERTY(bool canGoBack READ canGoBack NOTIFY historyChanged)
    Q_PROPERTY(bool canGoForward READ canGoForward NOTIFY historyChanged)
    // Whether the clipboard holds anything that could be pasted here.
    Q_PROPERTY(bool canPaste READ canPaste NOTIFY canPasteChanged)
    // Whether a paste is under way, and what it is copying right now.
    Q_PROPERTY(bool pasting READ isPasting NOTIFY pastingChanged)
    Q_PROPERTY(QString pasteStatus READ pasteStatus NOTIFY pasteStatusChanged)

public:
    explicit FileBrowser(QObject *parent = nullptr);
    ~FileBrowser() override;

    QString directory() const;
    void setDirectory(const QString &directory);

    FileEntries *entries() const;
    FileEntries *places() const;
    FileEntries *devices() const;
    FileEntries *ancestors() const;
    FileEntries *favorites() const;
    bool currentIsFavorite() const;

    QStringList nameFilters() const;
    void setNameFilters(const QStringList &filters);

    bool hideFilteredFiles() const;
    void setHideFilteredFiles(bool hide);
    bool directoriesOnly() const;
    void setDirectoriesOnly(bool on);
    bool showHiddenFiles() const;
    void setShowHiddenFiles(bool show);

    bool classicLayout() const;
    void setClassicLayout(bool on);

    QString searchText() const;
    void setSearchText(const QString &text);
    bool isSearching() const;

    bool isBusy() const;
    QString error() const;
    bool canGoUp() const;
    bool canGoBack() const;
    bool canGoForward() const;
    bool canPaste() const;
    bool isPasting() const;
    QString pasteStatus() const;

    // To the directory above, if there is one.
    Q_INVOKABLE void goUp();
    Q_INVOKABLE void goBack();
    Q_INVOKABLE void goForward();
    // Keeps the directory being looked at, or stops keeping it.
    Q_INVOKABLE void addFavorite(const QString &path);
    Q_INVOKABLE void removeFavorite(const QString &path);
    // Puts the favourite at \a from at \a to. The order is the reader's own
    // - most used at the top - so it is kept where the list is kept.
    Q_INVOKABLE void moveFavorite(int from, int to);
    // A new directory here, named \a name. Answers with its path, or empty
    // when it could not be made.
    Q_INVOKABLE QString createDirectory(const QString &name);
    // Renames \a row to \a name. Says whether it worked; the listing catches
    // up on its own.
    Q_INVOKABLE bool rename(int row, const QString &name);
    // Puts \a row in the bin. Answers with what went wrong, or empty when
    // nothing did - a dialog has to be able to say why nothing happened.
    Q_INVOKABLE QString moveToTrash(int row);
    // Why \a path cannot go in the bin, or empty when it can. Asked before
    // offering to bin something as well as when binning it: an entry that
    // cannot be binned should say so rather than fail when picked.
    Q_INVOKABLE QString whyNotBinned(const QString &path) const;
    // Puts \a rows on the clipboard, as the files they are - which is what
    // pastes them into a file manager as well as back into this dialog.
    Q_INVOKABLE void copyToClipboard(const QList<int> &rows);
    // Copies whatever is on the clipboard into the directory being looked at.
    // A name already taken here gets "<name> copy", so pasting into the
    // directory something came from duplicates rather than overwrites.
    //
    // It runs in the background and says when it is done: a directory of any
    // size, or anything at all on a device, takes long enough that a dialog
    // frozen until it finishes looks broken.
    Q_INVOKABLE void startPaste();
    Q_INVOKABLE void cancelPaste();
    // Into \a row of entries() when it is a directory. Says whether it went.
    Q_INVOKABLE bool enter(int row);
    // The full path of \a row, for a view that has to hand one back.
    Q_INVOKABLE QString filePathAt(int row) const;
    Q_INVOKABLE bool isDirectoryAt(int row) const;
    // What \a row is called, which is not the last part of its path while a
    // search is on: a hit is named by where it is.
    Q_INVOKABLE QString nameAt(int row) const;
    // What a name typed into the dialog means here, which is a path relative
    // to the directory being looked at unless it is an absolute one.
    Q_INVOKABLE QString resolve(const QString &name) const;

signals:
    void directoryChanged();
    void nameFiltersChanged();
    void showHiddenFilesChanged();
    void classicLayoutChanged();
    void searchTextChanged();
    void searchingChanged();
    void busyChanged();
    void errorChanged();
    void hideFilteredFilesChanged();
    void directoriesOnlyChanged();
    void favoritesChanged();
    void historyChanged();
    void canPasteChanged();
    void pastingChanged();
    void pasteStatusChanged();
    // What could not be copied, or empty when all of it was. Also emitted
    // when the reader cancels, listing nothing.
    void pasteFinished(const QString &failed);

private:
    void rebuildEntries();
    void startSearch();
    void cancelSearch();
    void rebuildAncestors();
    void rebuildPlaces();
    void rebuildFavorites();
    void saveFavorites();
    void recordVisit(const Utils::FilePath &path);

    std::unique_ptr<FileBrowserPrivate> d;
};

} // namespace QtcQuick
