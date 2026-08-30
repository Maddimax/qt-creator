// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "logchangedialog.h"

#include "gitclient.h"
#include "gittr.h"

#include <coreplugin/dialogs/ioptionspage.h>

#include <vcsbase/vcsoutputwindow.h>

#ifdef WITH_TESTS
#include <QSignalSpy>
#include <QTest>
#endif

#include <utils/algorithm.h>
#include <utils/utilsicons.h>
#include <utils/qtcassert.h>

#include <QComboBox>
#include <QDialogButtonBox>
#include <QItemSelectionModel>
#include <QLabel>
#include <QModelIndex>
#include <QPainter>
#include <QPushButton>
#include <QStandardItemModel>
#include <QTimer>
#include <QTreeView>
#include <QVBoxLayout>

using namespace Utils;
using namespace VcsBase;

namespace Git::Internal {

enum Columns
{
    HashColumn,
    SubjectColumn,
    ColumnCount
};

std::optional<LogRow> parseLogLine(const QString &line)
{
    const int colonPos = line.indexOf(':');
    if (colonPos == -1)
        return {};
    return LogRow{line.left(colonPos),
                  line.right(line.size() - colonPos - 1),
                  line.endsWith(')')};
}

QStringList logArguments(const QString &commit, unsigned flags, const QString &excludedRemote)
{
    QStringList arguments{"--max-count=1000", "--format=%h:%s %d"};
    arguments << (commit.isEmpty() ? QString("HEAD") : commit);
    if (!(flags & LogChangeWidget::IncludeRemotes)) {
        QString remotesFlag("--remotes");
        if (!excludedRemote.isEmpty())
            remotesFlag += '=' + excludedRemote;
        arguments << "--not" << remotesFlag;
    }
    if (flags & LogChangeWidget::OmitMerges)
        arguments << "--no-merges";
    arguments << "--";
    return arguments;
}

// The cells one parsed line becomes. Not the reader's to edit, and bold where
// a ref points at the commit.
static QList<QStandardItem *> logRowItems(const LogRow &row)
{
    QList<QStandardItem *> items;
    for (int c = 0; c < ColumnCount; ++c) {
        auto item = new QStandardItem;
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        if (row.named) {
            QFont font = item->font();
            font.setBold(true);
            item->setFont(font);
        }
        items.push_back(item);
    }
    items[HashColumn]->setText(row.hash);
    items[SubjectColumn]->setText(row.subject);
    return items;
}

bool logRowIsStruckOut(LogRowMarks marks, int row, int currentRow)
{
    // Only where there is a chosen row to be before.
    return marks == LogRowMarks::StrikeOutBeforeCurrent && currentRow >= 0 && row < currentRow;
}

bool logRowHasIcon(LogRowMarks marks, int row, int currentRow, bool selected)
{
    switch (marks) {
    case LogRowMarks::IconUpToCurrent:
        return currentRow >= 0 && row <= currentRow;
    case LogRowMarks::IconOnSelected:
        return selected;
    case LogRowMarks::None:
    case LogRowMarks::StrikeOutBeforeCurrent:
        break;
    }
    return false;
}

class LogChangeModel : public QStandardItemModel
{
public:
    explicit LogChangeModel(LogChangeWidget *parent) : QStandardItemModel(0, ColumnCount, parent) {}

    QVariant data(const QModelIndex &index, int role) const override
    {
        if (role == Qt::ToolTipRole) {
            const QString revision = index.sibling(index.row(), HashColumn).data(Qt::EditRole).toString();
            const auto it = m_descriptions.constFind(revision);
            if (it != m_descriptions.constEnd())
                return *it;
            const QString desc = QString::fromUtf8(gitClient().synchronousShow(
                                 m_workingDirectory, revision, RunFlag::NoOutput));
            m_descriptions[revision] = desc;
            return desc;
        }
        if (role == Qt::FontRole && logRowIsStruckOut(m_marks, index.row(), m_currentRow)) {
            QFont font = QStandardItemModel::data(index, role).value<QFont>();
            font.setStrikeOut(true);
            return font;
        }
        if (role == Qt::DecorationRole && index.column() == HashColumn) {
            const bool selected = m_selectedRows.contains(index.row());
            if (logRowHasIcon(m_marks, index.row(), m_currentRow, selected))
                return m_icon;
            return {};
        }
        if (role == AspectTable::EditableRole)
            return AspectTable::isWritable(flags(index));
        return QStandardItemModel::data(index, role);
    }

