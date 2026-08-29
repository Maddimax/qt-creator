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
        // As the model writes them for a reader - "1.2 MB", "C++ source",
        // a date in this locale - rather than as numbers a view would have to
        // format again, and differently from the widget dialog.
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
    // Where to start from: this machine's usual places and every device that
    // can be browsed. The sidebar of the widget dialog, without the widget.
    Q_PROPERTY(QtcQuick::FileEntries *places READ places CONSTANT)
    // The directories the reader put there, kept where the widget dialog
    // keeps them so that both show the same ones.
    Q_PROPERTY(QtcQuick::FileEntries *favorites READ favorites CONSTANT)
    Q_PROPERTY(bool currentIsFavorite READ currentIsFavorite NOTIFY favoritesChanged)
    // Which names to show, as glob patterns - ["*.cpp", "*.h"]. Empty shows
    // everything. Directories are always shown: they are how you get to a file
    // that does match.
    Q_PROPERTY(QStringList nameFilters READ nameFilters WRITE setNameFilters
                   NOTIFY nameFiltersChanged)
    Q_PROPERTY(bool showHiddenFiles READ showHiddenFiles WRITE setShowHiddenFiles
                   NOTIFY showHiddenFilesChanged)
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

public:
    explicit FileBrowser(QObject *parent = nullptr);
    ~FileBrowser() override;

    QString directory() const;
    void setDirectory(const QString &directory);

    FileEntries *entries() const;
    FileEntries *places() const;
    FileEntries *favorites() const;
    bool currentIsFavorite() const;

    QStringList nameFilters() const;
    void setNameFilters(const QStringList &filters);

    bool showHiddenFiles() const;
    void setShowHiddenFiles(bool show);

    QString searchText() const;
    void setSearchText(const QString &text);
    bool isSearching() const;

    bool isBusy() const;
    QString error() const;
    bool canGoUp() const;
    bool canGoBack() const;
    bool canGoForward() const;

    // To the directory above, if there is one.
    Q_INVOKABLE void goUp();
    Q_INVOKABLE void goBack();
    Q_INVOKABLE void goForward();
    // Keeps the directory being looked at, or stops keeping it.
    Q_INVOKABLE void addFavorite(const QString &path);
    Q_INVOKABLE void removeFavorite(const QString &path);
    // A new directory here, named \a name. Answers with its path, or empty
    // when it could not be made.
    Q_INVOKABLE QString createDirectory(const QString &name);
    // Into \a row of entries() when it is a directory. Says whether it went.
    Q_INVOKABLE bool enter(int row);
    // The full path of \a row, for a view that has to hand one back.
    Q_INVOKABLE QString filePathAt(int row) const;
    Q_INVOKABLE bool isDirectoryAt(int row) const;
    // What a name typed into the dialog means here, which is a path relative
    // to the directory being looked at unless it is an absolute one.
    Q_INVOKABLE QString resolve(const QString &name) const;

signals:
    void directoryChanged();
    void nameFiltersChanged();
    void showHiddenFilesChanged();
    void searchTextChanged();
    void searchingChanged();
    void busyChanged();
    void errorChanged();
    void favoritesChanged();
    void historyChanged();

private:
    void rebuildEntries();
    void startSearch();
    void cancelSearch();
    void rebuildPlaces();
    void rebuildFavorites();
    void saveFavorites();
    void recordVisit(const Utils::FilePath &path);

    std::unique_ptr<FileBrowserPrivate> d;
};

} // namespace QtcQuick
