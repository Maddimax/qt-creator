// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "selectablefilesmodel.h"

#include "projectexplorerconstants.h"
#include "projectexplorertr.h"

#include <coreplugin/dialogs/ioptionspage.h>
#include <coreplugin/icore.h>

#include <utils/algorithm.h>
#include <utils/aspects.h>
#include <utils/async.h>
#include <utils/fancylineedit.h>
#include <utils/fsengine/fileiconprovider.h>
#include <utils/pathchooser.h>
#include <utils/stringutils.h>

#include <QDialogButtonBox>
#include <QDir>
#include <QVBoxLayout>

#ifdef WITH_TESTS
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#endif
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSortFilterProxyModel>
#include <QTreeView>

#include <memory>

#ifdef WITH_TESTS
#include <QTest>
#endif

using namespace QtTaskTree;
using namespace Utils;

namespace ProjectExplorer {

const char HIDE_FILE_FILTER_DEFAULT[] = "Makefile*; *.o; *.lo; *.la; *.obj; *~; *.files;"
                                        " *.config; *.creator; *.user*; *.includes; *.autosave";
const char SELECT_FILE_FILTER_DEFAULT[] = "*.c; *.cc; *.cpp; *.cp; *.cxx; *.c++; *.h; *.hh; *.hpp; *.hxx;";

using ResultType = std::shared_ptr<Tree>;

SelectableFilesModel::SelectableFilesModel(QObject *parent) : QAbstractItemModel(parent)
{
    m_root.reset(new Tree);
}

void SelectableFilesModel::setInitialMarkedFiles(const FilePaths &files)
{
    m_files = Utils::toSet(files);
}

static void buildTree(QPromise<ResultType> &promise, const FilePath &baseDir,
                      const SelectableFilesModel::FilterData &filterData, Tree *tree,
                      int symlinkDepth, int &futureCount)
{
    if (symlinkDepth == 0)
        return;

    const QFileInfoList fileInfoList = QDir(baseDir.toUrlishString())
                       .entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
    bool allChecked = true;
    bool allUnchecked = true;
    for (const QFileInfo &fileInfo : fileInfoList) {
        const FilePath fn = FilePath::fromFileInfo(fileInfo);
        if ((futureCount % 100) == 0) {
            promise.setProgressRange(0, futureCount);
            promise.setProgressValueAndText(futureCount, fn.toUserOutput());
            if (promise.isCanceled())
                return;
        }
        ++futureCount;
        if (fileInfo.isDir()) {
            if (fileInfo.isSymLink()) {
                const FilePath target = FilePath::fromString(fileInfo.symLinkTarget());
                if (target == baseDir || baseDir.isChildOf(target))
                    continue;
            }
            auto t = new Tree;
            t->parent = tree;
            t->name = fileInfo.fileName();
            t->fullPath = fn;
            t->isDir = true;
            buildTree(promise, fn, filterData, t, symlinkDepth - fileInfo.isSymLink(), futureCount);
            allChecked &= t->checked == Qt::Checked;
            allUnchecked &= t->checked == Qt::Unchecked;
            tree->childDirectories.append(t);
        } else {
            auto t = new Tree;
            t->parent = tree;
            t->name = fileInfo.fileName();
            const SelectableFilesModel::FilterState state = SelectableFilesModel::filter(filterData, t);
            t->checked = ((filterData.files.isEmpty() && state == SelectableFilesModel::FilterState::CHECKED)
                          || filterData.files.contains(fn)) ? Qt::Checked : Qt::Unchecked;
            t->fullPath = fn;
            t->isDir = false;
            allChecked &= t->checked == Qt::Checked;
            allUnchecked &= t->checked == Qt::Unchecked;
            tree->files.append(t);
            if (state != SelectableFilesModel::FilterState::HIDDEN)
                tree->visibleFiles.append(t);
        }
    }
    if (tree->childDirectories.isEmpty() && tree->visibleFiles.isEmpty())
        tree->checked = Qt::Unchecked;
    else if (allChecked)
        tree->checked = Qt::Checked;
    else if (allUnchecked)
        tree->checked = Qt::Unchecked;
    else
        tree->checked = Qt::PartiallyChecked;
}

static void buildTreeRoot(QPromise<ResultType> &promise, const FilePath &baseDir,
                          const SelectableFilesModel::FilterData &filterData)
{
    int futureCount = 0;
    const ResultType root(new Tree);
    root->name = baseDir.toUserOutput();
    root->fullPath = baseDir;
    root->isDir = true;
    buildTree(promise, baseDir, filterData, root.get(), 5, futureCount);
    promise.addResult(root);
}

void SelectableFilesFromDirModel::startParsing(const FilePath &baseDir)
{
    const auto onSetup = [this, baseDir, filterData = filterData()](Async<ResultType> &task) {
        task.setConcurrentCallData(buildTreeRoot, baseDir, filterData);
        connect(&task, &AsyncBase::progressTextChanged,
                this, &SelectableFilesFromDirModel::parsingProgress);
    };
    const auto onDone = [this, baseDir](const Async<ResultType> &task) {
        beginResetModel();
        m_root = task.result();
        m_outOfBaseDirFiles = Utils::filtered(m_files, [baseDir](const FilePath &fn) {
            return !fn.isChildOf(baseDir);
        });
        endResetModel();
        emit parsingFinished();
    };
    m_taskTreeRunner.start({AsyncTask<ResultType>(onSetup, onDone, CallDoneFlag::OnSuccess)});
}

void SelectableFilesFromDirModel::cancel()
{
    m_taskTreeRunner.reset();
}

SelectableFilesModel::FilterState SelectableFilesModel::filter(const FilterData &filterData, Tree *t)
{
    if (t->isDir)
        return FilterState::SHOWN;
    if (filterData.files.contains(t->fullPath))
        return FilterState::CHECKED;

    const auto matchesTreeName = [name = t->name](const Glob &g) { return g.isMatch(name); };

    if (Utils::anyOf(filterData.selectFilesFilter, matchesTreeName))
        return FilterState::CHECKED;

    return Utils::anyOf(filterData.hideFilesFilter, matchesTreeName) ? FilterState::HIDDEN : FilterState::SHOWN;
}

SelectableFilesModel::FilterState SelectableFilesModel::filter(Tree *t) const
{
    return filter(filterData(), t);
}

int SelectableFilesModel::columnCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent)
    return 1;
}

