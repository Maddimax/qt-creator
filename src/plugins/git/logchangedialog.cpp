// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "logchangedialog.h"

#include "gitclient.h"
#include "gittr.h"

#include <vcsbase/vcsoutputwindow.h>

#ifdef WITH_TESTS
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

bool LogChangeWidget::populateLog(const FilePath &repository, const QString &commit, LogFlags flags)
{
    const QString currentCommit = this->commit();
    int selected = currentCommit.isEmpty() ? 0 : -1;
    if (const int rowCount = m_model->rowCount())
        m_model->removeRows(0, rowCount);

    // Retrieve log using a custom format "Hash:Subject [(refs)]"
    QStringList arguments;
    arguments << "--max-count=1000" << "--format=%h:%s %d";
    arguments << (commit.isEmpty() ? "HEAD" : commit);
    if (!(flags & IncludeRemotes)) {
        QString remotesFlag("--remotes");
        if (!m_excludedRemote.isEmpty())
            remotesFlag += '=' + m_excludedRemote;
        arguments << "--not" << remotesFlag;
    }
    if (flags & OmitMerges)
        arguments << "--no-merges";
    arguments << "--";

    const Result<QString> res = gitClient().synchronousLog(repository, arguments, RunFlag::NoOutput);
    if (!res) {
        VcsOutputWindow::appendError(repository, res.error());
        return false;
    }

    const QStringList lines = res->split('\n');
    for (const QString &line : lines) {
        const int colonPos = line.indexOf(':');
        if (colonPos != -1) {
            QList<QStandardItem *> row;
            for (int c = 0; c < ColumnCount; ++c) {
                auto item = new QStandardItem;
                item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
                if (line.endsWith(')')) {
                    QFont font = item->font();
                    font.setBold(true);
                    item->setFont(font);
                }
                row.push_back(item);
            }
            const QString hash = line.left(colonPos);
            row[HashColumn]->setText(hash);
            row[SubjectColumn]->setText(line.right(line.size() - colonPos - 1));
            m_model->appendRow(row);
            if (selected == -1 && currentCommit == hash)
                selected = m_model->rowCount() - 1;
        }
    }
    setCurrentIndex(m_model->index(selected, 0));
    return true;
}

const QStandardItem *LogChangeWidget::currentItem(int column) const
{
    const QModelIndex currentIndex = selectionModel()->currentIndex();
    if (currentIndex.isValid())
        return m_model->item(currentIndex.row(), column);
    return nullptr;
}

LogChangeDialog::LogChangeDialog(DialogType type, QWidget *parent) :
    QDialog(parent)
    , m_widget(new LogChangeWidget)
    , m_dialogButtonBox(new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this))
{
    const bool isReset = type == Reset;
    auto layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(isReset ? Tr::tr("Reset to:") : Tr::tr("Select change:"), this));
    m_selectionHintLabel = new QLabel(
                Tr::tr("Hint: Select or deselect a single commit with a mouse click "
                       "and multiple commits by dragging the mouse over them."), this);
    m_selectionHintLabel->setVisible(false);
    layout->addWidget(m_selectionHintLabel);
    layout->addWidget(m_widget);
    auto popUpLayout = new QHBoxLayout;
    if (isReset) {
        popUpLayout->addWidget(new QLabel(Tr::tr("Reset type:")));
        m_resetTypeComboBox = new QComboBox;
        m_resetTypeComboBox->addItem(Tr::tr("Hard"), "--hard");
        m_resetTypeComboBox->addItem(Tr::tr("Mixed"), "--mixed");
        m_resetTypeComboBox->addItem(Tr::tr("Soft"), "--soft");
        m_resetTypeComboBox->setCurrentIndex(settings().lastResetIndex());
        popUpLayout->addWidget(m_resetTypeComboBox);
        popUpLayout->addItem(new QSpacerItem(0, 0, QSizePolicy::Expanding, QSizePolicy::Ignored));
    }

    popUpLayout->addWidget(m_dialogButtonBox);
    QPushButton *okButton = m_dialogButtonBox->button(QDialogButtonBox::Ok);
    layout->addLayout(popUpLayout);

    connect(m_dialogButtonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_dialogButtonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    connect(m_widget, &LogChangeWidget::activated, okButton, [okButton] { okButton->animateClick(); });
    connect(m_widget, &LogChangeWidget::hasSelectionChanged, this, [this](bool hasSelection) {
        m_dialogButtonBox->button(QDialogButtonBox::Ok)->setEnabled(hasSelection);
    });


    resize(600, 400);
}

void LogChangeDialog::setSelectionMode(QAbstractItemView::SelectionMode mode)
{
    m_widget->setSelectionMode(mode);
    m_selectionHintLabel->setVisible(mode == QAbstractItemView::SelectionMode::MultiSelection);
}

bool LogChangeDialog::runDialog(const FilePath &repository,
                                const QString &commit,
                                LogChangeWidget::LogFlags flags)
{
    if (!m_widget->init(repository, commit, flags))
        return false;

    if (QDialog::exec() == QDialog::Accepted) {
        if (m_resetTypeComboBox)
            settings().lastResetIndex.setValue(m_resetTypeComboBox->currentIndex());
        return true;
    }
    return false;
}

QString LogChangeDialog::commit() const
{
    return m_widget->commit();
}

int LogChangeDialog::commitIndex() const
{
    return m_widget->commitIndex();
}

QStringList LogChangeDialog::commitList() const
{
    return m_widget->commitList();
}

QStringList LogChangeDialog::patchRange() const
{
    return m_widget->patchRange();
}

QString LogChangeDialog::resetFlag() const
{
    if (!m_resetTypeComboBox)
        return {};
    return m_resetTypeComboBox->itemData(m_resetTypeComboBox->currentIndex()).toString();
}

LogChangeWidget *LogChangeDialog::widget() const
{
    return m_widget;
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
