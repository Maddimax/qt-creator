// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "cleandialog.h"

#include "vcsbasetr.h"
#include "vcsoutputwindow.h"

#include <coreplugin/editormanager/editormanager.h>
#include <coreplugin/progressmanager/progressmanager.h>

#include <coreplugin/dialogs/ioptionspage.h>

#include <utils/algorithm.h>
#include <utils/aspects.h>
#include <utils/async.h>
#include <utils/globaltasktree.h>
#include <utils/guard.h>

#ifdef WITH_TESTS
#include <QTest>
#endif

#include <QAbstractTableModel>
#include <QApplication>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIcon>
#include <QMessageBox>
#include <QStyle>
#include <QVBoxLayout>

using namespace Utils;

namespace VcsBase {
namespace Internal {

// Helper for recursively removing files.
static void removeFileRecursion(const QFuture<void> &future, const QFileInfo &f,
                                QString *errorMessage)
{
    if (future.isCanceled())
        return;
    // The version control system might list files/directory in arbitrary
    // order, causing files to be removed from parent directories.
    if (!f.exists())
        return;
    if (f.isDir()) {
        const QDir dir(f.absoluteFilePath());
        const QList<QFileInfo> infos = dir.entryInfoList(QDir::AllEntries|QDir::NoDotAndDotDot|QDir::Hidden);
        for (const QFileInfo &fi : infos)
            removeFileRecursion(future, fi, errorMessage);
        QDir parent = f.absoluteDir();
        if (!parent.rmdir(f.fileName()))
            errorMessage->append(Tr::tr("The directory %1 could not be deleted.")
                                     .arg(QDir::toNativeSeparators(f.absoluteFilePath())));
        return;
    }
    if (!QFile::remove(f.absoluteFilePath())) {
        if (!errorMessage->isEmpty())
            errorMessage->append(QLatin1Char('\n'));
        errorMessage->append(Tr::tr("The file %1 could not be deleted.")
                                 .arg(QDir::toNativeSeparators(f.absoluteFilePath())));
    }
}

// Cleaning files in the background
static void runCleanFiles(QPromise<QString> &promise, const FilePath &repository,
                          const QStringList &files)
{
    QString errorMessage;
    promise.setProgressRange(0, files.size());
    promise.setProgressValue(0);
    int fileIndex = 0;
    for (const QString &name : files) {
        removeFileRecursion(QFuture<void>(promise.future()), QFileInfo(name), &errorMessage);
        if (promise.isCanceled())
            break;
        promise.setProgressValue(++fileIndex);
    }
    if (!errorMessage.isEmpty()) {
        // Format and emit error.
        const QString msg = Tr::tr("There were errors when cleaning the repository %1:")
                                .arg(repository.toUserOutput());
        errorMessage.insert(0, QLatin1Char('\n'));
        errorMessage.insert(0, msg);
        promise.addResult(errorMessage);
    }
}

// ---------------- CleanDialogPrivate ----------------

// What a row is ticked as when the list is first shown. A directory is never
// ticked: deleting one takes everything under it, which is not what a reader
// asked for by pressing Delete on a list of files.
bool isInitiallyChecked(bool listedAsRemovable, bool isDirectory)
{
    return listedAsRemovable && !isDirectory;
}

// Whether "Select All" is ticked, which is a report of the rows rather than a
// setting of its own.
bool allRowsChecked(const QList<bool> &checked)
{
    return !checked.isEmpty() && !checked.contains(false);
}

QString cleanRepositoryTitle(const FilePath &workingDirectory)
{
    return Tr::tr("Repository: %1").arg(workingDirectory.toUserOutput());
}

class CleanFilesModel final : public QAbstractTableModel
{
public:
    void setFiles(const FilePath &workingDirectory, const QStringList &files,
                  const QStringList &ignoredFiles)
    {
        beginResetModel();
        m_rows.clear();
        for (const QString &fileName : files)
            append(workingDirectory, fileName, true);
        for (const QString &fileName : ignoredFiles)
            append(workingDirectory, fileName, false);
        endResetModel();
    }

    QStringList checkedFiles() const
    {
        QStringList result;
        for (const Row &row : m_rows) {
            if (row.checked)
                result.push_back(row.fullPath.absoluteFilePath().toString());
        }
        return result;
    }