int SelectableFilesModel::rowCount(const QModelIndex &parent) const
{
    if (!parent.isValid())
        return 1;
    auto parentT = static_cast<Tree *>(parent.internalPointer());
    return parentT->childDirectories.size() + parentT->visibleFiles.size();
}

QModelIndex SelectableFilesModel::index(int row, int column, const QModelIndex &parent) const
{
    if (!parent.isValid())
        return createIndex(row, column, m_root.get());
    auto parentT = static_cast<Tree *>(parent.internalPointer());
    if (row < parentT->childDirectories.size())
        return createIndex(row, column, parentT->childDirectories.at(row));
    else
        return createIndex(row, column, parentT->visibleFiles.at(row - parentT->childDirectories.size()));
}

QModelIndex SelectableFilesModel::parent(const QModelIndex &child) const
{
    if (!child.isValid())
        return {};
    if (!child.internalPointer())
        return {};
    auto parent = static_cast<Tree *>(child.internalPointer())->parent;
    if (!parent)
        return {};
    if (!parent->parent) //then the parent is the root
        return createIndex(0, 0, parent);
    // figure out where the parent is
    int pos = parent->parent->childDirectories.indexOf(parent);
    if (pos == -1)
        pos = parent->parent->childDirectories.size() + parent->parent->visibleFiles.indexOf(parent);
    return createIndex(pos, 0, parent);
}

QVariant SelectableFilesModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid())
        return {};
    auto t = static_cast<Tree *>(index.internalPointer());
    if (role == Qt::DisplayRole)
        return t->name;
    if (role == Qt::CheckStateRole)
        return t->checked;
    if (role == Qt::DecorationRole) {
        if (t->icon.isNull())
            t->icon = FileIconProvider::icon(t->fullPath);
        return t->icon;
    }
    // A widget view reads flags() for these; a Qt Quick one cannot, so the
    // model answers both. Every row is a check box and every one may be
    // ticked, which is what flags() says.
    if (role == AspectTable::CheckableRole)
        return true;
    if (role == AspectTable::EditableRole)
        return AspectTable::isWritable(flags(index));
    return {};
}

// The one column has no name - the widget view hid its header - and a Qt Quick
// view draws a heading for a column that answers one.
QVariant SelectableFilesModel::headerData(int, Qt::Orientation, int) const
{
    return {};
}

QHash<int, QByteArray> SelectableFilesModel::roleNames() const
{
    return AspectTable::withRoleNames(QAbstractItemModel::roleNames());
}

bool SelectableFilesModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (role == Qt::CheckStateRole) {
        // We can do that!
        auto t = static_cast<Tree *>(index.internalPointer());
        t->checked = Qt::CheckState(value.toInt());
        propagateDown(index);
        propagateUp(index);
        emit dataChanged(index, index);
    }
    return false;
}

