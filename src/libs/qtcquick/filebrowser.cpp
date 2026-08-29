// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "filebrowser.h"

#include "qtciconprovider.h"

#include <utils/fsengine/fileiconprovider.h>
#include <utils/filesystemmodel.h>
#include <utils/fsengine/fsengine.h>
#include <utils/qtcassert.h>

#include <utils/async.h>
#include <utils/fileutils.h>

#include <QClipboard>
#include <QGuiApplication>
#include <QMimeData>

#include <utils/utilstr.h>

#include <QDir>
#include <QFile>
#include <QLocale>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFutureWatcher>
#include <QSettings>
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
    case TypeRole:
        return entry.type;
    case ModifiedRole:
        return entry.modified;
    case IconRole:
        // Asked for when a row is drawn rather than kept on the entry: a
        // directory of ten thousand files would otherwise make ten thousand
        // icons for the handful on screen.
        return QtcQuick::iconUrl(FileIconProvider::icon(entry.path));
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
        {TypeRole, "type"},
        {ModifiedRole, "modified"},
        // Not "icon": a Control has a FINAL icon property, and a delegate
        // that is one cannot take a role of that name at all - every page
        // using the delegate stops loading.
        {IconRole, "iconSource"},
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
    FileEntries m_favorites;
    FilePaths m_favoritePaths;
    // Where the reader has been, and where in that they are. Back and forward
    // walk it; going somewhere new drops whatever was ahead.
    FilePaths m_history;
    int m_historyIndex = -1;
    bool m_walkingHistory = false;
    FilePath m_directory;
    QStringList m_nameFilters;
    QString m_searchText;
    // A recursive walk of a directory, which on a device is slow enough that
    // it has to be cancellable and has to report what it finds as it goes.
    QFuture<FileEntries::Entry> m_search;
    QFutureWatcher<FileEntries::Entry> *m_searchWatcher = nullptr;
    QList<FileEntries::Entry> m_hits;
    QFuture<QString> m_paste;
    QFutureWatcher<QString> *m_pasteWatcher = nullptr;
    QString m_pasteStatus;
    QString m_pasteFailure;
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
    rebuildFavorites();

    // What is on the clipboard decides whether pasting is offered, and it
    // changes while the dialog is open - the reader copies something in
    // another window.
    connect(QGuiApplication::clipboard(), &QClipboard::dataChanged, this,
            &FileBrowser::canPasteChanged);
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
    cancelSearch();
    if (!d->m_searchText.isEmpty()) {
        d->m_searchText.clear();
        emit searchTextChanged();
    }
    d->m_model.setRootPath(path);
    // A view asks the model for a directory's contents when it first shows
    // it. There is no view here, so the browser asks.
    const QModelIndex root = d->m_model.index(0, 0);
    if (root.isValid() && d->m_model.canFetchMore(root))
        d->m_model.fetchMore(root);
    rebuildEntries();
    recordVisit(path);
    emit directoryChanged();
    emit favoritesChanged();
}

FileEntries *FileBrowser::entries() const
{
    return &d->m_entries;
}

FileEntries *FileBrowser::places() const
{
    return &d->m_places;
}

FileEntries *FileBrowser::favorites() const
{
    return &d->m_favorites;
}

bool FileBrowser::currentIsFavorite() const
{
    return d->m_favoritePaths.contains(d->m_directory);
}

QString FileBrowser::searchText() const
{
    return d->m_searchText;
}

void FileBrowser::setSearchText(const QString &text)
{
    if (d->m_searchText == text)
        return;
    d->m_searchText = text;
    if (text.isEmpty()) {
        cancelSearch();
        rebuildEntries();
    } else {
        startSearch();
    }
    emit searchTextChanged();
}

bool FileBrowser::isSearching() const
{
    return d->m_searchWatcher && d->m_searchWatcher->isRunning();
}

void FileBrowser::cancelSearch()
{
    if (!d->m_searchWatcher)
        return;
    const bool was = isSearching();
    d->m_searchWatcher->cancel();
    d->m_searchWatcher->waitForFinished();
    d->m_hits.clear();
    if (was)
        emit searchingChanged();
}

