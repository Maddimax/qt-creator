// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "remotedialog.h"

#include "gitclient.h"
#include "gitplugin.h"
#include "gittr.h"
#include "remotemodel.h"

#include <coreplugin/dialogs/ioptionspage.h>

#include <utils/aspects.h>

#ifdef WITH_TESTS
#include <QTest>
#endif

#include <vcsbase/vcsoutputwindow.h>

#include <QDialogButtonBox>
#include <QPushButton>
#include <QMessageBox>
#include <QRegularExpression>
#include <QVBoxLayout>

#include <memory>

using namespace Utils;

namespace Git::Internal {

// --------------------------------------------------------------------------
// RemoteAdditionDialog:
// --------------------------------------------------------------------------

// What is wrong with \a name as a remote name, or nothing when it is a good
// one. Git refuses these itself; saying so here means the reader is told
// before the command runs.
QString remoteNameIssue(const QString &name, const QStringList &existing)
{
    // Empty is not an error to complain about - there is nothing typed yet -
    // but it is not a name either, so Ok stays off.
    if (name.isEmpty())
        return QString();
    if (name.endsWith(".lock"))
        return QString();
    if (name.endsWith('.'))
        return QString();
    if (name.endsWith('/'))
        return QString();
    if (existing.contains(name))
        return Tr::tr("A remote with the name \"%1\" already exists.").arg(name);
    return QString();
}

// Whether \a name is one git will take. The messages above say *why* for the
// cases worth explaining; this is what the Ok button follows.
bool isUsableRemoteName(const QString &name, const QStringList &existing)
{
    return !name.isEmpty() && !name.endsWith(".lock") && !name.endsWith('.')
           && !name.endsWith('/') && !existing.contains(name);
}

// What a typed remote name becomes: git takes none of these characters, so
// they are turned into underscores as the reader types.
QString sanitizedRemoteName(const QString &typed)
{
    // A pattern, not a literal: the function hands back the source of a
    // regular expression.
    static const QRegularExpression invalid(invalidBranchAndRemoteNamePattern());
    QString name = typed;
    return name.replace(invalid, "_");
}

QString remoteUrlIssue(const QString &url)
{
    if (url.isEmpty())
        return QString();
    return GitRemote(url).isValid ? QString() : Tr::tr("The URL may not be valid.");
}

class RemoteAdditionSettings final : public AspectContainer
{
public:
    explicit RemoteAdditionSettings(const QStringList &remoteNames)
        : m_remoteNames(remoteNames)
    {
        setAutoApply(true);
        setQmlSource(QUrl("qrc:/qt/qml/QtCreator/Git/RemoteAdditionDialog.qml"));

        name.setQmlName("Name");
        name.setLabelText(Tr::tr("Name:"));
        name.setDisplayStyle(StringAspect::LineEditDisplay);
        name.setHistoryCompleter("Git.RemoteNames");
        name.setValidationFunction([this](const QString &text) -> Result<> {
            const QString issue = remoteNameIssue(text, m_remoteNames);
            if (!issue.isEmpty())
                return ResultError(issue);
            // Silently not yet a name: nothing to say, but not acceptable.
            return isUsableRemoteName(text, m_remoteNames) ? Result<>(ResultOk)
                                                           : ResultError(QString());
        });

        url.setQmlName("Url");
        url.setLabelText(Tr::tr("URL:"));
        url.setDisplayStyle(StringAspect::LineEditDisplay);
        url.setHistoryCompleter("Git.RemoteUrls");
        url.setValidationFunction([](const QString &text) -> Result<> {
            const QString issue = remoteUrlIssue(text);
            if (!issue.isEmpty())
                return ResultError(issue);
            return text.isEmpty() ? ResultError(QString()) : Result<>(ResultOk);
        });
    }