void SelectableFilesModel::propagateUp(const QModelIndex &index)
{
    QModelIndex parent = index.parent();
    if (!parent.isValid())
        return;
    auto parentT = static_cast<Tree *>(parent.internalPointer());
    if (!parentT)
        return;
    bool allChecked = true;
    bool allUnchecked = true;
    for (const Tree *tree : std::as_const(parentT->childDirectories)) {
        allChecked &= tree->checked == Qt::Checked;
        allUnchecked &= tree->checked == Qt::Unchecked;
    }
    for (const Tree *tree : std::as_const(parentT->visibleFiles)) {
        allChecked &= tree->checked == Qt::Checked;
        allUnchecked &= tree->checked == Qt::Unchecked;
    }
    Qt::CheckState newState = Qt::PartiallyChecked;
    if (parentT->childDirectories.isEmpty() && parentT->visibleFiles.isEmpty())
        newState = Qt::Unchecked;
    else if (allChecked)
        newState = Qt::Checked;
    else if (allUnchecked)
        newState = Qt::Unchecked;
    if (parentT->checked != newState) {
        parentT->checked = newState;
        emit dataChanged(parent, parent);
        propagateUp(parent);
    }
}

void SelectableFilesModel::propagateDown(const QModelIndex &idx)
{
    auto t = static_cast<Tree *>(idx.internalPointer());
    for (int i = 0; i<t->childDirectories.size(); ++i) {
        t->childDirectories[i]->checked = t->checked;
        propagateDown(index(i, 0, idx));
    }
    for (int i = 0; i<t->files.size(); ++i)
        t->files[i]->checked = t->checked;

    int rows = rowCount(idx);
    if (rows)
        emit dataChanged(index(0, 0, idx), index(rows-1, 0, idx));
}

Qt::ItemFlags SelectableFilesModel::flags(const QModelIndex &index) const
{
    Q_UNUSED(index)
    return Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsUserCheckable;
}

FilePaths SelectableFilesModel::selectedPaths() const
{
    FilePaths result;
    collectPaths(m_root.get(), &result);
    return result;
}

void SelectableFilesModel::collectPaths(Tree *root, FilePaths *result) const
{
    if (root->checked == Qt::Unchecked)
        return;
    result->append(root->fullPath);
    for (Tree *t : std::as_const(root->childDirectories))
        collectPaths(t, result);
}

FilePaths SelectableFilesModel::selectedFiles() const
{
    FilePaths result = Utils::toList(m_outOfBaseDirFiles);
    collectFiles(m_root.get(), &result);
    return result;
}

FilePaths SelectableFilesModel::preservedFiles() const
{
    return Utils::toList(m_outOfBaseDirFiles);
}

bool SelectableFilesModel::hasCheckedFiles() const
{
    return m_root->checked != Qt::Unchecked;
}

void SelectableFilesModel::collectFiles(Tree *root, FilePaths *result) const
{
    if (root->checked == Qt::Unchecked)
        return;
    for (Tree *t : std::as_const(root->childDirectories))
        collectFiles(t, result);
    for (Tree *t : std::as_const(root->visibleFiles))
        if (t->checked == Qt::Checked)
            result->append(t->fullPath);
}

QList<Glob> SelectableFilesModel::parseFilter(const QString &filter)
{
    QList<Glob> result;
    const QStringList list = filter.split(QLatin1Char(';'), Qt::SkipEmptyParts);
    for (const QString &e : list) {
        QString entry = e.trimmed();
        Glob g;
        if (entry.indexOf(QLatin1Char('*')) == -1 && entry.indexOf(QLatin1Char('?')) == -1) {
            g.mode = Glob::EXACT;
            g.matchString = entry;
        } else if (entry.startsWith(QLatin1Char('*')) && entry.indexOf(QLatin1Char('*'), 1) == -1
                   && entry.indexOf(QLatin1Char('?'), 1) == -1) {
            g.mode = Glob::ENDSWITH;
            g.matchString = entry.mid(1);
        } else {
            g.mode = Glob::REGEXP;
            const QString re = QRegularExpression::wildcardToRegularExpression(entry);
            g.matchRegexp = QRegularExpression(re, QRegularExpression::CaseInsensitiveOption);
        }
        result.append(g);
    }
    return result;
}

void SelectableFilesModel::applyFilter(const QString &selectFilesfilter, const QString &hideFilesfilter)
{
    QList<Glob> filter = parseFilter(selectFilesfilter);
    bool mustApply = filter != m_selectFilesFilter;
    m_selectFilesFilter = filter;

    filter = parseFilter(hideFilesfilter);
    mustApply = mustApply || (filter != m_hideFilesFilter);
    m_hideFilesFilter = filter;

    if (mustApply)
        applyFilter(createIndex(0, 0, m_root.get()));
}

void SelectableFilesModel::selectAllFiles()
{
    selectAllFiles(m_root.get());
}

