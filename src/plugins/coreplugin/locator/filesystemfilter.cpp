// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "filesystemfilter.h"

#include "../coreplugintr.h"
#include "../dialogs/ioptionspage.h"
#include "../documentmanager.h"
#include "../editormanager/editormanager.h"
#include "../icore.h"
#include "../vcsmanager.h"
#include "locatormanager.h"

#include <utils/algorithm.h>
#include <utils/async.h>
#include <utils/checkablemessagebox.h>
#include <utils/environment.h>
#include <utils/filepath.h>
#include <utils/fsengine/fileiconprovider.h>
#include <utils/link.h>

#ifdef WITH_TESTS
#include <QTest>
#endif

#include <QApplication>
#include <QDir>
#include <QJsonObject>
#include <QPushButton>
#include <QRegularExpression>
#include <QStyle>

using namespace QtTaskTree;
using namespace Utils;

namespace Core::Internal {

Q_GLOBAL_STATIC(QIcon, sDeviceRootIcon);

static const char kAlwaysCreate[] = "Locator/FileSystemFilter/AlwaysCreate";

static ILocatorFilter::MatchLevel matchLevelFor(const QRegularExpressionMatch &match,
                                                const QString &matchText)
{
    const int consecutivePos = match.capturedStart(1);
    if (consecutivePos == 0)
        return ILocatorFilter::MatchLevel::Best;
    if (consecutivePos > 0) {
        const QChar prevChar = matchText.at(consecutivePos - 1);
        if (prevChar == '_' || prevChar == '.')
            return ILocatorFilter::MatchLevel::Better;
    }
    if (match.capturedStart() == 0)
        return ILocatorFilter::MatchLevel::Good;
    return ILocatorFilter::MatchLevel::Normal;
}

static bool askForCreating(const QString &title, const FilePath &filePath)
{
    QMessageBox::StandardButton selected
        = CheckableMessageBox::question(title,
                                        Tr::tr("Create \"%1\"?").arg(filePath.shortNativePath()),
                                        Key(kAlwaysCreate),
                                        QMessageBox::Yes | QMessageBox::Cancel,
                                        QMessageBox::Cancel,
                                        QMessageBox::Yes,
                                        {{QMessageBox::Yes, Tr::tr("Create")}},
                                        Tr::tr("Always create"));
    return selected == QMessageBox::Yes;
}

static void createAndOpenFile(const FilePath &filePath)
{
    if (!filePath.exists()) {
        if (askForCreating(Tr::tr("Create File"), filePath)) {
            QFile file(filePath.toFSPathString());
            if (!file.open(QFile::WriteOnly)) {
                QMessageBox::warning(ICore::dialogParent(),
                                     Tr::tr("Cannot Create File"),
                                     Tr::tr("Cannot create file \"%1\".")
                                         .arg(filePath.toUserOutput()));
                return;
            }
            file.close();
            VcsManager::promptToAdd(filePath.absolutePath(), {filePath});
        }
    }
    if (filePath.exists())
        EditorManager::openEditor(filePath);
}

static bool createDirectory(const FilePath &filePath)
{
    if (!filePath.exists()) {
        if (askForCreating(Tr::tr("Create Directory"), filePath))
            filePath.createDir();
    }
    if (filePath.exists())
        return true;
    return false;
}

static FilePaths deviceRoots()
{
    const QString rootPath = FilePath::specialRootPath();
    const QStringList roots = QDir(rootPath).entryList();
    FilePaths devices;
    for (const QString &root : roots) {
        const QString prefix = rootPath + '/' + root;
        devices += Utils::transform(QDir(prefix).entryList(), [prefix](const QString &s) {
            return FilePath::fromString(prefix + '/' + s);
        });
    }
    return devices;
}

static std::pair<FilePaths, FilePaths> dirEntries(const FilePath &dir, const FileFilter &filter)
{
    FilePaths dirs;
    FilePaths files;
    dir.iterateDirectory(
        [&dirs, &files](const FilePath &item, const FilePathInfo &info) {
            if (info.fileFlags & FilePathInfo::DirectoryType)
                dirs.append(item);
            else if (info.fileFlags & FilePathInfo::FileType)
                files.append(item);
            return IterationPolicy::Continue;
        },
        filter);
    dirs.sort();
    files.sort();
    return {FilePaths({dir / ".."}) + dirs, files};
}

FileSystemFilter::FileSystemFilter()
{
    setId("Files in file system");
    setDisplayName(Tr::tr("Files in File System"));
    setDescription(Tr::tr("Opens a file given by a relative path to the current document, or absolute "
                      "path. \"~\" refers to your home directory. You have the option to create a "
                      "file if it does not exist yet."));
    setDefaultShortcutString("f");
    *sDeviceRootIcon = qApp->style()->standardIcon(QStyle::SP_DriveHDIcon);
}

static void matches(QPromise<void> &promise, const LocatorStorage &storage,
                    const QString &shortcutString, const FilePath &currentDocumentDir,
                    bool includeHidden)
{
    const QString input = storage.input();
    LocatorFilterEntries entries[int(ILocatorFilter::MatchLevel::Count)];

    const Environment env = Environment::systemEnvironment();
    const QString expandedEntry = env.expandVariables(input);
    const auto expandedEntryPath = FilePath::fromUserInput(expandedEntry);
    const FilePath absoluteEntryPath = currentDocumentDir.isEmpty()
                                           ? expandedEntryPath
                                           : currentDocumentDir.resolvePath(expandedEntryPath);
    // The case of e.g. "ssh://", "ssh://*p", etc
    const bool isPartOfDeviceRoot = !expandedEntryPath.isLocal()
                                    && expandedEntryPath.path().isEmpty();

    // Consider the entered path a directory if it ends with slash/backslash.
    // If it is a dir but doesn't end with a backslash, we want to still show all (other) matching
    // items from the same parent directory.
    // Unfortunately fromUserInput removes slash/backslash at the end, so manually check the original.
    const bool isDir = expandedEntry.isEmpty() || expandedEntry.endsWith('/')
                       || expandedEntry.endsWith('\\');
    const FilePath directory = isDir ? absoluteEntryPath : absoluteEntryPath.parentDir();
    const QString entryFileName = isDir ? QString() : absoluteEntryPath.fileName();

    DirFilterFlags fileFilter = DirFilterFlag::Dirs | DirFilterFlag::Drives
                               | DirFilterFlag::NoDotAndDotDot | DirFilterFlag::Files;
    if (includeHidden)
        fileFilter |= DirFilterFlag::Hidden;
    // use only 'name' for case sensitivity decision, because we need to make the path
    // match the case on the file system for case-sensitive file systems
    const Qt::CaseSensitivity caseSensitivity = ILocatorFilter::caseSensitivity(entryFileName);
    const auto [dirs, files] = isPartOfDeviceRoot ? std::pair(FilePaths(), FilePaths())
                                                  : dirEntries(directory, {{}, fileFilter});

    // directories
    QRegularExpression regExp = ILocatorFilter::createRegExp(entryFileName, caseSensitivity);
    if (regExp.isValid()) {
        for (const FilePath &dir : dirs) {
            if (promise.isCanceled())
                return;

            const QString dirString = dir.relativeChildPath(directory).nativePath();
            const QRegularExpressionMatch match = regExp.match(dirString);
            if (match.hasMatch()) {
                const ILocatorFilter::MatchLevel level = matchLevelFor(match, dirString);
                LocatorFilterEntry filterEntry;
                filterEntry.displayName = dirString;
                filterEntry.acceptor = [shortcutString, dir] {
                    const QString value
                        = shortcutString + ' '
                          + dir.absoluteFilePath().cleanPath().pathAppended("/").toUserOutput();
                    return AcceptResult{value, int(value.size())};
                };
                filterEntry.completer = filterEntry.acceptor;
                filterEntry.filePath = dir;
                filterEntry.highlightInfo = ILocatorFilter::highlightInfo(match);

                entries[int(level)].append(filterEntry);
            }
        }
    }
    // file names can match with +linenumber or :linenumber
    const Link link = Link::fromString(entryFileName, true);
    regExp = ILocatorFilter::createRegExp(link.targetFilePath.toUrlishString(), caseSensitivity);
    if (regExp.isValid()) {
        for (const FilePath &file : files) {
            if (promise.isCanceled())
                return;

            const QString fileString = file.relativeChildPath(directory).nativePath();
            const QRegularExpressionMatch match = regExp.match(fileString);
            if (match.hasMatch()) {
                const ILocatorFilter::MatchLevel level = matchLevelFor(match, fileString);
                LocatorFilterEntry filterEntry;
                filterEntry.displayName = fileString;
                filterEntry.filePath = file;
                filterEntry.highlightInfo = ILocatorFilter::highlightInfo(match);
                filterEntry.linkForEditor = Link(filterEntry.filePath,
                                                 link.target.line,
                                                 link.target.column);
                filterEntry.acceptor = [file, line = link.target.line, column = link.target.column] {
                    QMetaObject::invokeMethod(
                        EditorManager::instance(),
                        [file, line, column] {
                            if (line > 0)
                                EditorManager::openEditorAt(Link(file, line, column));
                            else
                                ICore::openFiles({file}, ICore::SwitchMode, {},
                                                 /*openProjects=*/false);
                        },
                        Qt::QueuedConnection);
                    return AcceptResult();
                };
                filterEntry.completer = [shortcutString, file] {
                    const QString value = shortcutString + ' '
                                          + file.absoluteFilePath().cleanPath().toUserOutput();
                    return AcceptResult{value, int(value.size())};
                };
                entries[int(level)].append(filterEntry);
            }
        }
    }
    // device roots
    // check against full search text
    regExp = ILocatorFilter::createRegExp(expandedEntryPath.toUserOutput(), caseSensitivity);
    if (regExp.isValid()) {
        const FilePaths roots = deviceRoots();
        for (const FilePath &root : roots) {
            if (promise.isCanceled())
                return;

            const QString displayString = root.toUserOutput();
            const QRegularExpressionMatch match = regExp.match(displayString);
            if (match.hasMatch()) {
                LocatorFilterEntry filterEntry;
                filterEntry.displayName = displayString;
                filterEntry.acceptor = [shortcutString, root] {
                    const QString value
                        = shortcutString + ' '
                          + root.absoluteFilePath().cleanPath().pathAppended("/").toUserOutput();
                    return AcceptResult{value, int(value.size())};
                };
                filterEntry.completer = filterEntry.acceptor;
                filterEntry.filePath = root;
                filterEntry.displayIcon = *sDeviceRootIcon;
                filterEntry.highlightInfo = ILocatorFilter::highlightInfo(match);

                entries[int(ILocatorFilter::MatchLevel::Normal)].append(filterEntry);
            }
        }
    }

    // "create and open" functionality
    const FilePath fullFilePath = directory / entryFileName;
    const bool containsWildcard = expandedEntry.contains('?') || expandedEntry.contains('*');
    if (!containsWildcard && !fullFilePath.exists() && directory.exists()) {
        // create and open file
        {
            LocatorFilterEntry filterEntry;
            filterEntry.displayName = Tr::tr("Create and Open File \"%1\"").arg(expandedEntry);
            filterEntry.displayIcon = Utils::FileIconProvider::icon(QFileIconProvider::File);
            filterEntry.acceptor = [fullFilePath] {
                QMetaObject::invokeMethod(
                    EditorManager::instance(),
                    [fullFilePath] { createAndOpenFile(fullFilePath); },
                    Qt::QueuedConnection);
                return AcceptResult();
            };
            filterEntry.completer = [] { return AcceptResult(); };
            filterEntry.filePath = fullFilePath;
            filterEntry.extraInfo = directory.absoluteFilePath().shortNativePath();
            entries[int(ILocatorFilter::MatchLevel::Normal)].append(filterEntry);
        }

        // create directory
        {
            LocatorFilterEntry filterEntry;
            filterEntry.displayName = Tr::tr("Create Directory \"%1\"").arg(expandedEntry);
            filterEntry.displayIcon = Utils::FileIconProvider::icon(QFileIconProvider::Folder);
            filterEntry.acceptor = [fullFilePath, shortcutString] {
                QMetaObject::invokeMethod(
                    EditorManager::instance(),
                    [fullFilePath, shortcutString] {
                        if (createDirectory(fullFilePath)) {
                            const QString value = shortcutString + ' '
                                                  + fullFilePath.absoluteFilePath()
                                                        .cleanPath()
                                                        .pathAppended("/")
                                                        .toUserOutput();
                            LocatorManager::show(value, value.size());
                        }
                    },
                    Qt::QueuedConnection);
                return AcceptResult();
            };
            filterEntry.completer = [] { return AcceptResult(); };
            filterEntry.filePath = fullFilePath;
            filterEntry.extraInfo = directory.absoluteFilePath().shortNativePath();
            entries[int(ILocatorFilter::MatchLevel::Normal)].append(filterEntry);
        }
    }

    storage.reportOutput(
        std::accumulate(std::begin(entries), std::end(entries), LocatorFilterEntries()));
}

LocatorMatcherTasks FileSystemFilter::matchers()
{
    const auto onSetup = [includeHidden = m_includeHidden, shortcut = shortcutString()]
        (Async<void> &async) {
        async.setConcurrentCallData(matches, *LocatorStorage::storage(), shortcut,
                                    DocumentManager::fileDialogInitialDirectory(), includeHidden);
    };

    return {AsyncTask<void>(onSetup)};
}

class FileSystemFilterOptions final : public LocatorFilterOptions
{
public:
    FileSystemFilterOptions(FileSystemFilter *filter, bool includeHiddenValue)
        : LocatorFilterOptions(filter)
    {
        setQmlSource(QUrl("qrc:/qt/qml/QtCreator/Core/locator/FileSystemFilterDialog.qml"));

        includeHidden.setQmlName("IncludeHidden");
        includeHidden.setLabelText(Tr::tr("Include hidden files"));
        includeHidden.setValue(includeHiddenValue);
    }