    StringAspect name{this};
    StringAspect url{this};

private:
    const QStringList m_remoteNames;
};

class RemoteAdditionDialog : public QDialog
{
public:
    explicit RemoteAdditionDialog(const QStringList &remoteNames)
        : m_settings(remoteNames)
    {
        resize(381, 93);

        auto buttonBox = new QDialogButtonBox(QDialogButtonBox::Cancel | QDialogButtonBox::Ok);
        QPushButton *const okButton = buttonBox->button(QDialogButtonBox::Ok);
        okButton->setEnabled(false);

        auto layout = new QVBoxLayout(this);
        layout->addWidget(Core::createAspectForm(&m_settings));
        layout->addWidget(buttonBox);

        connect(&m_settings.name, &BaseAspect::changed, this, [this, okButton, remoteNames] {
            // The characters git will not take become underscores as they are
            // typed, which the widget line edit did from inside its validator.
            const QString cleaned = sanitizedRemoteName(m_settings.name());
            if (cleaned != m_settings.name())
                m_settings.name.setValue(cleaned);
            okButton->setEnabled(isUsableRemoteName(cleaned, remoteNames));
        });

        connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
        connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    }

    QString remoteName() const { return m_settings.name(); }
    QString remoteUrl() const { return m_settings.url(); }

private:
    RemoteAdditionSettings m_settings;
};

// --------------------------------------------------------------------------
// RemoteDialog:
// --------------------------------------------------------------------------


class RemoteDialogSettings final : public AspectContainer
{
public:
    explicit RemoteDialogSettings(QAbstractItemModel *model)
    {
        setAutoApply(true);
        setQmlSource(QUrl("qrc:/qt/qml/QtCreator/Git/RemoteDialog.qml"));

        repository.setQmlName("Repository");

        refresh.setQmlName("Refresh");
        refresh.setActionText(Tr::tr("Re&fresh"));

        remotes.setQmlName("Remotes");
        remotes.setLabelText(Tr::tr("Remotes"));
        remotes.setModel(model);

        add.setQmlName("Add");
        add.setActionText(Tr::tr("&Add..."));
        fetch.setQmlName("Fetch");
        fetch.setActionText(Tr::tr("F&etch"));
        push.setQmlName("Push");
        push.setActionText(Tr::tr("&Push"));
        remove.setQmlName("Remove");
        remove.setActionText(Tr::tr("&Remove"));
    }

    // Everything but adding acts on the remote that is picked.
    void setHaveSelection(bool haveSelection)
    {
        fetch.setEnabled(haveSelection);
        push.setEnabled(haveSelection);
        remove.setEnabled(haveSelection);
    }