void SelectableFilesModel::selectAllFiles(Tree *root)
{
    root->checked = Qt::Checked;

    for (Tree *t : std::as_const(root->childDirectories))
        selectAllFiles(t);

    for (Tree *t : std::as_const(root->visibleFiles))
        t->checked = Qt::Checked;

    emit checkedFilesChanged();
}

Qt::CheckState SelectableFilesModel::applyFilter(const QModelIndex &idx)
{
    bool allChecked = true;
    bool allUnchecked = true;
    auto t = static_cast<Tree *>(idx.internalPointer());

    for (int i=0; i < t->childDirectories.size(); ++i) {
        Qt::CheckState childCheckState = applyFilter(index(i, 0, idx));
        if (childCheckState == Qt::Checked)
            allUnchecked = false;
        else if (childCheckState == Qt::Unchecked)
            allChecked = false;
        else
            allChecked = allUnchecked = false;
    }

    int visibleIndex = 0;
    int visibleEnd = t->visibleFiles.size();
    int startOfBlock = 0;

    bool removeBlock = false;
    // first remove filtered out rows..
    for (;visibleIndex < visibleEnd; ++visibleIndex) {
        if (startOfBlock == visibleIndex) {
            removeBlock = (filter(t->visibleFiles.at(visibleIndex)) == FilterState::HIDDEN);
        } else if (removeBlock != (filter(t->visibleFiles.at(visibleIndex)) == FilterState::HIDDEN)) {
            if (removeBlock) {
                beginRemoveRows(idx, startOfBlock, visibleIndex - 1);
                for (int i=startOfBlock; i < visibleIndex; ++i)
                    t->visibleFiles[i]->checked = Qt::Unchecked;
                t->visibleFiles.erase(t->visibleFiles.begin() + startOfBlock,
                                      t->visibleFiles.begin() + visibleIndex);
                endRemoveRows();
                visibleIndex = startOfBlock; // start again at startOfBlock
                visibleEnd = t->visibleFiles.size();
            }
            removeBlock = (filter(t->visibleFiles.at(visibleIndex)) == FilterState::HIDDEN);
            startOfBlock = visibleIndex;
        }
    }
    if (removeBlock) {
        beginRemoveRows(idx, startOfBlock, visibleEnd - 1);
        for (int i=startOfBlock; i < visibleEnd; ++i)
            t->visibleFiles[i]->checked = Qt::Unchecked;
        t->visibleFiles.erase(t->visibleFiles.begin() + startOfBlock,
                              t->visibleFiles.begin() + visibleEnd);
        endRemoveRows();
    }

    // Figure out which rows should be visible
    QList<Tree *> newRows;
    for (Tree * const tree : std::as_const(t->files)) {
        if (filter(tree) != FilterState::HIDDEN)
            newRows.append(tree);
    }
    // now add them!
    visibleIndex = 0;
    visibleEnd = t->visibleFiles.size();
    int newIndex = 0;
    int newEnd = newRows.size();
    while (true) {
        while (visibleIndex < visibleEnd && newIndex < newEnd &&
               t->visibleFiles.at(visibleIndex) == newRows.at(newIndex)) {
            ++newIndex;
            ++visibleIndex;
        }
        if (visibleIndex >= visibleEnd || newIndex >= newEnd)
            break;
        startOfBlock = newIndex;
        while (newIndex < newEnd &&
               t->visibleFiles.at(visibleIndex) != newRows.at(newIndex)) {
            ++newIndex;
        }
        // end of block = newIndex
        beginInsertRows(idx, visibleIndex, visibleIndex + newIndex - startOfBlock - 1);
        for (int i= newIndex - 1; i >= startOfBlock; --i)
            t->visibleFiles.insert(visibleIndex, newRows.at(i));
        endInsertRows();
        visibleIndex = visibleIndex + newIndex-startOfBlock;
        visibleEnd = visibleEnd + newIndex-startOfBlock;
        if (newIndex >= newEnd)
            break;
    }
    if (newIndex != newEnd) {
        beginInsertRows(idx, visibleIndex, visibleIndex + newEnd - newIndex - 1);
        for (int i = newEnd - 1; i >= newIndex; --i)
            t->visibleFiles.insert(visibleIndex, newRows.at(i));
        endInsertRows();
    }

    for (Tree * const fileNode : std::as_const(t->visibleFiles)) {
        fileNode->checked = filter(fileNode) == FilterState::CHECKED ? Qt::Checked : Qt::Unchecked;
        if (fileNode->checked)
            allUnchecked = false;
        else
            allChecked = false;
    }

    Qt::CheckState newState = Qt::PartiallyChecked;
    if (t->childDirectories.isEmpty() && t->visibleFiles.isEmpty())
        newState = Qt::Unchecked;
    else if (allChecked)
        newState = Qt::Checked;
    else if (allUnchecked)
        newState = Qt::Unchecked;
    if (t->checked != newState) {
        t->checked = newState;
        emit dataChanged(idx, idx);
    }

    return newState;
}