    void setWorkingDirectory(const FilePath &workingDir) { m_workingDirectory = workingDir; }

    // Fills the rows from the repository's log, and answers which row should be
    // current: the one holding \a keepCommit if it is still there, else the
    // first. The git call lives here rather than in a view, so that a dialog
    // drawing these rows in Qt Quick needs no widget to fill them.
    Utils::Result<int> populate(const FilePath &repository, const QString &commit,
                                unsigned flags, const QString &excludedRemote,
                                const QString &keepCommit)
    {
        setWorkingDirectory(repository);
        if (const int rows = rowCount())
            removeRows(0, rows);

        const Result<QString> res = gitClient().synchronousLog(
            repository, logArguments(commit, flags, excludedRemote), RunFlag::NoOutput);
        if (!res)
            return ResultError(res.error());

        int selected = keepCommit.isEmpty() ? 0 : -1;
        const QStringList lines = res->split('\n');
        for (const QString &line : lines) {
            const std::optional<LogRow> parsed = parseLogLine(line);
            if (!parsed)
                continue;
            appendRow(logRowItems(*parsed));
            if (selected == -1 && keepCommit == parsed->hash)
                selected = rowCount() - 1;
        }
        return selected;
    }

    // Which rows are marked, and against what. The view used to paint this
    // itself; saying it here means every view of these rows agrees, and a Qt
    // Quick table needs no delegate at all.
    void setMarks(LogRowMarks marks) { m_marks = marks; refreshMarks(); }
    void setChosen(int currentRow, const QList<int> &selectedRows)
    {
        m_currentRow = currentRow;
        m_selectedRows = selectedRows;
        refreshMarks();
    }
    void setMarkIcon(const QIcon &icon) { m_icon = icon; }

    QHash<int, QByteArray> roleNames() const override
    {
        return AspectTable::withRoleNames(QStandardItemModel::roleNames());
    }

private:
    void refreshMarks()
    {
        if (const int rows = rowCount())
            emit dataChanged(index(0, 0), index(rows - 1, columnCount() - 1),
                             {Qt::FontRole, Qt::DecorationRole});
    }