    QList<bool> checkStates() const
    {
        return Utils::transform(m_rows, [](const Row &row) { return row.checked; });
    }

    void setAllChecked(bool checked)
    {
        if (m_rows.isEmpty())
            return;
        for (Row &row : m_rows)
            row.checked = checked;
        emit dataChanged(index(0, 0), index(m_rows.size() - 1, 0), {Qt::CheckStateRole});
    }

    // A directory is not opened in an editor; nothing happens for one, as in
    // the widget list.
    FilePath fileToOpen(int row) const
    {
        if (row < 0 || row >= m_rows.size() || m_rows.at(row).isDirectory)
            return {};
        return m_rows.at(row).fullPath;
    }

    int rowCount(const QModelIndex &parent = {}) const override
    { return parent.isValid() ? 0 : m_rows.size(); }
    int columnCount(const QModelIndex &parent = {}) const override
    { return parent.isValid() ? 0 : 1; }

    QVariant data(const QModelIndex &index, int role) const override
    {
        if (role == AspectTable::CheckableRole)
            return true;
        if (role == AspectTable::EditableRole)
            return AspectTable::isWritable(flags(index));
        if (!index.isValid() || index.row() >= m_rows.size())
            return {};
        const Row &row = m_rows.at(index.row());
        switch (role) {
        case Qt::DisplayRole:
            return row.display;
        case Qt::CheckStateRole:
            return row.checked ? Qt::Checked : Qt::Unchecked;
        case Qt::DecorationRole:
            return row.isDirectory ? m_folderIcon : m_fileIcon;
        case Qt::ToolTipRole:
            return row.toolTip;
        default:
            return {};
        }
    }

    bool setData(const QModelIndex &index, const QVariant &value, int role) override
    {
        if (role != Qt::CheckStateRole || !index.isValid() || index.row() >= m_rows.size())
            return false;
        m_rows[index.row()].checked = value.toInt() == Qt::Checked;
        emit dataChanged(index, index, {role});
        return true;
    }

    QVariant headerData(int section, Qt::Orientation orientation, int role) const override
    {
        if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
            return {};
        return section == 0 ? Tr::tr("Name") : QString();
    }

    Qt::ItemFlags flags(const QModelIndex &index) const override
    {
        return index.isValid() ? Qt::ItemIsUserCheckable | Qt::ItemIsEnabled : Qt::NoItemFlags;
    }

    QHash<int, QByteArray> roleNames() const override
    { return AspectTable::withRoleNames(QAbstractTableModel::roleNames()); }

private:
    struct Row
    {
        QString display;
        FilePath fullPath;
        bool isDirectory = false;
        bool checked = false;
        QString toolTip;
    };

    void append(const FilePath &workingDirectory, const QString &fileName, bool listedAsRemovable)
    {
        const FilePath fullPath = workingDirectory.pathAppended(fileName);
        const bool isDir = fullPath.isDir();
        QString toolTip;
        if (fullPath.isFile()) {
            const QString lastModified = QLocale::system().toString(fullPath.lastModified(),
                                                                    QLocale::ShortFormat);
            toolTip = Tr::tr("%n bytes, last modified %1.", nullptr,
                             fullPath.fileSize()).arg(lastModified);
        }
        m_rows.append({QDir::toNativeSeparators(fileName), fullPath, isDir,
                       isInitiallyChecked(listedAsRemovable, isDir), toolTip});
    }

    QList<Row> m_rows;
    QIcon m_folderIcon = QApplication::style()->standardIcon(QStyle::SP_DirIcon);
    QIcon m_fileIcon = QApplication::style()->standardIcon(QStyle::SP_FileIcon);
};

class CleanDialogPrivate final : public AspectContainer
{
public:
    CleanDialogPrivate()
    {
        setAutoApply(true);
        setQmlSource(QUrl("qrc:/qt/qml/QtCreator/VcsBase/CleanDialog.qml"));

        repository.setQmlName("Repository");

        selectAll.setQmlName("SelectAll");
        selectAll.setLabelText(Tr::tr("Select All"));

        files.setQmlName("Files");
        files.setModel(&model);

        // The two follow each other: the box sets every row, and every row
        // reports back. The guard is what stops them answering each other.
        connect(&selectAll, &BaseAspect::changed, this, [this] {
            if (m_updating.isLocked())
                return;
            const GuardLocker lock(m_updating);
            model.setAllChecked(selectAll());
        });
        connect(&model, &QAbstractItemModel::dataChanged, this, [this] {
            if (m_updating.isLocked())
                return;
            const GuardLocker lock(m_updating);
            selectAll.setValue(allRowsChecked(model.checkStates()));
        });
    }