//////////
// SelectableFilesWidget
//////////

enum class SelectableFilesWidgetRows {
    BaseDirectory, SelectFileFilter, HideFileFilter, ApplyButton, View, Progress, PreservedInformation
};

namespace {

// The tree of what was found. The model is the form's, and comes and goes with
// the directory being looked at; the aspect hands out one proxy for its whole
// life and swaps what is behind it.
class SelectableFilesTreeAspect final : public BaseAspect
{
public:
    explicit SelectableFilesTreeAspect(AspectContainer *container)
        : BaseAspect(container)
    {}

    AspectPresentation presentation() const override
    {
        AspectPresentation p = BaseAspect::presentation();
        p.control = AspectControls::Tree;
        return p;
    }

    QAbstractItemModel *tableModel() override { return &m_shown; }

    void setModel(QAbstractItemModel *model) { m_shown.setSourceModel(model); }

    // The rows the form hands out are the proxy's, so an index of the model
    // behind it has to be translated before anyone else sees it.
    QModelIndex fromModel(const QModelIndex &index) const { return m_shown.mapFromSource(index); }

private:
    QSortFilterProxyModel m_shown;
};

} // namespace

// What the form asks, and what it does about the answers. The model does the
// looking; this says when to start, what to keep, and what the reader is told
// while it runs.
class SelectableFilesAspects final : public AspectContainer
{
public:
    explicit SelectableFilesAspects(SelectableFilesWidget *widget);

    void resetModel(const FilePath &path, const FilePaths &files);
    void setAddFileFilter(const QString &filter);
    void setBaseDirEditable(bool edit);
    void enableFilterHistoryCompletion(const Key &keyPrefix);
    void cancelParsing();

    FilePaths selectedFiles() const { return m_model ? m_model->selectedFiles() : FilePaths(); }
    FilePaths selectedPaths() const { return m_model ? m_model->selectedPaths() : FilePaths(); }
    bool hasFilesSelected() const { return m_model && m_model->hasCheckedFiles(); }

    FilePathAspect baseDir{this};
    ActionAspect startParsing{this};
    StringAspect selectFilter{this};
    StringAspect hideFilter{this};
    ActionAspect applyFilters{this};
    SelectableFilesTreeAspect files{this};
    TextDisplay preserved{this};
    TextDisplay progress{this};

private:
    void applyFilter();
    void beginParsing(const FilePath &baseDir);
    void showParsingProgress(const QString &text);
    void parsingFinished();
    void enableAspects(bool enabled);
    // Opens every directory only some of whose files are picked, so that what
    // was picked is what the reader sees first.
    void expandWhereSomethingIsPicked(const QModelIndex &index);

    SelectableFilesWidget *const m_widget;
    SelectableFilesFromDirModel *m_model = nullptr;
    bool m_filteringScheduled = false;
};

SelectableFilesAspects::SelectableFilesAspects(SelectableFilesWidget *widget)
    : m_widget(widget)
{
    setAutoApply(true);
    setQmlSource(QUrl("qrc:/qt/qml/QtCreator/ProjectExplorer/SelectableFilesForm.qml"));

    const QString selectFilterDefault
        = Core::ICore::settings()->value("GenericProject/ShowFileFilter",
                                         QLatin1String(SELECT_FILE_FILTER_DEFAULT)).toString();
    const QString hideFilterDefault
        = Core::ICore::settings()->value("GenericProject/FileFilter",
                                         QLatin1String(HIDE_FILE_FILTER_DEFAULT)).toString();

    baseDir.setQmlName("BaseDir");
    baseDir.setLabelText(Tr::tr("Source directory:"));
    baseDir.setExpectedKind(PathChooserKind::ExistingDirectory);
    baseDir.setHistoryCompleter("PE.AddToProjectDir.History");

    startParsing.setQmlName("StartParsing");
    startParsing.setActionText(Tr::tr("Start Parsing"));
    startParsing.setAction([this] { beginParsing(baseDir()); });

    selectFilter.setQmlName("SelectFilter");
    selectFilter.setLabelText(Tr::tr("Select files matching:"));
    selectFilter.setDisplayStyle(StringAspect::LineEditDisplay);
    selectFilter.setValue(selectFilterDefault);

    hideFilter.setQmlName("HideFilter");
    hideFilter.setLabelText(Tr::tr("Hide files matching:"));
    hideFilter.setDisplayStyle(StringAspect::LineEditDisplay);
    hideFilter.setValue(hideFilterDefault);

    applyFilters.setQmlName("ApplyFilters");
    applyFilters.setActionText(Tr::tr("Apply Filters"));
    applyFilters.setAction([this] { applyFilter(); });

    files.setQmlName("Files");

    preserved.setQmlName("Preserved");
    preserved.setVisible(false);

    progress.setQmlName("Progress");
    progress.setVisible(false);

    // Nothing to parse until the directory is one.
    connect(&baseDir, &BaseAspect::changed, this, [this] {
        startParsing.setEnabled(baseDir().isDir());
    });
    startParsing.setEnabled(false);
}

