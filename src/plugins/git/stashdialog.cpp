// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "stashdialog.h"

#include "gitclient.h"
#include "gitplugin.h"
#include "gittr.h"
#include "gitutils.h"

#include <coreplugin/dialogs/ioptionspage.h>

#include <utils/algorithm.h>
#include <utils/aspects.h>
#include <utils/qtcassert.h>

#ifdef WITH_TESTS
#include <QSignalSpy>
#include <QTest>
#endif

#include <QApplication>
#include <QDateTime>
#include <QDebug>
#include <QDialogButtonBox>
#include <QDir>
#include <QMessageBox>
#include <QModelIndex>
#include <QPushButton>
#include <QStandardItemModel>
#include <QVBoxLayout>

#include <memory>

using namespace Utils;

enum { NameColumn, BranchColumn, MessageColumn, ColumnCount };

namespace Git::Internal {

static QList<QStandardItem*> stashModelRowItems(const Stash &s)
{
    Qt::ItemFlags itemFlags = Qt::ItemIsSelectable | Qt::ItemIsEnabled;
    auto nameItem = new QStandardItem(s.name);
    nameItem->setFlags(itemFlags);
    auto branchItem = new QStandardItem(s.branch);
    branchItem->setFlags(itemFlags);
    auto messageItem = new QStandardItem(s.message);
    messageItem->setFlags(itemFlags);
    QList<QStandardItem*> rc;
    rc << nameItem << branchItem << messageItem;
    return rc;
}

// -----------  StashModel
class StashModel : public QStandardItemModel
{
public:
    explicit StashModel(QObject *parent = nullptr);

    void setStashes(const QList<Stash> &stashes);
    const Stash &at(int i) { return m_stashes.at(i); }

    // A Quick table asks the model whether a cell may be written to, and takes
    // a cell that says nothing to be the reader's. A stash is changed by the
    // buttons beside the list, not by typing over it.
    QVariant data(const QModelIndex &index, int role) const override
    {
        if (role == AspectTable::EditableRole)
            return AspectTable::isWritable(flags(index));
        return QStandardItemModel::data(index, role);
    }

    QHash<int, QByteArray> roleNames() const override
    {
        return AspectTable::withRoleNames(QStandardItemModel::roleNames());
    }

private:
    QList<Stash> m_stashes;
};

StashModel::StashModel(QObject *parent) :
    QStandardItemModel(0, ColumnCount, parent)
{
    setHorizontalHeaderLabels({Tr::tr("Name"), Tr::tr("Branch"), Tr::tr("Message")});
}

void StashModel::setStashes(const QList<Stash> &stashes)
{
    m_stashes = stashes;
    if (const int rows = rowCount())
        removeRows(0, rows);
    for (const Stash &s : stashes)
        appendRow(stashModelRowItems(s));
}

// What the buttons beside the list offer. Pulled out of enableButtons() so
// that the six can be compared without a repository to open.
struct StashActions
{
    bool canDeleteAll = false;
    bool canDeleteSelection = false;
    bool canShow = false;
    bool canRestore = false;
    bool canRestoreInBranch = false;
    bool canRefresh = false;

    bool operator==(const StashActions &) const = default;
};

static StashActions actionsFor(bool hasRepository, int stashCount, int currentRow,
                               int selectedCount)
{
    const bool hasStashes = hasRepository && stashCount > 0;
    // Showing or restoring acts on the one the reader is on; deleting acts on
    // everything they picked.
    const bool hasCurrent = hasStashes && currentRow >= 0;
    return {hasStashes, selectedCount > 0, hasCurrent, hasCurrent, hasCurrent, hasRepository};
}

class StashTableAspect final : public BaseAspect
{
    Q_OBJECT

public:
    StashTableAspect(AspectContainer *container, QAbstractItemModel *model)
        : BaseAspect(container), m_model(model)
    {}

    AspectPresentation presentation() const override
    {
        AspectPresentation p = BaseAspect::presentation();
        p.control = AspectControls::Table;
        p.filterPlaceholderText = Tr::tr("Filter");
        return p;
    }

    QAbstractItemModel *tableModel() override { return m_model; }