    FilePath m_workingDirectory;
    mutable QHash<QString, QString> m_descriptions;
    LogRowMarks m_marks = LogRowMarks::None;
    int m_currentRow = -1;
    QList<int> m_selectedRows;
    QIcon m_icon;
};

LogChangeWidget::LogChangeWidget(QWidget *parent)
    : Utils::TreeView(parent)
    , m_model(new LogChangeModel(this))
{
    const QStringList headers = {Tr::tr("Hash"), Tr::tr("Subject")};
    m_model->setHorizontalHeaderLabels(headers);
    setModel(m_model);
    setMinimumWidth(300);
    setRootIsDecorated(false);
    setSelectionBehavior(QAbstractItemView::SelectRows);
    setActivationMode(Utils::DoubleClickActivation);
    connect(this, &LogChangeWidget::activated, this, &LogChangeWidget::emitCommitActivated);
    QTimer::singleShot(0, this, [this] { setFocus(); });
}

bool LogChangeWidget::init(const FilePath &repository, const QString &commit, LogFlags flags)
{
    m_model->setWorkingDirectory(repository);
    if (!populateLog(repository, commit, flags))
        return false;

    if (selectionMode() == QAbstractItemView::MultiSelection)
        selectionModel()->clearSelection();

    if (m_model->rowCount() > 0)
        return true;
    if (!(flags & Silent))
        VcsOutputWindow::appendError(repository, GitClient::msgNoCommits(flags & IncludeRemotes));
    return false;
}

QString LogChangeWidget::commit() const
{
    if (const QStandardItem *hashItem = currentItem(HashColumn))
        return hashItem->text();
    return {};
}

int LogChangeWidget::commitIndex() const
{
    const QModelIndex currentIndex = selectionModel()->currentIndex();
    if (currentIndex.isValid())
        return currentIndex.row();
    return -1;
}

/**
 * Returns a list of commit hashes suitable for cherry-picking.
 */
QStringList LogChangeWidget::commitList() const
{
    QModelIndexList selected = selectionModel()->selectedRows();
    std::sort(selected.begin(), selected.end(), [](const QModelIndex &a, const QModelIndex &b) {
        return a.row() > b.row(); // sort list bottom to top
    });
    const QStringList result = Utils::transform(selected, [](const QModelIndex &row) {
        return row.data().toString();
    });
    return result;
}

/**
 * Returns a commit range suitable for `git format-patch`.
 *
 * The format is {"-n", "hash"} or an empty string list if nothing was selected.
 */
QStringList LogChangeWidget::patchRange() const
{
    const QModelIndexList selected = selectionModel()->selectedRows();
    if (selected.isEmpty())
        return {};

    const QString size = QString::number(selected.size());
    const QStandardItem *highestItem = m_model->item(selected.first().row());
    QTC_ASSERT(highestItem, return {});
    const QString highestText = highestItem->text();
    const QStringList result = {"-" + size, highestText};
    return result;
}

bool LogChangeWidget::isRowSelected(int row) const
{
    return selectionModel()->isRowSelected(row);
}

QString LogChangeWidget::earliestCommit() const
{
    int rows = m_model->rowCount();
    if (rows) {
        if (const QStandardItem *item = m_model->item(rows - 1, HashColumn))
            return item->text();
    }
    return {};
}

void LogChangeWidget::setMarks(LogRowMarks marks)
{
    m_marks = marks;
    m_model->setMarks(marks);
    m_model->setMarkIcon(marks == LogRowMarks::IconUpToCurrent ? Utils::Icons::UNDO.icon()
                         : marks == LogRowMarks::IconOnSelected ? Utils::Icons::PLUS.icon()
                                                                : QIcon());
    m_model->setChosen(commitIndex(), Utils::transform(
        selectionModel()->selectedRows(), [](const QModelIndex &index) { return index.row(); }));
}

void LogChangeWidget::emitCommitActivated(const QModelIndex &index)
{
    if (index.isValid()) {
        const QString commit = index.sibling(index.row(), HashColumn).data().toString();
        if (!commit.isEmpty())
            emit commitActivated(commit);
    }
}

void LogChangeWidget::selectionChanged(const QItemSelection &selected,
                                       const QItemSelection &deselected)
{
    Utils::TreeView::selectionChanged(selected, deselected);
    emit hasSelectionChanged(!selectionModel()->selectedIndexes().isEmpty());

    // What is marked follows what is chosen. The model says which rows those
    // are and repaints them itself, so the view no longer has to work out
    // which ones changed.
    if (m_marks != LogRowMarks::None) {
        m_model->setChosen(commitIndex(), Utils::transform(
            selectionModel()->selectedRows(),
            [](const QModelIndex &index) { return index.row(); }));
    }
}

bool LogChangeWidget::populateLog(const FilePath &repository, const QString &commit,
                                  LogFlags flags)
{
    const Result<int> selected = m_model->populate(repository, commit, flags,
                                                   m_excludedRemote, this->commit());
    if (!selected) {
        VcsOutputWindow::appendError(repository, selected.error());
        return false;
    }
    setCurrentIndex(m_model->index(*selected, 0));
    return true;
}

const QStandardItem *LogChangeWidget::currentItem(int column) const
{
    const QModelIndex currentIndex = selectionModel()->currentIndex();
    if (currentIndex.isValid())
        return m_model->item(currentIndex.row(), column);
    return nullptr;
}

class LogChangeSettings final : public AspectContainer
{
public:
    explicit LogChangeSettings(QAbstractItemModel *model)
    {
        setAutoApply(true);
        setQmlSource(QUrl("qrc:/qt/qml/QtCreator/Git/LogChangeDialog.qml"));

        prompt.setQmlName("Prompt");
        hint.setQmlName("Hint");
        hint.setText(Tr::tr("Hint: Select or deselect a single commit with a mouse click "
                            "and multiple commits by dragging the mouse over them."));
        // Only where more than one can be picked.
        hint.setVisible(false);

        commits.setQmlName("Commits");
        commits.setModel(model);

        resetType.setQmlName("ResetType");
        resetType.setLabelText(Tr::tr("Reset type:"));
        resetType.setDisplayStyle(SelectionAspect::DisplayStyle::ComboBox);
        resetType.addOption({Tr::tr("Hard"), {}, "--hard"});
        resetType.addOption({Tr::tr("Mixed"), {}, "--mixed"});
        resetType.addOption({Tr::tr("Soft"), {}, "--soft"});
        // Only the reset dialog asks how far to reset.
        resetType.setVisible(false);
    }