void SelectableFilesAspects::resetModel(const FilePath &path, const FilePaths &files_)
{
    delete m_model;
    m_model = new SelectableFilesFromDirModel(this);
    m_model->setInitialMarkedFiles(files_);
    connect(m_model, &SelectableFilesFromDirModel::parsingProgress,
            this, &SelectableFilesAspects::showParsingProgress);
    connect(m_model, &SelectableFilesFromDirModel::parsingFinished,
            this, &SelectableFilesAspects::parsingFinished);
    connect(m_model, &SelectableFilesModel::checkedFilesChanged,
            m_widget, &SelectableFilesWidget::selectedFilesChanged);

    files.setModel(m_model);
    baseDir.setValue(path);
    beginParsing(path);
}

void SelectableFilesAspects::setAddFileFilter(const QString &filter)
{
    selectFilter.setValue(filter);
    if (applyFilters.isEnabled())
        applyFilter();
    else
        m_filteringScheduled = true;
}

void SelectableFilesAspects::setBaseDirEditable(bool edit)
{
    baseDir.setVisible(edit);
    startParsing.setVisible(edit);
}

void SelectableFilesAspects::enableFilterHistoryCompletion(const Key &keyPrefix)
{
    selectFilter.setHistoryCompleter(keyPrefix + ".select");
    hideFilter.setHistoryCompleter(keyPrefix + ".hide");
}

void SelectableFilesAspects::cancelParsing()
{
    if (m_model)
        m_model->cancel();
}

void SelectableFilesAspects::applyFilter()
{
    m_filteringScheduled = false;
    if (m_model)
        m_model->applyFilter(selectFilter.value(), hideFilter.value());
}

void SelectableFilesAspects::beginParsing(const FilePath &dir)
{
    if (!m_model)
        return;
    enableAspects(false);
    applyFilter();
    m_model->startParsing(dir);
}

void SelectableFilesAspects::showParsingProgress(const QString &text)
{
    progress.setText(Tr::tr("Generating file list...\n\n%1").arg(text));
}

void SelectableFilesAspects::parsingFinished()
{
    if (!m_model)
        return;

    expandWhereSomethingIsPicked(m_model->index(0, 0, {}));

    const FilePaths preservedFiles = m_model->preservedFiles();
    preserved.setText(Tr::tr("Not showing %n files that are outside of the base directory.\n"
                             "These files are preserved.", nullptr, preservedFiles.count()));

    enableAspects(true);
    if (m_filteringScheduled)
        applyFilter();
}

void SelectableFilesAspects::enableAspects(bool enabled)
{
    hideFilter.setEnabled(enabled);
    selectFilter.setEnabled(enabled);
    applyFilters.setEnabled(enabled);
    files.setEnabled(enabled);
    baseDir.setEnabled(enabled);
    startParsing.setEnabled(enabled && baseDir().isDir());

    progress.setVisible(!enabled);
    preserved.setVisible(m_model && !m_model->preservedFiles().isEmpty());
}

void SelectableFilesAspects::expandWhereSomethingIsPicked(const QModelIndex &index)
{
    if (!index.isValid() || index.data(Qt::CheckStateRole) != Qt::PartiallyChecked)
        return;
    files.expandIndexInControl(files.fromModel(index));
    for (int row = 0, rows = m_model->rowCount(index); row < rows; ++row)
        expandWhereSomethingIsPicked(m_model->index(row, 0, index));
}

SelectableFilesWidget::SelectableFilesWidget(QWidget *parent)
    : QWidget(parent)
    , d(new SelectableFilesAspects(this))
{
    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(Core::createAspectForm(d.get()));
}

SelectableFilesWidget::SelectableFilesWidget(const FilePath &path, const FilePaths &files,
                                             QWidget *parent)
    : SelectableFilesWidget(parent)
{
    resetModel(path, files);
}

SelectableFilesWidget::~SelectableFilesWidget() = default;

void SelectableFilesWidget::setAddFileFilter(const QString &filter)
{
    d->setAddFileFilter(filter);
}

void SelectableFilesWidget::setBaseDirEditable(bool edit)
{
    d->setBaseDirEditable(edit);
}