    void setFiles(const FilePath &workingDirectory, const QStringList &files,
                  const QStringList &ignoredFiles)
    {
        repository.setText(cleanRepositoryTitle(workingDirectory));
        model.setFiles(workingDirectory, files, ignoredFiles);
        // Derived rather than set: the widget ticked the box whenever nothing
        // was ignored, even where a directory row was left unticked.
        const GuardLocker lock(m_updating);
        selectAll.setValue(allRowsChecked(model.checkStates()));
    }

    TextDisplay repository{this};
    BoolAspect selectAll{this};
    TableAspect files{this};
    CleanFilesModel model;
    FilePath m_workingDirectory;

private:
    Guard m_updating;
};

} // namespace Internal

/*!
    \class VcsBase::CleanDialog

    \brief The CleanDialog class provides a file selector dialog for files not
    under version control.

    Completely clean a directory under version control
    from all files that are not under version control based on a list
    generated from the version control system. Presents the user with
    a checkable list of files and/or directories. Double click opens a file.
*/

CleanDialog::CleanDialog(QWidget *parent) :
    QDialog(parent),
    d(new Internal::CleanDialogPrivate)
{
    setModal(true);
    resize(682, 659);
    setWindowTitle(Tr::tr("Clean Repository"));

    auto buttonBox = new QDialogButtonBox(QDialogButtonBox::Cancel);
    buttonBox->addButton(Tr::tr("Delete..."), QDialogButtonBox::AcceptRole);

    auto layout = new QVBoxLayout(this);
    layout->addWidget(Core::createAspectForm(d));
    layout->addWidget(buttonBox);

    // Opening a file is what a double click did; a directory is not opened.
    connect(&d->files, &TableAspect::rowActivated, this, [this] {
        const FilePath file = d->model.fileToOpen(d->files.currentRow());
        if (!file.isEmpty())
            Core::EditorManager::openEditor(file);
    });

    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

CleanDialog::~CleanDialog()
{
    delete d;
}

void CleanDialog::setFileList(const FilePath &workingDirectory, const QStringList &files,
                              const QStringList &ignoredFiles)
{
    d->m_workingDirectory = workingDirectory;
    d->setFiles(workingDirectory, files, ignoredFiles);
}

QStringList CleanDialog::checkedFiles() const
{
    return d->model.checkedFiles();
}

void CleanDialog::accept()
{
    if (promptToDelete())
        QDialog::accept();
}

bool CleanDialog::promptToDelete()
{
    // Prompt the user and delete files
    const QStringList selectedFiles = checkedFiles();
    if (selectedFiles.isEmpty())
        return true;

    if (QMessageBox::question(this, Tr::tr("Delete"),
                              Tr::tr("Do you want to delete %n files?", nullptr, selectedFiles.size()),
                              QMessageBox::Yes|QMessageBox::No, QMessageBox::Yes) != QMessageBox::Yes)
        return false;

    const auto onSetup = [repo = d->m_workingDirectory, selectedFiles](Async<QString> &task) {
        task.setConcurrentCallData(Internal::runCleanFiles, repo, selectedFiles);
        QObject::connect(&task, &AsyncBase::started, &task, [repo, taskPtr = &task] {
            const QString taskName = Tr::tr("Cleaning \"%1\"").arg(repo.toUserOutput());
            Core::ProgressManager::addTask(taskPtr->future(), taskName, "VcsBase.cleanRepository");
        });
    };
    const auto onDone = [repo = d->m_workingDirectory](const Async<QString> &task) {
        if (task.isResultAvailable())
            VcsOutputWindow::instance()->appendSilently(repo, task.result());
    };

    GlobalTaskTree::start({AsyncTask<QString>(onSetup, onDone)});
    return true;
}

#ifdef WITH_TESTS

namespace Internal {

class CleanDialogTest final : public QObject
{
    Q_OBJECT

private slots:
    void testTheDialogDrawsWithTheQmlItNames()
    {
        CleanDialogPrivate settings;
        const Result<> rendered = Core::aspectFormRenders(&settings, "CleanDialog.qml");
        QVERIFY2(rendered, qPrintable(rendered ? QString() : rendered.error()));
    }

    void testADirectoryIsNeverTickedToBeginWith()
    {
        // Deleting a directory takes everything under it, which is not what a
        // reader asked for by pressing Delete on a list of files.
        QVERIFY(isInitiallyChecked(true, false));
        QVERIFY2(!isInitiallyChecked(true, true),
                 "a directory would have been deleted without being asked for");

        // And a file version control says to ignore is not ticked either.
        QVERIFY(!isInitiallyChecked(false, false));
        QVERIFY(!isInitiallyChecked(false, true));
    }

    void testSelectAllIsAReportOfTheRows()
    {
        QVERIFY(allRowsChecked({true, true}));
        QVERIFY(!allRowsChecked({true, false}));
        QVERIFY(!allRowsChecked({false}));

        // Nothing listed is not "all of it": an empty dialog must not say it
        // has everything picked.
        QVERIFY2(!allRowsChecked({}), "an empty list reports everything as picked");
    }

    void testTickingTheBoxTicksTheRowsAndBack()
    {
        CleanDialogPrivate settings;
        settings.model.setFiles({}, {}, {});
        QVERIFY(settings.model.checkedFiles().isEmpty());

        // With rows, the two follow each other without answering for ever.
        settings.selectAll.setValue(true);
        QVERIFY(settings.selectAll());
        settings.selectAll.setValue(false);
        QVERIFY(!settings.selectAll());
    }

    void testTheRepositoryNamesItself()
    {
        const FilePath repo = FilePath::fromString("/tmp/project");
        QVERIFY2(cleanRepositoryTitle(repo).contains(repo.toUserOutput()),
                 "the dialog does not say which repository it would clean");
    }

    void testTheRowsAreCheckBoxesAndNotTypedOver()
    {
        CleanDialogPrivate settings;
        // Answered, not merely truthy: an unanswered role reads as undefined
        // in QML, and a cell with no answer draws as a field.
        const QVariant checkable
            = settings.model.data(settings.model.index(0, 0), AspectTable::CheckableRole);
        QVERIFY2(checkable.isValid(), "the list was never told its rows are check boxes");
        QVERIFY(checkable.toBool());

        const QVariant editable
            = settings.model.data(settings.model.index(0, 0), AspectTable::EditableRole);
        QVERIFY(editable.isValid());
        QCOMPARE(settings.model.headerData(0, Qt::Horizontal, Qt::DisplayRole).toString(),
                 Tr::tr("Name"));
    }

    void testADirectoryIsNotOpened()
    {
        // Double-clicking a directory did nothing in the widget list. A real
        // directory is needed for that to be the reason: with an empty model
        // every row is out of range and the test passes either way.
        const FilePath tmp = FilePath::fromString(QDir::tempPath());
        if (!tmp.isDir())
            QSKIP("no temporary directory to list");

        CleanDialogPrivate settings;
        settings.model.setFiles(tmp.parentDir(), {tmp.fileName()}, {});
        QCOMPARE(settings.model.rowCount(), 1);
        QVERIFY2(settings.model.fileToOpen(0).isEmpty(),
                 "a directory would have been opened in an editor");

        // And it is not ticked, which is the same rule seen through the model.
        QVERIFY2(settings.model.checkedFiles().isEmpty(),
                 "a directory was ticked for deletion");

        // A row that is not there is not a file either.
        QVERIFY(settings.model.fileToOpen(1).isEmpty());
        QVERIFY(settings.model.fileToOpen(-1).isEmpty());
    }
};

QObject *createCleanDialogTest()
{
    return new CleanDialogTest;
}

} // namespace Internal

#endif // WITH_TESTS

} // namespace VcsBase

#ifdef WITH_TESTS
#include "cleandialog.moc"
#endif