    BoolAspect includeHidden{this};
};

bool FileSystemFilter::openConfigDialog(QWidget *parent, bool &needsRefresh)
{
    Q_UNUSED(needsRefresh)
    FileSystemFilterOptions options(this, m_includeHidden);
    if (!ILocatorFilter::openConfigDialog(parent, &options))
        return false;

    m_includeHidden = options.includeHidden();
    return true;
}

const char kIncludeHiddenKey[] = "includeHidden";

void FileSystemFilter::saveState(QJsonObject &object) const
{
    if (m_includeHidden != s_includeHiddenDefault)
        object.insert(kIncludeHiddenKey, m_includeHidden);
}

void FileSystemFilter::restoreState(const QJsonObject &object)
{
    m_includeHidden = object.value(kIncludeHiddenKey).toBool(s_includeHiddenDefault);
}

#ifdef WITH_TESTS

class FileSystemFilterTest final : public QObject
{
    Q_OBJECT

private slots:
    void testTheDialogDrawsWithTheQmlItNames()
    {
        FileSystemFilter filter;
        FileSystemFilterOptions options(&filter, true);
        const Result<> rendered
            = Core::aspectFormRenders(&options, "locator/FileSystemFilterDialog.qml");
        QVERIFY2(rendered, qPrintable(rendered ? QString() : rendered.error()));
    }

    void testTheDialogOpensOnWhatTheFilterHolds()
    {
        FileSystemFilter filter;
        filter.setShortcutString("f");

        const FileSystemFilterOptions on(&filter, true);
        QCOMPARE(on.includeHidden(), true);
        const FileSystemFilterOptions off(&filter, false);
        QCOMPARE(off.includeHidden(), false);

        // The prefix row is filled from the same filter, which is what
        // deriving from the shared options buys.
        QCOMPARE(on.shortcut(), QString("f"));
    }

    void testHiddenFilesAreOfferedByDefault()
    {
        // Restoring a state that says nothing has to leave them on: this
        // filter is how a path is typed, and a dot directory is a path.
        FileSystemFilter filter;
        filter.restoreState(QJsonObject());
        QVERIFY2(filter.m_includeHidden, "browsing stopped offering hidden files");
    }
};

QObject *createFileSystemFilterTest()
{
    return new FileSystemFilterTest;
}

#endif // WITH_TESTS

} // namespace Core::Internal

#include "filesystemfilter.moc"