    TextDisplay repository{this};
    ActionAspect refresh{this};
    TableAspect remotes{this};
    ActionAspect add{this};
    ActionAspect fetch{this};
    ActionAspect push{this};
    ActionAspect remove{this};
};

RemoteDialog::RemoteDialog(QWidget *parent)
    : QDialog(parent)
    , m_remoteModel(new RemoteModel(this))
    , m_settings(new RemoteDialogSettings(m_remoteModel))
{
    setModal(false);
    setAttribute(Qt::WA_DeleteOnClose, true); // Do not update unnecessarily
    setWindowTitle(Tr::tr("Remotes"));

    m_settings->add.setAction([this] { addRemote(); });
    m_settings->fetch.setAction([this] { fetchFromRemote(); });
    m_settings->push.setAction([this] { pushToRemote(); });
    m_settings->remove.setAction([this] { removeRemote(); });
    m_settings->refresh.setAction([this] { refreshRemotes(); });

    auto buttonBox = new QDialogButtonBox(QDialogButtonBox::Close);

    auto layout = new QVBoxLayout(this);
    layout->addWidget(Core::createAspectForm(m_settings.get()));
    layout->addWidget(buttonBox);

    connect(&m_settings->remotes, &TableAspect::chosenChanged,
            this, &RemoteDialog::updateButtonState);
    connect(m_remoteModel, &RemoteModel::refreshed,
            this, &RemoteDialog::updateButtonState);

    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    updateButtonState();
}

RemoteDialog::~RemoteDialog() = default;

void RemoteDialog::refresh(const FilePath &repository, bool force)
{
    if (m_remoteModel->workingDirectory() == repository && !force)
        return;
    // Refresh
    m_settings->repository.setText(msgRepositoryLabel(repository));
    if (repository.isEmpty()) {
        m_remoteModel->clear();
    } else {
        QString errorMessage;
        if (!m_remoteModel->refresh(repository, &errorMessage))
            VcsBase::VcsOutputWindow::appendError(repository, errorMessage);
    }
}

void RemoteDialog::refreshRemotes()
{
    refresh(m_remoteModel->workingDirectory(), true);
}

void RemoteDialog::addRemote()
{
    RemoteAdditionDialog addDialog(m_remoteModel->allRemoteNames());
    if (addDialog.exec() != QDialog::Accepted)
        return;

    m_remoteModel->addRemote(addDialog.remoteName(), addDialog.remoteUrl());
}

void RemoteDialog::removeRemote()
{
    const int row = m_settings->remotes.currentRow();
    if (row < 0)
        return;

    const QString remoteName = m_remoteModel->remoteName(row);
    if (QMessageBox::question(this, Tr::tr("Delete Remote"),
                              Tr::tr("Would you like to delete the remote \"%1\"?").arg(remoteName),
                              QMessageBox::Yes | QMessageBox::No,
                              QMessageBox::Yes) == QMessageBox::Yes) {
        m_remoteModel->removeRemote(row);
    }
}

void RemoteDialog::pushToRemote()
{
    const int row = m_settings->remotes.currentRow();
    if (row < 0)
        return;

    const QString remoteName = m_remoteModel->remoteName(row);
    gitClient().push(m_remoteModel->workingDirectory(), {remoteName});
}

void RemoteDialog::fetchFromRemote()
{
    const int row = m_settings->remotes.currentRow();
    if (row < 0)
        return;

    const QString remoteName = m_remoteModel->remoteName(row);
    gitClient().fetch(m_remoteModel->workingDirectory(), remoteName);
}

void RemoteDialog::updateButtonState()
{
    m_settings->setHaveSelection(m_settings->remotes.hasSelection());
}

#ifdef WITH_TESTS

class RemoteDialogTest final : public QObject
{
    Q_OBJECT

private slots:
    void testBothDialogsDrawWithTheQmlTheyName()
    {
        RemoteAdditionSettings addition({});
        const Result<> additionRendered
            = Core::aspectFormRenders(&addition, "RemoteAdditionDialog.qml");
        QVERIFY2(additionRendered,
                 qPrintable(additionRendered ? QString() : additionRendered.error()));

        RemoteModel model;
        RemoteDialogSettings settings(&model);
        const Result<> rendered = Core::aspectFormRenders(&settings, "RemoteDialog.qml");
        QVERIFY2(rendered, qPrintable(rendered ? QString() : rendered.error()));
    }

    void testWhichNamesGitWouldTake()
    {
        const QStringList existing{"origin", "upstream"};

        QVERIFY(isUsableRemoteName("fork", existing));

        // Nothing typed yet, and the three shapes git refuses at the end of a
        // name. None of them is worth a message - the reader is mid-word.
        QVERIFY(!isUsableRemoteName("", existing));
        QVERIFY(!isUsableRemoteName("fork.lock", existing));
        QVERIFY(!isUsableRemoteName("fork.", existing));
        QVERIFY(!isUsableRemoteName("fork/", existing));
        QVERIFY(remoteNameIssue("", existing).isEmpty());
        QVERIFY(remoteNameIssue("fork.lock", existing).isEmpty());

        // A name that is already taken is worth saying out loud, because
        // nothing the reader types next will fix it by accident.
        QVERIFY(!isUsableRemoteName("origin", existing));
        QVERIFY2(remoteNameIssue("origin", existing).contains("origin"),
                 "the reader is not told which name is taken");

        // A dot or a slash inside a name is fine - only the end matters.
        QVERIFY(isUsableRemoteName("my.fork", existing));
        QVERIFY(isUsableRemoteName("team/fork", existing));
    }