    Q_INVOKABLE void setCurrentRow(int row)
    {
        if (m_currentRow == row)
            return;
        m_currentRow = row;
        emit chosenChanged();
    }

    Q_INVOKABLE void setSelectedRows(const QVariantList &rows)
    {
        QList<int> selected;
        for (const QVariant &row : rows)
            selected << row.toInt();
        if (m_selectedRows == selected)
            return;
        m_selectedRows = selected;
        emit chosenChanged();
    }

    Q_INVOKABLE void activateRow(int row)
    {
        setCurrentRow(row);
        emit rowActivated();
    }

    int currentRow() const { return m_currentRow; }
    QList<int> selectedRows() const { return Utils::sorted(m_selectedRows); }

signals:
    void chosenChanged();
    void rowActivated();

private:
    QAbstractItemModel *const m_model;
    int m_currentRow = -1;
    QList<int> m_selectedRows;
};

class StashSettings final : public AspectContainer
{
public:
    explicit StashSettings(QAbstractItemModel *model)
        : stashes(this, model)
    {
        setAutoApply(true);
        setQmlSource(QUrl("qrc:/qt/qml/QtCreator/Git/StashDialog.qml"));

        repository.setQmlName("Repository");
        stashes.setQmlName("Stashes");

        show.setQmlName("Show");
        show.setActionText(Tr::tr("&Show"));
        refresh.setQmlName("Refresh");
        refresh.setActionText(Tr::tr("Re&fresh"));
        restore.setQmlName("Restore");
        restore.setActionText(Tr::tr("R&estore..."));
        //: Restore a git stash to new branch to be created
        restoreInBranch.setQmlName("RestoreInBranch");
        restoreInBranch.setActionText(Tr::tr("Restore to &Branch..."));
        deleteSelection.setQmlName("DeleteSelection");
        deleteSelection.setActionText(Tr::tr("&Delete..."));
        deleteAll.setQmlName("DeleteAll");
        deleteAll.setActionText(Tr::tr("Delete &All..."));
    }