// Everything below the directory being looked at whose name contains what was
// typed. Reported as it is found rather than at the end: a walk of a directory
// on a device can take a while, and a dialog that shows nothing until it
// finishes looks broken.
void FileBrowser::startSearch()
{
    cancelSearch();
    if (d->m_directory.isEmpty() || d->m_searchText.isEmpty())
        return;

    const FilePath root = d->m_directory;
    const QString text = d->m_searchText;
    d->m_search = Utils::asyncRun(
        QThread::LowPriority, [root, text](QPromise<FileEntries::Entry> &promise) {
            // Batched, because reporting every hit on its own floods the GUI
            // thread with deliveries and stalls the very view they are for.
            QList<FileEntries::Entry> batch;
            QElapsedTimer sinceFlush;
            sinceFlush.start();
            const auto flush = [&] {
                if (!batch.isEmpty()) {
                    promise.addResults(batch);
                    batch.clear();
                }
                sinceFlush.restart();
            };

            root.iterateDirectory(
                [&](const FilePath &item, const FilePathInfo &info) {
                    if (promise.isCanceled())
                        return IterationPolicy::Stop;
                    if (item.fileName().contains(text, Qt::CaseInsensitive)) {
                        FileEntries::Entry entry;
                        // Where it is, not just what it is called: two files
                        // of the same name in different directories are one
                        // list here.
                        entry.name = item.relativePathFromDir(root);
                        entry.path = item;
                        entry.isDir = info.fileFlags & FilePathInfo::DirectoryType;
                        if (!entry.isDir)
                            entry.size = QLocale().formattedDataSize(info.fileSize);
                        entry.modified = QLocale().toString(info.lastModified,
                                                            QLocale::ShortFormat);
                        batch.append(entry);
                        if (batch.size() >= 256 || sinceFlush.elapsed() >= 100)
                            flush();
                    }
                    return IterationPolicy::Continue;
                },
                FileFilter({},
                           DirFilterFlag::NoDotAndDotDot | DirFilterFlag::AllEntries,
                           DirIteratorFlag::Subdirectories));
            flush();
        });

    if (!d->m_searchWatcher) {
        d->m_searchWatcher = new QFutureWatcher<FileEntries::Entry>(this);
        connect(d->m_searchWatcher, &QFutureWatcherBase::resultsReadyAt, this,
                [this](int begin, int end) {
                    for (int i = begin; i < end; ++i)
                        d->m_hits.append(d->m_searchWatcher->future().resultAt(i));
                    d->m_entries.setEntries(d->m_hits);
                });
        connect(d->m_searchWatcher, &QFutureWatcherBase::finished, this,
                [this] { emit searchingChanged(); });
    }
    d->m_searchWatcher->setFuture(d->m_search);
    emit searchingChanged();
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

bool FileBrowser::canGoBack() const
{
    return d->m_historyIndex > 0;
}

bool FileBrowser::canGoForward() const
{
    return d->m_historyIndex >= 0 && d->m_historyIndex < int(d->m_history.size()) - 1;
}

void FileBrowser::goUp()
{
    if (canGoUp())
        setDirectory(d->m_directory.parentDir().toUserOutput());
}

void FileBrowser::goBack()
{
    if (!canGoBack())
        return;
    --d->m_historyIndex;
    d->m_walkingHistory = true;
    setDirectory(d->m_history.at(d->m_historyIndex).toUserOutput());
    d->m_walkingHistory = false;
    emit historyChanged();
}

void FileBrowser::goForward()
{
    if (!canGoForward())
        return;
    ++d->m_historyIndex;
    d->m_walkingHistory = true;
    setDirectory(d->m_history.at(d->m_historyIndex).toUserOutput());
    d->m_walkingHistory = false;
    emit historyChanged();
}

// Going somewhere new is the end of whatever was ahead: forward means "back
// to where I came from", and there is no coming back from here yet.
void FileBrowser::recordVisit(const FilePath &path)
{
    if (d->m_walkingHistory || path.isEmpty())
        return;
    if (d->m_historyIndex >= 0 && d->m_history.at(d->m_historyIndex) == path)
        return;
    d->m_history.resize(d->m_historyIndex + 1);
    d->m_history.append(path);
    d->m_historyIndex = int(d->m_history.size()) - 1;
    emit historyChanged();
}

void FileBrowser::addFavorite(const QString &path)
{
    const FilePath favorite = FilePath::fromUserInput(path);
    if (favorite.isEmpty() || d->m_favoritePaths.contains(favorite))
        return;
    d->m_favoritePaths.append(favorite);
    saveFavorites();
    rebuildFavorites();
    emit favoritesChanged();
}

void FileBrowser::removeFavorite(const QString &path)
{
    const FilePath favorite = FilePath::fromUserInput(path);
    if (!d->m_favoritePaths.removeOne(favorite))
        return;
    saveFavorites();
    rebuildFavorites();
    emit favoritesChanged();
}

void FileBrowser::moveFavorite(int from, int to)
{
    const int count = int(d->m_favoritePaths.size());
    if (from < 0 || from >= count || to < 0 || to >= count || from == to)
        return;
    d->m_favoritePaths.move(from, to);
    saveFavorites();
    rebuildFavorites();
    emit favoritesChanged();
}

QString FileBrowser::createDirectory(const QString &name)
{
    if (name.isEmpty() || d->m_directory.isEmpty())
        return {};
    const QModelIndex root = d->m_model.index(0, 0);
    if (!root.isValid())
        return {};
    const QModelIndex made = d->m_model.mkdir(root, name);
    if (!made.isValid())
        return {};
    rebuildEntries();
    return d->m_model.filePath(made).toUserOutput();
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

QString FileBrowser::nameAt(int row) const
{
    return d->m_entries.data(d->m_entries.index(row, 0), FileEntries::NameRole).toString();
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
        entry.size = d->m_proxy.index(row, FileSystemModel::SizeColumn, root).data().toString();
        entry.type = d->m_proxy.index(row, FileSystemModel::TypeColumn, root).data().toString();
        entry.modified
            = d->m_proxy.index(row, FileSystemModel::DateModifiedColumn, root).data().toString();
        rows.append(entry);
    }
    d->m_entries.setEntries(rows);
}

bool FileBrowser::rename(int row, const QString &name)
{
    // What a name may be is the model's to say - it refuses an empty one and
    // one that is not a change - and saying it twice is how the two come to
    // disagree.
    const QString path = filePathAt(row);
    if (path.isEmpty())
        return false;
    const QModelIndex index = d->m_model.index(FilePath::fromUserInput(path));
    if (!index.isValid())
        return false;
    if (!d->m_model.setData(index, name, Qt::EditRole))
        return false;
    rebuildEntries();
    return true;
}

bool FileBrowser::canPaste() const
{
    const QMimeData * const clipboard = QGuiApplication::clipboard()->mimeData();
    return clipboard && clipboard->hasUrls() && !d->m_directory.isEmpty();
}

void FileBrowser::copyToClipboard(const QList<int> &rows)
{
    QList<QUrl> urls;
    for (const int row : rows) {
        const QString path = filePathAt(row);
        if (!path.isEmpty())
            urls << FilePath::fromUserInput(path).toUrl();
    }
    if (urls.isEmpty())
        return;
    auto * const mime = new QMimeData;
    mime->setUrls(urls);
    QGuiApplication::clipboard()->setMimeData(mime);
}

bool FileBrowser::isPasting() const
{
    return d->m_pasteWatcher && d->m_pasteWatcher->isRunning();
}

QString FileBrowser::pasteStatus() const
{
    return d->m_pasteStatus;
}

void FileBrowser::cancelPaste()
{
    if (d->m_pasteWatcher)
        d->m_pasteWatcher->cancel();
}

void FileBrowser::startPaste()
{
    const QMimeData * const clipboard = QGuiApplication::clipboard()->mimeData();
    if (!clipboard || !clipboard->hasUrls() || d->m_directory.isEmpty() || isPasting())
        return;

    const QList<QUrl> urls = clipboard->urls();
    const FilePath into = d->m_directory;
    d->m_paste = Utils::asyncRun([urls, into](QPromise<QString> &promise) {
        QStringList failed;
        for (const QUrl &url : urls) {
            if (promise.isCanceled())
                break;
            const FilePath source = FilePath::fromUrl(url);
            if (source.isEmpty())
                continue;
            const FilePath target = FileUtils::uniqueCopyTarget(source, into);
            // Reported per file so that copying a directory says what it is
            // up to, and so that cancelling takes effect between files
            // rather than only between the things on the clipboard.
            FileUtils::CopyAskingForOverwrite copy([&promise](const FilePath &copied) {
                promise.addResult(copied.fileName());
                return !promise.isCanceled();
            });
            const Result<FileUtils::CopyResult> done
                = FileUtils::copyRecursively(source, target, copy());
            if (!done) {
                failed << source.toUserOutput();
            } else if (*done == FileUtils::CopyResult::Canceled) {
                // Half a directory is not what was asked for.
                target.removeRecursively();
                break;
            }
        }
        // The last result is what went wrong, marked so that it is not read
        // as another file name.
        if (!failed.isEmpty()) {
            promise.addResult(QLatin1Char('\0')
                              + Tr::tr("Could not paste the following:\n%1")
                                    .arg(failed.join(QLatin1Char('\n'))));
        }
    });

    if (!d->m_pasteWatcher) {
        d->m_pasteWatcher = new QFutureWatcher<QString>(this);
        connect(d->m_pasteWatcher, &QFutureWatcherBase::resultsReadyAt, this,
                [this](int begin, int end) {
                    for (int i = begin; i < end; ++i) {
                        const QString result = d->m_pasteWatcher->future().resultAt(i);
                        if (result.startsWith(QLatin1Char('\0'))) {
                            d->m_pasteFailure = result.mid(1);
                            continue;
                        }
                        d->m_pasteStatus = result;
                        emit pasteStatusChanged();
                    }
                });
        connect(d->m_pasteWatcher, &QFutureWatcherBase::finished, this, [this] {
            d->m_pasteStatus.clear();
            emit pasteStatusChanged();
            emit pastingChanged();
            rebuildEntries();
            const QString failed = d->m_pasteFailure;
            d->m_pasteFailure.clear();
            emit pasteFinished(failed);
        });
    }
    d->m_pasteFailure.clear();
    d->m_pasteWatcher->setFuture(d->m_paste);
    emit pastingChanged();
}

QString FileBrowser::whyNotBinned(const QString &path) const
{
    if (path.isEmpty())
        return Tr::tr("There is nothing there to move to the bin.");
    // The bin belongs to the machine Qt Creator is running on. A device has
    // none of its own, and quietly deleting instead of binning is not what
    // was asked for.
    const FilePath target = FilePath::fromUserInput(path);
    if (!target.isLocal())
        return Tr::tr("%1 is on a device, which has no bin.").arg(target.toUserOutput());
    return {};
}

QString FileBrowser::moveToTrash(int row)
{
    const QString path = filePathAt(row);
    if (const QString refusal = whyNotBinned(path); !refusal.isEmpty())
        return refusal;
    const FilePath target = FilePath::fromUserInput(path);
    if (!QFile::moveToTrash(target.toFSPathString()))
        return Tr::tr("Could not move %1 to the bin.").arg(target.toUserOutput());
    rebuildEntries();
    return {};
}

// The same settings the widget dialog keeps them in, so a directory kept in
// one is there in the other.
static const char kFavoritesGroup[] = "FileDialog";
static const char kFavoritesKey[] = "Favorites";

void FileBrowser::rebuildFavorites()
{
    if (d->m_favoritePaths.isEmpty()) {
        QSettings settings;
        settings.beginGroup(QLatin1String(kFavoritesGroup));
        const QStringList paths = settings.value(QLatin1String(kFavoritesKey)).toStringList();
        settings.endGroup();
        for (const QString &path : paths)
            d->m_favoritePaths.append(FilePath::fromString(path));
    }

    QList<FileEntries::Entry> rows;
    for (const FilePath &favorite : std::as_const(d->m_favoritePaths)) {
        if (favorite.isDir())
            rows.append({favorite.fileName(), favorite, true, {}, {}, {}});
    }
    d->m_favorites.setEntries(rows);
}

void FileBrowser::saveFavorites()
{
    QStringList paths;
    for (const FilePath &favorite : std::as_const(d->m_favoritePaths))
        paths << favorite.toFSPathString();
    QSettings settings;
    settings.beginGroup(QLatin1String(kFavoritesGroup));
    settings.setValue(QLatin1String(kFavoritesKey), paths);
    settings.endGroup();
}

void FileBrowser::rebuildPlaces()
{
    QList<FileEntries::Entry> rows;
    const auto addPlace = [&rows](const QString &name, const FilePath &path) {
        if (path.isEmpty())
            return;
        rows.append({name, path, true, {}, {}, {}});
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
