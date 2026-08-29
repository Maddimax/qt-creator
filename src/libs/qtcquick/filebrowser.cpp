// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "filebrowser.h"

#include <utils/filesystemmodel.h>
#include <utils/fsengine/fsengine.h>
#include <utils/qtcassert.h>

#include <QDir>
#include <QRegularExpression>
#include <QStandardPaths>

using namespace Utils;

namespace QtcQuick {

int FileEntries::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_entries.size());
}

QVariant FileEntries::data(const QModelIndex &index, int role) const
{
    if (index.row() < 0 || index.row() >= m_entries.size())
        return {};
    const Entry &entry = m_entries.at(index.row());
    switch (role) {
    case Qt::DisplayRole:
    case NameRole:
        return entry.name;
    case FilePathRole:
        return entry.path.toUserOutput();
    case IsDirRole:
        return entry.isDir;
    case SizeRole:
        return entry.size;
    case ModifiedRole:
        return entry.modified;
    default:
        return {};
    }
}

QHash<int, QByteArray> FileEntries::roleNames() const
{
    return {
        {NameRole, "name"},
        {FilePathRole, "filePath"},
        {IsDirRole, "isDir"},
        {SizeRole, "size"},
        {ModifiedRole, "modified"},
    };
}

void FileEntries::setEntries(const QList<Entry> &entries)
{
    if (entries == m_entries)
        return;
    beginResetModel();
    m_entries = entries;
    endResetModel();
}

class FileBrowserPrivate
{
public:
    FileSystemModel m_model;
    FileSystemProxyModel m_proxy;
    FileEntries m_entries;
    FileEntries m_places;
    FilePath m_directory;
    QStringList m_nameFilters;
    QString m_error;
    bool m_busy = false;
};

FileBrowser::FileBrowser(QObject *parent)
    : QObject(parent)
    , d(new FileBrowserPrivate)
{
    d->m_proxy.setSourceModel(&d->m_model);

    // The listing arrives after the directory is set, so the rows are built
    // when it does rather than when it was asked for.
    connect(&d->m_model, &FileSystemModel::directoryLoaded, this, [this](const FilePath &path) {
        if (path == d->m_directory)
            rebuildEntries();
    });
    connect(&d->m_proxy, &QAbstractItemModel::rowsInserted, this, [this] { rebuildEntries(); });
    connect(&d->m_proxy, &QAbstractItemModel::rowsRemoved, this, [this] { rebuildEntries(); });
    connect(&d->m_proxy, &QAbstractItemModel::modelReset, this, [this] { rebuildEntries(); });

    connect(&d->m_model, &FileSystemModel::fetchingChanged, this,
            [this](const FilePath &path, bool fetching) {
                if (path != d->m_directory)
                    return;
                if (d->m_busy == fetching)
                    return;
                d->m_busy = fetching;
                emit busyChanged();
            });
    connect(&d->m_model, &FileSystemModel::directoryLoadFailed, this,
            [this](const FilePath &path, const QString &error) {
                if (path != d->m_directory)
                    return;
                d->m_error = error;
                emit errorChanged();
            });

    rebuildPlaces();
}

FileBrowser::~FileBrowser() = default;

QString FileBrowser::directory() const
{
    return d->m_directory.toUserOutput();
}

void FileBrowser::setDirectory(const QString &directory)
{
    const FilePath path = FilePath::fromUserInput(directory);
    if (path == d->m_directory)
        return;

    d->m_directory = path;
    if (!d->m_error.isEmpty()) {
        d->m_error.clear();
        emit errorChanged();
    }
    d->m_model.setRootPath(path);
    // A view asks the model for a directory's contents when it first shows
    // it. There is no view here, so the browser asks.
    const QModelIndex root = d->m_model.index(0, 0);
    if (root.isValid() && d->m_model.canFetchMore(root))
        d->m_model.fetchMore(root);
    rebuildEntries();
    emit directoryChanged();
}

FileEntries *FileBrowser::entries() const
{
    return &d->m_entries;
}

FileEntries *FileBrowser::places() const
{
    return &d->m_places;
}

QStringList FileBrowser::nameFilters() const
{
    return d->m_nameFilters;
}

void FileBrowser::setNameFilters(const QStringList &filters)
{
    if (d->m_nameFilters == filters)
        return;
    d->m_nameFilters = filters;
    rebuildEntries();
    emit nameFiltersChanged();
}

bool FileBrowser::showHiddenFiles() const
{
    return d->m_proxy.showHiddenFiles();
}