    TextDisplay repository{this};
    StashTableAspect stashes;
    ActionAspect show{this};
    ActionAspect refresh{this};
    ActionAspect restore{this};
    ActionAspect restoreInBranch{this};
    ActionAspect deleteSelection{this};
    ActionAspect deleteAll{this};
};

// ---------- StashDialog
StashDialog::StashDialog(QWidget *parent)
    : QDialog(parent)
    , m_model(new StashModel(this))
    , m_settings(new StashSettings(m_model))
{
    setAttribute(Qt::WA_DeleteOnClose, true);  // Do not update unnecessarily
    setWindowTitle(Tr::tr("Stashes"));

    resize(599, 485);

    m_settings->show.setAction([this] { showCurrent(); });
    m_settings->refresh.setAction([this] { forceRefresh(); });
    m_settings->restore.setAction([this] { restoreCurrent(); });
    m_settings->restoreInBranch.setAction([this] { restoreCurrentInBranch(); });
    m_settings->deleteSelection.setAction([this] { deleteSelection(); });
    m_settings->deleteAll.setAction([this] { deleteAll(); });

    auto buttonBox = new QDialogButtonBox(QDialogButtonBox::Close, this);

    auto layout = new QVBoxLayout(this);
    layout->addWidget(Core::createAspectForm(m_settings.get()));
    layout->addWidget(buttonBox);

    connect(&m_settings->stashes, &StashTableAspect::chosenChanged,
            this, &StashDialog::enableButtons);
    connect(&m_settings->stashes, &StashTableAspect::rowActivated,
            this, &StashDialog::showCurrent);

    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    enableButtons();
}

StashDialog::~StashDialog() = default;

void StashDialog::refresh(const FilePath &repository, bool force)
{
    if (m_repository == repository && !force)
        return;
    // Refresh
    m_repository = repository;
    m_settings->repository.setText(msgRepositoryLabel(repository));
    if (m_repository.isEmpty())
        m_model->setStashes({});
    else
        m_model->setStashes(gitClient().synchronousStashList(m_repository));
    enableButtons();
}

void StashDialog::deleteAll()
{
    const QString title = Tr::tr("Delete Stashes");
    if (!ask(title, Tr::tr("Do you want to delete all stashes?")))
        return;
    QString errorMessage;
    if (gitClient().synchronousStashRemove(m_repository, QString(), &errorMessage))
        refresh(m_repository, true);
    else
        warning(title, errorMessage);
}

void StashDialog::deleteSelection()
{
    const QList<int> rows = selectedRows();
    QTC_ASSERT(!rows.isEmpty(), return);
    const QString title = Tr::tr("Delete Stashes");
    if (!ask(title, Tr::tr("Do you want to delete %n stash(es)?", nullptr, rows.size())))
        return;
    QString errorMessage;
    QStringList errors;
    // Delete in reverse order as stashes rotate
    for (int r = rows.size() - 1; r >= 0; r--)
        if (!gitClient().synchronousStashRemove(m_repository, m_model->at(rows.at(r)).name, &errorMessage))
            errors.push_back(errorMessage);
    refresh(m_repository, true);
    if (!errors.isEmpty())
        warning(title, errors.join('\n'));
}

void StashDialog::showCurrent()
{
    const int index = currentRow();
    QTC_ASSERT(index >= 0, return);
    gitClient().show(m_repository, QString(m_model->at(index).name));
}

// Suggest Branch name to restore 'stash@{0}' -> 'stash0-date'
static inline QString stashRestoreDefaultBranch(QString stash)
{
    stash.remove('{');
    stash.remove('}');
    stash.remove('@');
    stash += '-';
    stash += QDateTime::currentDateTime().toString("yyMMddhhmmss");
    return stash;
}

// Return next stash id 'stash@{0}' -> 'stash@{1}'
static inline QString nextStash(const QString &stash)
{
    const int openingBracePos = stash.indexOf('{');
    if (openingBracePos == -1)
        return {};
    const int closingBracePos = stash.indexOf('}', openingBracePos + 2);
    if (closingBracePos == -1)
        return {};
    bool ok;
    const int n = stash.mid(openingBracePos + 1, closingBracePos - openingBracePos - 1).toInt(&ok);
    if (!ok)
        return {};
    QString rc =  stash.left(openingBracePos + 1);
    rc += QString::number(n + 1);
    rc += '}';
    return rc;
}

StashDialog::ModifiedRepositoryAction StashDialog::promptModifiedRepository(const QString &stash)
{
    QMessageBox box(QMessageBox::Question,
                    Tr::tr("Repository Modified"),
                    Tr::tr("%1 cannot be restored since the repository is modified.\n"
                       "You can choose between stashing the changes or discarding them.").arg(stash),
                    QMessageBox::Cancel, this);
    QPushButton *stashButton = box.addButton(Tr::tr("Stash"), QMessageBox::AcceptRole);
    QPushButton *discardButton = box.addButton(Tr::tr("Discard"), QMessageBox::AcceptRole);
    box.exec();
    const QAbstractButton *clickedButton = box.clickedButton();
    if (clickedButton == stashButton)
        return ModifiedRepositoryStash;
    if (clickedButton == discardButton)
        return ModifiedRepositoryDiscard;
    return ModifiedRepositoryCancel;
}

// Prompt for restore: Make sure repository is unmodified,
// prompt for a branch if desired or just ask to restore.
// Note that the stash to be restored changes if the user
// chooses to stash away modified repository.
bool StashDialog::promptForRestore(QString *stash,
                                   QString *branch /* = 0*/,
                                   QString *errorMessage)
{
    const QString stashIn = *stash;
    bool modifiedPromptShown = false;
    switch (gitClient().gitStatus(
                m_repository, StatusModes(NoUntracked | NoSubmodules), nullptr, errorMessage)) {
    case StatusResult::Failed:
        return false;
    case StatusResult::Changed: {
            switch (promptModifiedRepository(*stash)) {
            case ModifiedRepositoryCancel:
                return false;
            case ModifiedRepositoryStash:
                if (gitClient().synchronousStash(
                            m_repository, QString(), GitClient::StashPromptDescription).isEmpty()) {
                    return false;
                }
                *stash = nextStash(*stash); // Our stash id to be restored changed
                QTC_ASSERT(!stash->isEmpty(), return false);
                break;
            case ModifiedRepositoryDiscard:
                if (!gitClient().synchronousReset(m_repository))
                    return false;
                break;
            }
        modifiedPromptShown = true;
    }
        break;
    case StatusResult::Unchanged:
        break;
    }
    // Prompt for branch or just ask.
    if (branch) {
        *branch = stashRestoreDefaultBranch(*stash);
        if (!inputText(this, Tr::tr("Restore Stash to Branch"), Tr::tr("Branch:"), branch)
            || branch->isEmpty())
            return false;
    } else {
        if (!modifiedPromptShown && !ask(Tr::tr("Stash Restore"), Tr::tr("Would you like to restore %1?").arg(stashIn)))
            return false;
    }
    return true;
}

static QString msgRestoreFailedTitle(const QString &stash)
{
    return Tr::tr("Error restoring %1").arg(stash);
}

void StashDialog::restoreCurrent()
{
    const int index = currentRow();
    QTC_ASSERT(index >= 0, return);
    QString errorMessage;
    QString name = m_model->at(index).name;
    // Make sure repository is not modified, restore. The command will
    // output to window on success.
    if (promptForRestore(&name, nullptr, &errorMessage)
            && gitClient().synchronousStashRestore(m_repository, name)) {
        refresh(m_repository, true); // Might have stashed away local changes.
    } else if (!errorMessage.isEmpty()) {
        warning(msgRestoreFailedTitle(name), errorMessage);
    }
}

void StashDialog::restoreCurrentInBranch()
{
    const int index = currentRow();
    QTC_ASSERT(index >= 0, return);
    QString errorMessage;
    QString branch;
    QString name = m_model->at(index).name;
    if (promptForRestore(&name, &branch, &errorMessage)
            && gitClient().synchronousStashRestore(m_repository, name, false, branch)) {
        refresh(m_repository, true); // git deletes the stash, unfortunately.
    } else if (!errorMessage.isEmpty()) {
        warning(msgRestoreFailedTitle(name), errorMessage);
    }
}

int StashDialog::currentRow() const
{
    return m_settings->stashes.currentRow();
}

QList<int> StashDialog::selectedRows() const
{
    return m_settings->stashes.selectedRows();
}

void StashDialog::forceRefresh()
{
    refresh(m_repository, true);
}

void StashDialog::enableButtons()
{
    const StashActions actions = actionsFor(!m_repository.isEmpty(), m_model->rowCount(),
                                            currentRow(), selectedRows().size());
    m_settings->deleteAll.setEnabled(actions.canDeleteAll);
    m_settings->deleteSelection.setEnabled(actions.canDeleteSelection);
    m_settings->show.setEnabled(actions.canShow);
    m_settings->restore.setEnabled(actions.canRestore);
    m_settings->restoreInBranch.setEnabled(actions.canRestoreInBranch);
    m_settings->refresh.setEnabled(actions.canRefresh);
}

void StashDialog::warning(const QString &title, const QString &what, const QString &details)
{
    QMessageBox msgBox(QMessageBox::Warning, title, what, QMessageBox::Ok, this);
    if (!details.isEmpty())
        msgBox.setDetailedText(details);
    msgBox.exec();
}

bool StashDialog::ask(const QString &title, const QString &what, bool defaultButton)
{
    return QMessageBox::question(
                this, title, what, QMessageBox::Yes | QMessageBox::No,
                defaultButton ? QMessageBox::Yes : QMessageBox::No) == QMessageBox::Yes;
}

#ifdef WITH_TESTS

class StashDialogTest final : public QObject
{
    Q_OBJECT

private slots:
    void testTheDialogDrawsWithTheQmlItNames()
    {
        StashModel model;
        StashSettings settings(&model);
        const Result<> rendered = Core::aspectFormRenders(&settings, "StashDialog.qml");
        QVERIFY2(rendered, qPrintable(rendered ? QString() : rendered.error()));
    }