FilePaths SelectableFilesWidget::selectedFiles() const
{
    return d->selectedFiles();
}

FilePaths SelectableFilesWidget::selectedPaths() const
{
    return d->selectedPaths();
}

bool SelectableFilesWidget::hasFilesSelected() const
{
    return d->hasFilesSelected();
}

void SelectableFilesWidget::resetModel(const FilePath &path, const FilePaths &files)
{
    d->resetModel(path, files);
}

void SelectableFilesWidget::cancelParsing()
{
    d->cancelParsing();
}

void SelectableFilesWidget::enableFilterHistoryCompletion(const Key &keyPrefix)
{
    d->enableFilterHistoryCompletion(keyPrefix);
}

//////////
// SelectableFilesDialogs
//////////

SelectableFilesDialogEditFiles::SelectableFilesDialogEditFiles(const FilePath &path,
                                                               const FilePaths &files,
                                                               QWidget *parent)
    : QDialog(parent)
    , m_filesWidget(new SelectableFilesWidget(path, files))
{
    setWindowTitle(Tr::tr("Edit Files"));

    auto layout = new QVBoxLayout(this);
    layout->addWidget(m_filesWidget);

    m_filesWidget->setBaseDirEditable(false);
    m_filesWidget->enableFilterHistoryCompletion(Constants::ADD_FILES_DIALOG_FILTER_HISTORY_KEY);

    auto buttonBox = new QDialogButtonBox(Qt::Horizontal, this);
    buttonBox->setStandardButtons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);

    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttonBox);
}

FilePaths SelectableFilesDialogEditFiles::selectedFiles() const
{
    return m_filesWidget->selectedFiles();
}

//////////
// SelectableFilesDialogAddDirectory
//////////

SelectableFilesDialogAddDirectory::SelectableFilesDialogAddDirectory(const FilePath &path,
                                                                     const FilePaths &files,
                                                                     QWidget *parent)
    : SelectableFilesDialogEditFiles(path, files, parent)
{
    setWindowTitle(Tr::tr("Add Existing Directory"));

    m_filesWidget->setBaseDirEditable(true);
}

SelectableFilesFromDirModel::SelectableFilesFromDirModel(QObject *parent)
    : SelectableFilesModel(parent)
{
    connect(this, &SelectableFilesFromDirModel::dataChanged,
            this, &SelectableFilesModel::checkedFilesChanged);
    connect(this, &SelectableFilesFromDirModel::modelReset,
            this, &SelectableFilesModel::checkedFilesChanged);
}

#ifdef WITH_TESTS

namespace Internal {

class SelectableFilesTest final : public QObject
{
    Q_OBJECT

private slots:
    void testTheFormDrawsWithTheQmlItNames()
    {
        SelectableFilesWidget widget;
        const Result<> rendered = Core::aspectFormRenders(widget.aspectsForTest(),
                                                          "SelectableFilesForm.qml");
        QVERIFY2(rendered, qPrintable(rendered ? QString() : rendered.error()));
    }

    void testACellSaysItIsATickableCheckBox()
    {
        // Every row of this tree is a check box and every one can be ticked -
        // which is what the widget view read out of flags() and a Qt Quick one
        // cannot. The one column has no name, so it draws no heading.
        SelectableFilesModel model(nullptr);
        QCOMPARE(model.headerData(0, Qt::Horizontal, Qt::DisplayRole), QVariant());
        const QHash<int, QByteArray> names = model.roleNames();
        QCOMPARE(names.value(Qt::CheckStateRole), QByteArray("checkState"));
        QCOMPARE(names.value(AspectTable::CheckableRole), QByteArray("checkable"));
        QCOMPARE(names.value(AspectTable::EditableRole), QByteArray("editable"));
    }

    void testTheDirectoryIsOnlyAskedForWhereItCanBeChanged()
    {
        SelectableFilesWidget widget;
        SelectableFilesAspects *d = widget.aspectsForTest();

        widget.setBaseDirEditable(false);
        QVERIFY2(!d->baseDir.isVisible(), "a directory was asked for that the caller had chosen");
        QVERIFY(!d->startParsing.isVisible());

        widget.setBaseDirEditable(true);
        QVERIFY(d->baseDir.isVisible());
        QVERIFY(d->startParsing.isVisible());
    }

    void testNothingIsParsedUntilThereIsSomewhereToLook()
    {
        SelectableFilesWidget widget;
        SelectableFilesAspects *d = widget.aspectsForTest();
        QVERIFY2(!d->startParsing.isEnabled(), "parsing was offered with no directory to parse");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        d->baseDir.setValue(FilePath::fromString(dir.path()));
        QVERIFY(d->startParsing.isEnabled());

        d->baseDir.setValue(FilePath::fromString(dir.path() + "/not-there"));
        QVERIFY2(!d->startParsing.isEnabled(), "a directory that is not one was offered");
    }