void FileBrowser::setShowHiddenFiles(bool show)
{
    if (d->m_proxy.showHiddenFiles() == show)
        return;
    d->m_proxy.setShowHiddenFiles(show);
    rebuildEntries();
    emit showHiddenFilesChanged();
}

bool FileBrowser::isBusy() const
{
    return d->m_busy;
}

QString FileBrowser::error() const
{
    return d->m_error;
}

bool FileBrowser::canGoUp() const
{
    return !d->m_directory.isEmpty() && d->m_directory.parentDir() != d->m_directory
           && !d->m_directory.parentDir().isEmpty();
}

void FileBrowser::goUp()
{
    if (canGoUp())
        setDirectory(d->m_directory.parentDir().toUserOutput());
}

bool FileBrowser::enter(int row)
{
    if (!isDirectoryAt(row))
        return false;
    setDirectory(filePathAt(row));
    return true;
}

QString FileBrowser::filePathAt(int row) const
{
    return d->m_entries.data(d->m_entries.index(row, 0), FileEntries::FilePathRole).toString();
}

bool FileBrowser::isDirectoryAt(int row) const
{
    return d->m_entries.data(d->m_entries.index(row, 0), FileEntries::IsDirRole).toBool();
}

QString FileBrowser::resolve(const QString &name) const
{
    if (name.isEmpty())
        return {};
    const FilePath typed = FilePath::fromUserInput(name);
    // An absolute path means itself; anything else is meant relative to what
    // is being looked at, which is how a file name typed into a dialog reads.
    if (typed.isAbsolutePath())
        return typed.toUserOutput();
    return d->m_directory.resolvePath(name).toUserOutput();
}

// Whether \a name is one of the kinds of file the dialog was asked for. A
// directory always is: it is how the reader reaches a file that matches.
static bool matchesFilters(const QString &name, const QStringList &filters)
{
    if (filters.isEmpty())
        return true;
    for (const QString &filter : filters) {
        const QRegularExpression pattern(
            QRegularExpression::wildcardToRegularExpression(filter),
            QRegularExpression::CaseInsensitiveOption);
        if (pattern.match(name).hasMatch())
            return true;
    }
    return false;
}

void FileBrowser::rebuildEntries()
{
    QList<FileEntries::Entry> rows;
    // After setRootPath() the directory being looked at is the model's first
    // row, and its children are what is in it - index(path) is not it.
    const QModelIndex root = d->m_proxy.mapFromSource(d->m_model.index(0, 0));
    const int count = d->m_directory.isEmpty() ? 0 : d->m_proxy.rowCount(root);
    for (int row = 0; row < count; ++row) {
        const QModelIndex index = d->m_proxy.index(row, 0, root);
        const QModelIndex source = d->m_proxy.mapToSource(index);
        FileEntries::Entry entry;
        entry.name = source.data(FileSystemModel::FileNameRole).toString();
        entry.path = d->m_model.filePath(source);
        entry.isDir = d->m_model.isDir(source);
        if (!entry.isDir && !matchesFilters(entry.name, d->m_nameFilters))
            continue;
        entry.size = d->m_proxy.index(row, FileSystemModel::SizeColumn, root).data().toLongLong();
        entry.modified
            = d->m_proxy.index(row, FileSystemModel::DateModifiedColumn, root).data().toString();
        rows.append(entry);
    }
    d->m_entries.setEntries(rows);
}

void FileBrowser::rebuildPlaces()
{
    QList<FileEntries::Entry> rows;
    const auto addPlace = [&rows](const QString &name, const FilePath &path) {
        if (path.isEmpty())
            return;
        rows.append({name, path, true, 0, {}});
    };

    using SP = QStandardPaths;
    const SP::StandardLocation locations[] = {
        SP::HomeLocation,
        SP::DesktopLocation,
        SP::DocumentsLocation,
        SP::DownloadLocation,
    };
    for (SP::StandardLocation location : locations) {
        const QString path = SP::writableLocation(location);
        if (!path.isEmpty() && FilePath::fromString(path).isDir())
            addPlace(SP::displayName(location), FilePath::fromString(path));
    }

    // The point of this browser: a device is a place to start from, listed
    // beside the ones on this machine. Only those that can actually be
    // browsed - a device with no file access is a dead entry.
    for (const FilePath &root : FSEngine::registeredDeviceRoots()) {
        if (root.isLocal() || !root.hasFileAccess())
            continue;
        addPlace(root.host().toString(), root);
    }

    d->m_places.setEntries(rows);
}

} // namespace QtcQuick