    void testWhatCanBeDoneToAStash()
    {
        // Without a repository there is nothing to act on - except that
        // deleting a selection follows the selection alone, as the widget
        // button did. Unreachable rather than wrong: with no repository there
        // are no rows, so nothing can be selected.
        QCOMPARE(actionsFor(false, 0, -1, 0), StashActions{});
        QCOMPARE(actionsFor(false, 3, 0, 1),
                 (StashActions{false, true, false, false, false, false}));

        // A repository with no stashes: only refreshing, which is what would
        // find some.
        QCOMPARE(actionsFor(true, 0, -1, 0),
                 (StashActions{false, false, false, false, false, true}));

        // Stashes but none chosen: they can all be deleted, and nothing else.
        QCOMPARE(actionsFor(true, 3, -1, 0),
                 (StashActions{true, false, false, false, false, true}));

        // One chosen: showing and restoring act on it.
        QCOMPARE(actionsFor(true, 3, 1, 1),
                 (StashActions{true, true, true, true, true, true}));

        // Several picked but none current - deleting works on a group, the
        // rest do not.
        QCOMPARE(actionsFor(true, 3, -1, 2),
                 (StashActions{true, true, false, false, false, true}));
    }

    void testAStashIsNotEditedInTheCell()
    {
        StashModel model;
        model.setStashes({{"stash@{0}", "master", "work in progress"}});
        QCOMPARE(model.rowCount(), 1);

        // Answered, not merely falsy: an unanswered role reads as undefined in
        // QML and a table cell with no answer is taken to be editable.
        const QVariant editable = model.data(model.index(0, 0), AspectTable::EditableRole);
        QVERIFY2(editable.isValid(), "the table was never told whether a cell may be written to");
        QVERIFY2(!editable.toBool(), "a stash could be renamed by typing in the list");

        QVERIFY2(model.roleNames().values().contains("display"),
                 "the table cannot read what a cell says");
    }