    void testWhatATypedNameBecomes()
    {
        // The characters git will not take become underscores as they are
        // typed. A good name is left alone.
        QCOMPARE(sanitizedRemoteName("fork"), QString("fork"));
        QCOMPARE(sanitizedRemoteName("my fork"), QString("my_fork"));
        QCOMPARE(sanitizedRemoteName("fork^2"), QString("fork_2"));
    }

    void testWhatIsSaidAboutAUrl()
    {
        QVERIFY2(remoteUrlIssue("").isEmpty(), "an empty field is complained about");
        QVERIFY(remoteUrlIssue("git@codereview.qt-project.org:qt-creator/qt-creator").isEmpty());

        // A local remote is refused when it is not there. Anything that parses
        // as host:path is taken on trust, because only git can say.
        QVERIFY2(!remoteUrlIssue("/no/such/repository/here").isEmpty(),
                 "a local path that does not exist is offered as a remote");
    }

    void testARemoteIsRenamedInTheList()
    {
        RemoteModel model;
        model.setRemotes({{"origin", "git://example.org/repo"}});
        QCOMPARE(model.rowCount(), 1);

        // Answered, not merely truthy: an unanswered role reads as undefined
        // in QML, which a cell also takes for editable.
        const QVariant editable = model.data(model.index(0, 0), AspectTable::EditableRole);
        QVERIFY2(editable.isValid(), "the table was never told whether a cell may be written to");
        QVERIFY2(editable.toBool(), "a remote can no longer be renamed in the list");

        QVERIFY2(model.roleNames().values().contains("display"),
                 "the table cannot read what a cell says");
    }

    void testWhatCanBeDoneToARemote()
    {
        RemoteModel model;
        model.setRemotes({{"origin", "git://example.org/repo"}});
        RemoteDialogSettings settings(&model);

        // Adding needs nothing chosen; the other three act on what is.
        settings.setHaveSelection(false);
        QVERIFY(settings.add.isEnabled());
        QVERIFY(!settings.fetch.isEnabled());
        QVERIFY(!settings.push.isEnabled());
        QVERIFY(!settings.remove.isEnabled());

        settings.setHaveSelection(true);
        QVERIFY(settings.add.isEnabled());
        QVERIFY(settings.fetch.isEnabled());
        QVERIFY(settings.push.isEnabled());
        QVERIFY(settings.remove.isEnabled());
    }

    void testTheDrawnTableHandsBackWhatWasChosen()
    {
        RemoteModel model;
        model.setRemotes({{"origin", "git://example.org/repo"},
                          {"upstream", "git://example.org/upstream"}});
        RemoteDialogSettings settings(&model);

        const std::unique_ptr<QWidget> form(Core::createAspectForm(&settings));
        QVERIFY(form);
        QObject *const root = Core::aspectFormRoot(form.get());
        QVERIFY2(root, "no front end said what the dialog was drawn from");

        QObject *table = nullptr;
        QTRY_VERIFY(table = root->findChild<QObject *>("remoteTable"));

        QVERIFY(QMetaObject::invokeMethod(table, "selectRow", Q_ARG(int, 1)));
        QTRY_COMPARE(settings.remotes.currentRow(), 1);
        QCOMPARE(model.remoteName(settings.remotes.currentRow()), QString("upstream"));
    }
};

QObject *createRemoteDialogTest()
{
    return new RemoteDialogTest;
}

#endif // WITH_TESTS

} // Git::Internal

#include "remotedialog.moc"