    void testWhatIsSaidWhileTheFilesAreBeingRead()
    {
        // Nothing is said before there is anything to say, and what the form
        // asks is closed while it is reading.
        SelectableFilesWidget widget;
        SelectableFilesAspects *d = widget.aspectsForTest();
        QVERIFY(!d->progress.isVisible());
        QVERIFY(!d->preserved.isVisible());

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const FilePath base = FilePath::fromString(dir.path());
        QVERIFY(base.pathAppended("a.cpp").writeFileContents("a"));
        QVERIFY(base.pathAppended("notes.txt").writeFileContents("b"));

        QSignalSpy picked(&widget, &SelectableFilesWidget::selectedFilesChanged);
        widget.resetModel(base, {});
        // Reading is a task tree, so it has not finished here: the form is
        // closed and says what it is doing.
        QVERIFY2(!d->selectFilter.isEnabled(), "the filters could be changed mid-read");
        QVERIFY(d->progress.isVisible());

        QTRY_VERIFY(d->selectFilter.isEnabled());
        QVERIFY2(!d->progress.isVisible(),
                 "the form still said it was reading when it had stopped");
        QVERIFY(picked.count() > 0);
        // The filter the form opens with is source files, so the note beside
        // them is found and left unticked.
        QCOMPARE(widget.selectedFiles(), FilePaths{base.pathAppended("a.cpp")});
    }

    void testTheTreeIsOpenedWhereSomethingIsPickedUnderIt()
    {
        // The form opens the directories only some of whose files are ticked,
        // and the rows it names are the ones the drawn tree holds - a proxy's,
        // not the model's behind it. Nothing but a complaint says otherwise,
        // so the complaint is what is checked.
        QStringList complaints;
        const auto previous = qInstallMessageHandler(nullptr);
        static QStringList *collected = nullptr;
        collected = &complaints;
        qInstallMessageHandler([](QtMsgType, const QMessageLogContext &, const QString &text) {
            if (collected && text.contains("wrong model"))
                collected->append(text);
        });

        {
            SelectableFilesWidget widget;
            SelectableFilesAspects *d = widget.aspectsForTest();

            QTemporaryDir dir;
            QVERIFY(dir.isValid());
            const FilePath base = FilePath::fromString(dir.path());
            QVERIFY(base.pathAppended("sub").createDir());
            QVERIFY(base.pathAppended("sub/kept.cpp").writeFileContents("a"));
            QVERIFY(base.pathAppended("sub/left.txt").writeFileContents("b"));

            QSignalSpy opened(&d->files, &BaseAspect::controlExpandIndexRequested);
            widget.resetModel(base, {});
            QTRY_VERIFY(d->selectFilter.isEnabled());

            QVERIFY2(opened.count() > 0, "nothing was opened where a file had been ticked");
        }

        collected = nullptr;
        qInstallMessageHandler(previous);
        QVERIFY2(complaints.isEmpty(), qPrintable(complaints.join(", ")));
    }

    void testTheFilterIsAppliedToWhatWasFound()
    {
        SelectableFilesWidget widget;
        SelectableFilesAspects *d = widget.aspectsForTest();

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const FilePath base = FilePath::fromString(dir.path());
        QVERIFY(base.pathAppended("kept.cpp").writeFileContents("a"));
        QVERIFY(base.pathAppended("dropped.log").writeFileContents("b"));

        d->selectFilter.setValue("*.cpp");
        d->hideFilter.setValue("");
        widget.resetModel(base, {});
        QTRY_VERIFY(d->selectFilter.isEnabled());
        QCOMPARE(widget.selectedFiles(), FilePaths{base.pathAppended("kept.cpp")});

        // And the rows say what they are: a check box that can be ticked.
        QAbstractItemModel *rows = d->files.tableModel();
        const QModelIndex row = rows->index(0, 0, {});
        QVERIFY(row.isValid());
        QVERIFY(row.data(AspectTable::CheckableRole).toBool());
        QVERIFY(row.data(AspectTable::EditableRole).toBool());

        // And it is applied again when the reader says so, not before.
        d->selectFilter.setValue("*.log");
        QCOMPARE(widget.selectedFiles(), FilePaths{base.pathAppended("kept.cpp")});
        d->applyFilters.triggerAction();
        QCOMPARE(widget.selectedFiles(), FilePaths{base.pathAppended("dropped.log")});
    }
};

QObject *createSelectableFilesTest()
{
    return new SelectableFilesTest;
}

} // namespace Internal

#endif // WITH_TESTS

} // namespace ProjectExplorer

#ifdef WITH_TESTS
#include "selectablefilesmodel.moc"
#endif