    void testTheBranchAStashWouldBeRestoredTo()
    {
        // 'stash@{0}' becomes something that can be a branch name, with the
        // time appended so that two restores do not collide.
        const QString branch = stashRestoreDefaultBranch("stash@{0}");
        QVERIFY2(!branch.contains('{') && !branch.contains('}') && !branch.contains('@'),
                 qPrintable(branch));
        QVERIFY2(branch.startsWith("stash0-"), qPrintable(branch));
    }

    void testTheNextStashAlongTheStack()
    {
        // Deleting rotates the stack, so the code walks it by name.
        QCOMPARE(nextStash("stash@{0}"), QString("stash@{1}"));
        QCOMPARE(nextStash("stash@{9}"), QString("stash@{10}"));

        // Anything that is not a stash id has no next one.
        QVERIFY(nextStash("stash").isEmpty());
        QVERIFY(nextStash("stash@{x}").isEmpty());
    }

    void testTheDrawnTableHandsBackWhatWasChosen()
    {
        // Three lines in the .qml carry the dialog: the current row, the
        // selection, and the activation.
        StashModel model;
        model.setStashes({{"stash@{0}", "master", "one"}, {"stash@{1}", "master", "two"}});
        StashSettings settings(&model);

        const std::unique_ptr<QWidget> form(Core::createAspectForm(&settings));
        QVERIFY(form);
        QObject *const root = Core::aspectFormRoot(form.get());
        QVERIFY2(root, "no front end said what the dialog was drawn from");

        QObject *table = nullptr;
        QTRY_VERIFY(table = root->findChild<QObject *>("stashTable"));

        QSignalSpy activated(&settings.stashes, &StashTableAspect::rowActivated);
        QVERIFY(QMetaObject::invokeMethod(table, "rowActivated", Q_ARG(int, 1)));
        QTRY_COMPARE(activated.count(), 1);
        QCOMPARE(settings.stashes.currentRow(), 1);

        // Selecting rows in the table reaches the aspect, in the model's own
        // order whatever the view is showing.
        QVERIFY(QMetaObject::invokeMethod(table, "selectRow", Q_ARG(int, 0)));
        QTRY_COMPARE(settings.stashes.currentRow(), 0);
    }
};

QObject *createStashDialogTest()
{
    return new StashDialogTest;
}

#endif // WITH_TESTS

} // namespace Git::Internal

#include "stashdialog.moc"