    TextDisplay prompt{this};
    TextDisplay hint{this};
    TableAspect commits{this};
    SelectionAspect resetType{this};
};

LogChangeDialog::LogChangeDialog(DialogType type, QWidget *parent)
    : QDialog(parent)
    , m_model(new LogChangeModel(nullptr))
    , m_settings(new LogChangeSettings(m_model))
    , m_dialogButtonBox(new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this))
{
    m_model->setParent(this);

    const bool isReset = type == Reset;
    m_settings->prompt.setText(isReset ? Tr::tr("Reset to:") : Tr::tr("Select change:"));
    if (isReset) {
        m_settings->resetType.setVisible(true);
        m_settings->resetType.setValue(settings().lastResetIndex());
    }

    auto layout = new QVBoxLayout(this);
    layout->addWidget(Core::createAspectForm(m_settings.get()));
    layout->addWidget(m_dialogButtonBox);

    connect(m_dialogButtonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_dialogButtonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    QPushButton *const okButton = m_dialogButtonBox->button(QDialogButtonBox::Ok);
    connect(&m_settings->commits, &TableAspect::rowActivated,
            okButton, [okButton] { okButton->animateClick(); });
    connect(&m_settings->commits, &TableAspect::chosenChanged, this, [this, okButton] {
        okButton->setEnabled(!m_settings->commits.selectedRows().isEmpty());
        m_model->setChosen(m_settings->commits.currentRow(), m_settings->commits.selectedRows());
    });
    okButton->setEnabled(false);

    resize(600, 400);
}

LogChangeDialog::~LogChangeDialog() = default;

void LogChangeDialog::setMultiSelect(bool multi)
{
    // The widget dialog told the view its selection mode; a Quick table always
    // takes several, so what is left is saying so.
    m_settings->hint.setVisible(multi);
}

void LogChangeDialog::setMarks(LogRowMarks marks)
{
    m_model->setMarks(marks);
    m_model->setMarkIcon(marks == LogRowMarks::IconUpToCurrent ? Utils::Icons::UNDO.icon()
                         : marks == LogRowMarks::IconOnSelected ? Utils::Icons::PLUS.icon()
                                                                : QIcon());
}

bool LogChangeDialog::runDialog(const FilePath &repository,
                                const QString &commit,
                                LogChangeWidget::LogFlags flags)
{
    const Result<int> selected = m_model->populate(repository, commit, flags, {}, this->commit());
    if (!selected) {
        VcsOutputWindow::appendError(repository, selected.error());
        return false;
    }
    if (m_model->rowCount() == 0) {
        if (!(flags & LogChangeWidget::Silent)) {
            VcsOutputWindow::appendError(
                repository, GitClient::msgNoCommits(flags & LogChangeWidget::IncludeRemotes));
        }
        return false;
    }
    m_settings->commits.showRow(*selected);

    if (QDialog::exec() == QDialog::Accepted) {
        if (m_settings->resetType.isVisible())
            settings().lastResetIndex.setValue(m_settings->resetType.value());
        return true;
    }
    return false;
}

QString LogChangeDialog::commit() const
{
    if (const QStandardItem *item = m_model->item(m_settings->commits.currentRow(), HashColumn))
        return item->text();
    return {};
}

int LogChangeDialog::commitIndex() const
{
    return m_settings->commits.currentRow();
}

QStringList LogChangeDialog::commitList() const
{
    // Bottom to top, which is the order they have to be applied in.
    QList<int> rows = m_settings->commits.selectedRows();
    std::reverse(rows.begin(), rows.end());
    return Utils::transform(rows, [this](int row) {
        const QStandardItem *item = m_model->item(row, HashColumn);
        return item ? item->text() : QString();
    });
}

QStringList LogChangeDialog::patchRange() const
{
    const QList<int> rows = m_settings->commits.selectedRows();
    if (rows.isEmpty())
        return {};
    const QStandardItem *highest = m_model->item(rows.first(), HashColumn);
    QTC_ASSERT(highest, return {});
    return {"-" + QString::number(rows.size()), highest->text()};
}

QString LogChangeDialog::resetFlag() const
{
    if (!m_settings->resetType.isVisible())
        return {};
    return m_settings->resetType.itemValue().toString();
}


#ifdef WITH_TESTS

class LogChangeMarksTest final : public QObject
{
    Q_OBJECT

private slots:
    void testNothingIsMarkedWhenNothingAsksFor()
    {
        for (int row = 0; row < 3; ++row) {
            QVERIFY(!logRowIsStruckOut(LogRowMarks::None, row, 1));
            QVERIFY(!logRowHasIcon(LogRowMarks::None, row, 1, true));
        }
    }

    void testResetStrikesOutWhatItWouldDiscard()
    {
        // The log is newest first, so the rows *above* the chosen commit are
        // the ones a reset throws away.
        QVERIFY2(logRowIsStruckOut(LogRowMarks::StrikeOutBeforeCurrent, 0, 2),
                 "a commit that would be discarded was not marked");
        QVERIFY(logRowIsStruckOut(LogRowMarks::StrikeOutBeforeCurrent, 1, 2));

        // The chosen one stays, and so does everything below it.
        QVERIFY2(!logRowIsStruckOut(LogRowMarks::StrikeOutBeforeCurrent, 2, 2),
                 "the commit being reset to was marked as discarded");
        QVERIFY(!logRowIsStruckOut(LogRowMarks::StrikeOutBeforeCurrent, 3, 2));

        // With nothing chosen there is nothing to be before.
        QVERIFY2(!logRowIsStruckOut(LogRowMarks::StrikeOutBeforeCurrent, 0, -1),
                 "commits were struck out before anything was chosen");
    }

    void testRebaseMarksWhatItWouldReplay()
    {
        // The chosen commit and everything newer than it are replayed, so the
        // mark reaches down to it inclusive.
        QVERIFY(logRowHasIcon(LogRowMarks::IconUpToCurrent, 0, 2, false));
        QVERIFY2(logRowHasIcon(LogRowMarks::IconUpToCurrent, 2, 2, false),
                 "the commit being rebased onto was not marked");
        QVERIFY2(!logRowHasIcon(LogRowMarks::IconUpToCurrent, 3, 2, false),
                 "a commit below the chosen one was marked for replay");
        QVERIFY(!logRowHasIcon(LogRowMarks::IconUpToCurrent, 0, -1, false));
    }

    void testPatchMarksWhatWasPicked()
    {
        // Exporting follows the selection, not the row the keyboard is on.
        QVERIFY(logRowHasIcon(LogRowMarks::IconOnSelected, 5, -1, true));
        QVERIFY2(!logRowHasIcon(LogRowMarks::IconOnSelected, 2, 2, false),
                 "the current row was marked although it was not picked");
    }

    void testTheModelSaysWhichRowsAreMarked()
    {
        // The rules used to be three item delegates painting over the view.
        // Answering them here is what lets a Qt Quick table show the same
        // marks with no delegate at all.
        LogChangeModel model(nullptr);
        model.setColumnCount(ColumnCount);
        for (int row = 0; row < 3; ++row) {
            model.appendRow({new QStandardItem(QString("hash%1").arg(row)),
                             new QStandardItem(QString("subject%1").arg(row))});
        }

        model.setMarks(LogRowMarks::StrikeOutBeforeCurrent);
        model.setChosen(2, {});
        QVERIFY2(model.data(model.index(0, SubjectColumn), Qt::FontRole)
                     .value<QFont>().strikeOut(),
                 "a discarded commit was not struck out");
        QVERIFY(!model.data(model.index(2, SubjectColumn), Qt::FontRole)
                     .value<QFont>().strikeOut());

        // And an icon only where one was asked for, in the hash column.
        model.setMarks(LogRowMarks::IconUpToCurrent);
        model.setMarkIcon(Utils::Icons::UNDO.icon());
        model.setChosen(1, {});
        QVERIFY2(!model.data(model.index(0, HashColumn), Qt::DecorationRole).isNull(),
                 "a replayed commit carried no mark");
        QVERIFY2(model.data(model.index(2, HashColumn), Qt::DecorationRole).isNull(),
                 "a commit below the chosen one carried a mark");
        QVERIFY2(model.data(model.index(0, SubjectColumn), Qt::DecorationRole).isNull(),
                 "the mark was drawn in every column");
    }

    void testWhatALogLineSays()
    {
        // The format is "%h:%s %d" - hash, colon, subject, and the refs
        // pointing at it, if any.
        const std::optional<LogRow> plain = parseLogLine("abc1234: Fix the thing ");
        QVERIFY(plain);
        QCOMPARE(plain->hash, QString("abc1234"));
        QCOMPARE(plain->subject, QString(" Fix the thing "));
        QVERIFY2(!plain->named, "a commit with no refs was shown as if it had one");

        // A commit some ref points at is shown in bold, and the format ends
        // it with the refs in brackets.
        const std::optional<LogRow> named = parseLogLine("def5678: Release  (HEAD -> master)");
        QVERIFY(named);
        QCOMPARE(named->hash, QString("def5678"));
        QVERIFY2(named->named, "a commit a ref points at was not marked");

        // A subject may contain a colon; only the first one separates.
        const std::optional<LogRow> colon = parseLogLine("abc1234: git: do the thing");
        QVERIFY(colon);
        QCOMPARE(colon->hash, QString("abc1234"));
        QCOMPARE(colon->subject, QString(" git: do the thing"));

        // Anything without a colon is not a log line - git's own blank last
        // line is the usual one.
        QVERIFY2(!parseLogLine(""), "an empty line was taken for a commit");
        QVERIFY(!parseLogLine("no colon here"));
    }

    void testHowTheLogIsAskedFor()
    {
        // What the flags mean, which is otherwise only visible by running git.
        const QStringList plain = logArguments({}, LogChangeWidget::None, {});
        QVERIFY2(plain.contains("HEAD"), qPrintable(plain.join(' ')));
        QVERIFY2(plain.contains("--not") && plain.contains("--remotes"),
                 "commits already on a remote were included");
        QVERIFY2(!plain.contains("--no-merges"), "merges were omitted without being asked");

        // A commit to start from replaces HEAD.
        const QStringList from = logArguments("abc1234", LogChangeWidget::None, {});
        QVERIFY(from.contains("abc1234"));
        QVERIFY(!from.contains("HEAD"));

        // Including remotes drops the exclusion entirely.
        const QStringList remotes = logArguments({}, LogChangeWidget::IncludeRemotes, {});
        QVERIFY2(!remotes.contains("--not"), "remotes were still excluded");

        // One remote can be singled out - the push dialog excludes only the
        // one it is pushing to.
        const QStringList excluded = logArguments({}, LogChangeWidget::None, "origin");
        QVERIFY2(excluded.contains("--remotes=origin"), qPrintable(excluded.join(' ')));

        QVERIFY(logArguments({}, LogChangeWidget::OmitMerges, {}).contains("--no-merges"));
    }

    void testTheDialogDrawsWithTheQmlItNames()
    {
        LogChangeModel model(nullptr);
        LogChangeSettings settings(&model);
        const Result<> rendered = Core::aspectFormRenders(&settings, "LogChangeDialog.qml");
        QVERIFY2(rendered, qPrintable(rendered ? QString() : rendered.error()));
    }

    void testOnlyTheResetDialogAsksHowFar()
    {
        // "Select change" has nothing to reset, so the reset type is not part
        // of it; the reset dialog answers with what git wants on the command
        // line.
        LogChangeDialog select(LogChangeDialog::Select, nullptr);
        QVERIFY2(!select.m_settings->resetType.isVisible(),
                 "the select dialog offered a reset type");
        QCOMPARE(select.resetFlag(), QString());

        LogChangeDialog reset(LogChangeDialog::Reset, nullptr);
        QVERIFY(reset.m_settings->resetType.isVisible());
        reset.m_settings->resetType.setValue(0);
        QCOMPARE(reset.resetFlag(), QString("--hard"));
        reset.m_settings->resetType.setValue(2);
        QCOMPARE(reset.resetFlag(), QString("--soft"));
    }

    void testTheHintIsShownOnlyWhereSeveralCanBePicked()
    {
        LogChangeDialog dialog(LogChangeDialog::Select, nullptr);
        QVERIFY2(!dialog.m_settings->hint.isVisible(),
                 "the dialog explained multi-selection where there is none");
        dialog.setMultiSelect(true);
        QVERIFY(dialog.m_settings->hint.isVisible());
    }

    void testWhichCommitsThePickAnswersWith()
    {
        LogChangeDialog dialog(LogChangeDialog::Select, nullptr);
        for (int row = 0; row < 4; ++row) {
            dialog.m_model->appendRow(logRowItems(
                {QString("hash%1").arg(row), QString("subject%1").arg(row), false}));
        }

        // Nothing picked is nothing to answer with.
        QCOMPARE(dialog.commit(), QString());
        QVERIFY(dialog.commitList().isEmpty());
        QVERIFY(dialog.patchRange().isEmpty());

        dialog.m_settings->commits.setCurrentRow(1);
        QCOMPARE(dialog.commit(), QString("hash1"));
        QCOMPARE(dialog.commitIndex(), 1);

        // A list of commits to apply comes back bottom to top, because that is
        // the order they have to be applied in.
        dialog.m_settings->commits.setSelectedRows({0, 2});
        QCOMPARE(dialog.commitList(), (QStringList{"hash2", "hash0"}));

        // And a range for format-patch is a count and the newest of them.
        QCOMPARE(dialog.patchRange(), (QStringList{"-2", "hash0"}));
    }

    void testTheDrawnTableHandsBackWhatWasPicked()
    {
        // Three lines in the .qml carry the dialog.
        LogChangeModel model(nullptr);
        LogChangeSettings settings(&model);
        for (int row = 0; row < 3; ++row)
            model.appendRow(logRowItems({QString("hash%1").arg(row), "subject", false}));

        const std::unique_ptr<QWidget> form(Core::createAspectForm(&settings));
        QVERIFY(form);
        QObject *const root = Core::aspectFormRoot(form.get());
        QVERIFY2(root, "no front end said what the dialog was drawn from");

        QObject *table = nullptr;
        QTRY_VERIFY(table = root->findChild<QObject *>("commitTable"));

        QSignalSpy activated(&settings.commits, &TableAspect::rowActivated);
        QVERIFY(QMetaObject::invokeMethod(table, "rowActivated", Q_ARG(int, 2)));
        QTRY_COMPARE(activated.count(), 1);
        QCOMPARE(settings.commits.currentRow(), 2);

        // And the dialog can put the reader on a row, which the view follows.
        settings.commits.showRow(0);
        QTRY_COMPARE(table->property("currentRow").toInt(), 0);
    }

    void testALogRowIsNotEditedInTheCell()
    {
        LogChangeModel model(nullptr);
        model.setColumnCount(ColumnCount);
        auto *const hash = new QStandardItem("abc1234");
        hash->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        model.appendRow({hash, new QStandardItem("subject")});

        const QVariant editable = model.data(model.index(0, 0), AspectTable::EditableRole);
        QVERIFY2(editable.isValid(), "the table was never told whether a cell may be written to");
        QVERIFY2(!editable.toBool(), "a commit could be edited by typing in the list");
        QVERIFY2(model.roleNames().values().contains("display"),
                 "a table cannot read what a cell says");
    }
};

QObject *createLogChangeMarksTest()
{
    return new LogChangeMarksTest;
}

#endif // WITH_TESTS

} // Git::Internal

#ifdef WITH_TESTS
#include "logchangedialog.moc"
#endif
