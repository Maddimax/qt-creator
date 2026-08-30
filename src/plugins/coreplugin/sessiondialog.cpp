// Copyright (C) 2023 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "sessiondialog.h"

#include "dialogs/ioptionspage.h"
#include "icore.h"
#include "session.h"
#include "sessionmodel.h"

#include <utils/algorithm.h>
#include <utils/aspects.h>
#include <utils/layoutbuilder.h>

#ifdef WITH_TESTS
#include <QSignalSpy>
#include <QTest>
#endif

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QIdentityProxyModel>
#include <QInputDialog>
#include <QLineEdit>
#include <QPushButton>
#include <QValidator>
#include <QVBoxLayout>

#include <memory>

using namespace Utils;

namespace Core::Internal {

namespace PE {
struct Tr
{
    Q_DECLARE_TR_FUNCTIONS(QtC::ProjectExplorer)
};
} // namespace PE

class SessionValidator : public QValidator
{
public:
    SessionValidator(QObject *parent, const QStringList &sessions);
    void fixup(QString & input) const override;
    QValidator::State validate(QString & input, int & pos) const override;
private:
    bool hasSession(const QString &input) const;

    QStringList m_sessions;
};

SessionValidator::SessionValidator(QObject *parent, const QStringList &sessions)
    : QValidator(parent), m_sessions(sessions)
{
}

QValidator::State SessionValidator::validate(QString &input, int &pos) const
{
    Q_UNUSED(pos)

    if (input.contains(QLatin1Char('/'))
            || input.contains(QLatin1Char(':'))
            || input.contains(QLatin1Char('\\'))
            || input.contains(QLatin1Char('?'))
            || input.contains(QLatin1Char('*')))
        return QValidator::Invalid;

    if (hasSession(input))
        return QValidator::Intermediate;
    else
        return QValidator::Acceptable;
}

bool SessionValidator::hasSession(const QString &input) const
{
    return m_sessions.contains(input, Qt::CaseInsensitive);
}

void SessionValidator::fixup(QString &input) const
{
    int i = 2;
    QString copy;
    do {
        copy = input + QLatin1String(" (") + QString::number(i) + QLatin1Char(')');
        ++i;
    } while (hasSession(copy));
    input = copy;
}

SessionNameInputDialog::SessionNameInputDialog()
    : QDialog(ICore::dialogParent())
{
    m_newSessionLineEdit = new QLineEdit(this);
    m_newSessionLineEdit->setValidator(new SessionValidator(this, SessionManager::sessions()));

    auto buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, Qt::Horizontal, this);
    m_okButton = buttons->button(QDialogButtonBox::Ok);
    m_switchToButton = new QPushButton;
    m_switchToButton->setDefault(true);
    buttons->addButton(m_switchToButton, QDialogButtonBox::AcceptRole);
    connect(m_switchToButton, &QPushButton::clicked, this, [this] {
        m_usedSwitchTo = true;
    });

    // clang-format off
    using namespace Layouting;
    Column {
        PE::Tr::tr("Enter the name of the session:"),
        m_newSessionLineEdit,
        buttons,
    }.attachTo(this);
    // clang-format on

    connect(m_newSessionLineEdit, &QLineEdit::textChanged, this, [this](const QString &text) {
        m_okButton->setEnabled(!text.isEmpty());
        m_switchToButton->setEnabled(!text.isEmpty());
    });
    m_okButton->setEnabled(false);
    m_switchToButton->setEnabled(false);

    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void SessionNameInputDialog::setActionText(const QString &actionText, const QString &openActionText)
{
    m_okButton->setText(actionText);
    m_switchToButton->setText(openActionText);
}

void SessionNameInputDialog::setValue(const QString &value)
{
    m_newSessionLineEdit->setText(value);
}

QString SessionNameInputDialog::value() const
{
    return m_newSessionLineEdit->text();
}

bool SessionNameInputDialog::isSwitchToRequested() const
{
    return m_usedSwitchTo;
}

// What the buttons beside the list offer for a given selection. The default
// session cannot be renamed or deleted and the active one cannot be deleted,
// because both would leave the reader somewhere that no longer exists.
struct SessionActions
{
    bool canOpen = false;
    bool canRename = false;
    bool canClone = false;
    bool canDelete = false;

    bool operator==(const SessionActions &) const = default;
};

static SessionActions actionsFor(const QStringList &sessions, const QString &activeSession)
{
    if (sessions.isEmpty())
        return {};

    const bool defaultIsSelected = sessions.contains("default");
    const bool activeIsSelected = sessions.contains(activeSession);
    const bool one = sessions.size() == 1;
    return {one, one && !defaultIsSelected, one, !defaultIsSelected && !activeIsSelected};
}

// SessionModel names Qt::DisplayRole "sessionName", because the Welcome page's
// QML reads it that way. A table cell reads "display", and a role has exactly
// one name - so the dialog looks at the same rows through a proxy that names
// them the way a table expects, leaving the Welcome page's names alone.
class SessionTableModel : public QIdentityProxyModel
{
public:
    QVariant data(const QModelIndex &index, int role) const override
    {
        // The names are changed by their own dialog, not in the cell.
        if (role == AspectTable::EditableRole)
            return false;
        return QIdentityProxyModel::data(index, role);
    }

    QHash<int, QByteArray> roleNames() const override
    {
        QHash<int, QByteArray> names = sourceModel() ? sourceModel()->roleNames()
                                                     : QHash<int, QByteArray>();
        names = AspectTable::withRoleNames(names);
        names.insert(Qt::DisplayRole, "display");
        return names;
    }
};

class SessionSettings final : public AspectContainer
{
public:
    SessionSettings()
    {
        setAutoApply(true);
        setQmlSource(QUrl("qrc:/qt/qml/QtCreator/Core/SessionDialog.qml"));

        sessions.setQmlName("Sessions");
        rows.setSourceModel(&model);
        sessions.setModel(&rows);
        sessions.setSortColumn(0);

        createNew.setQmlName("CreateNew");
        createNew.setActionText(PE::Tr::tr("&New..."));
        open.setQmlName("Open");
        open.setActionText(PE::Tr::tr("&Open"));
        rename.setQmlName("Rename");
        rename.setActionText(PE::Tr::tr("&Rename..."));
        clone.setQmlName("Clone");
        clone.setActionText(PE::Tr::tr("C&lone..."));
        remove.setQmlName("Delete");
        remove.setActionText(PE::Tr::tr("&Delete..."));

        autoLoad.setQmlName("AutoLoad");
        autoLoad.setLabel(PE::Tr::tr("Restore last session on startup"),
                          BoolAspect::LabelPlacement::AtCheckBox);

        whatIsASession.setQmlName("WhatIsASession");
        whatIsASession.setTextFormat(AspectControls::TextFormat::RichText);
        whatIsASession.setText(
            QString("<a href=\"qthelp://org.qt-project.qtcreator/doc/"
                    "creator-project-managing-sessions.html\">%1</a>")
                .arg(PE::Tr::tr("What is a Session?")));
    }

    SessionModel model;
    SessionTableModel rows;
    TableAspect sessions{this};
    ActionAspect createNew{this};
    ActionAspect open{this};
    ActionAspect rename{this};
    ActionAspect clone{this};
    ActionAspect remove{this};
    BoolAspect autoLoad{this};
    TextDisplay whatIsASession{this};
};

SessionDialog::SessionDialog()
    : QDialog(ICore::dialogParent())
    , m_settings(new SessionSettings)
{
    setObjectName("ProjectExplorer.SessionDialog");
    resize(550, 400);
    setWindowTitle(PE::Tr::tr("Session Manager"));

    m_settings->createNew.setAction([this] { m_settings->model.newSession(); });
    m_settings->open.setAction([this] { switchToCurrentSession(); });
    m_settings->rename.setAction([this] {
        m_settings->model.renameSession(currentSession());
    });
    m_settings->clone.setAction([this] {
        m_settings->model.cloneSession(currentSession());
    });
    m_settings->remove.setAction([this] {
        m_settings->model.deleteSessions(selectedSessions());
    });

    // The widget label opened its link externally; the aspect says a link was
    // followed and leaves what that means to the dialog.
    connect(&m_settings->whatIsASession, &TextDisplay::linkActivated,
            this, [](const QString &link) { QDesktopServices::openUrl(QUrl(link)); });

    auto buttonBox = new QDialogButtonBox(this);
    buttonBox->setStandardButtons(QDialogButtonBox::Close);

    auto layout = new QVBoxLayout(this);
    layout->addWidget(Core::createAspectForm(m_settings.get()));
    layout->addWidget(buttonBox);

    connect(&m_settings->sessions, &TableAspect::chosenChanged,
            this, &SessionDialog::updateActions);
    connect(&m_settings->sessions, &TableAspect::rowActivated,
            this, &SessionDialog::switchToCurrentSession);
    connect(&m_settings->model, &SessionModel::sessionSwitched, this, &QDialog::reject);
    connect(&m_settings->model, &SessionModel::sessionCreated,
            this, &SessionDialog::selectSession);
    connect(&m_settings->model, &SessionModel::modelReset, this, [this] {
        selectSession(SessionManager::activeSession());
    });

    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);

    selectSession(SessionManager::activeSession());
    updateActions();
}

SessionDialog::~SessionDialog() = default;

QString SessionDialog::currentSession() const
{
    return m_settings->model.sessionAt(m_settings->sessions.currentRow());
}

QStringList SessionDialog::selectedSessions() const
{
    return Utils::transform(m_settings->sessions.selectedRows(),
                            [this](int row) { return m_settings->model.sessionAt(row); });
}

void SessionDialog::switchToCurrentSession()
{
    m_settings->model.switchToSession(currentSession());
}

void SessionDialog::selectSession(const QString &sessionName)
{
    m_settings->sessions.showRow(m_settings->model.indexOfSession(sessionName));
}

void SessionDialog::setAutoLoadSession(bool check)
{
    m_settings->autoLoad.setValue(check);
}

bool SessionDialog::autoLoadSession() const
{
    return m_settings->autoLoad();
}

void SessionDialog::updateActions()
{
    const SessionActions actions
        = actionsFor(selectedSessions(), SessionManager::activeSession());
    m_settings->open.setEnabled(actions.canOpen);
    m_settings->rename.setEnabled(actions.canRename);
    m_settings->clone.setEnabled(actions.canClone);
    m_settings->remove.setEnabled(actions.canDelete);
}

#ifdef WITH_TESTS

class SessionDialogTest final : public QObject
{
    Q_OBJECT

private slots:
    void testTheDialogDrawsWithTheQmlItNames()
    {
        SessionSettings settings;
        const Result<> rendered = Core::aspectFormRenders(&settings, "SessionDialog.qml");
        QVERIFY2(rendered, qPrintable(rendered ? QString() : rendered.error()));
    }

    void testWhatCanBeDoneToASelection()
    {
        const QString active = "current";

        // Nothing chosen, nothing to do.
        QCOMPARE(actionsFor({}, active), SessionActions{});

        // One ordinary session: all four.
        QCOMPARE(actionsFor({"other"}, active),
                 (SessionActions{true, true, true, true}));

        // The default session is the one that must always be there, so it can
        // be opened and cloned but not renamed or deleted.
        QCOMPARE(actionsFor({"default"}, active),
                 (SessionActions{true, false, true, false}));

        // Deleting the session that is loaded would leave the reader nowhere.
        QCOMPARE(actionsFor({active}, active),
                 (SessionActions{true, true, true, false}));

        // More than one: only deleting makes sense for a group.
        QCOMPARE(actionsFor({"one", "two"}, active),
                 (SessionActions{false, false, false, true}));

        // And a group holding the default or the active one cannot be deleted
        // either.
        QCOMPARE(actionsFor({"one", "default"}, active),
                 (SessionActions{false, false, false, false}));
        QCOMPARE(actionsFor({"one", active}, active),
                 (SessionActions{false, false, false, false}));
    }

    void testTheTableSeesTheRowsUnderTheNameItExpects()
    {
        // SessionModel calls Qt::DisplayRole "sessionName" for the Welcome
        // page, and a role has one name. A table cell reads "display", so
        // without the proxy every cell in this dialog would be empty.
        SessionSettings settings;

        const QHash<int, QByteArray> source = settings.model.roleNames();
        QCOMPARE(source.value(Qt::DisplayRole), QByteArray("sessionName"));

        const QHash<int, QByteArray> shown = settings.sessions.tableModel()->roleNames();
        QCOMPARE(shown.value(Qt::DisplayRole), QByteArray("display"));

        // The names the Welcome page reads are still there beside it.
        QVERIFY2(shown.values().contains("activeSession"),
                 "the proxy dropped the roles the model added");
        QVERIFY2(shown.contains(AspectTable::EditableRole),
                 "the table cannot ask whether a cell may be written to");
    }

    void testASessionIsNotRenamedInTheCell()
    {
        // Renaming has its own dialog, because the name has to be checked
        // against the ones that exist.
        SessionSettings settings;
        QAbstractItemModel *const model = settings.sessions.tableModel();
        QVERIFY(model->rowCount() > 0);

        // Answered, not merely falsy: a role the model says nothing about
        // reads as undefined in QML, and a table cell with no answer is taken
        // to be the reader's to edit.
        const QVariant editable = model->data(model->index(0, 0), AspectTable::EditableRole);
        QVERIFY2(editable.isValid(), "the table was never told whether a cell may be written to");
        QVERIFY2(!editable.toBool(), "a session could be renamed by typing in the list");
    }

    void testWhatTheReaderHasChosen()
    {
        SessionSettings settings;
        QSignalSpy chosen(&settings.sessions, &TableAspect::chosenChanged);

        settings.sessions.setSelectedRows({0, 2});
        QCOMPARE(settings.sessions.selectedRows(), (QList<int>{0, 2}));
        QCOMPARE(chosen.count(), 1);

        // Saying the same thing again is not a change.
        settings.sessions.setSelectedRows({0, 2});
        QCOMPARE(chosen.count(), 1);
    }

    void testTheDrawnTableHandsBackWhatWasChosen()
    {
        // Three lines in the .qml carry the whole dialog: the current row, the
        // selection, and the activation. A C++ test calling them itself would
        // not notice if any of them went.
        SessionSettings settings;

        const std::unique_ptr<QWidget> form(Core::createAspectForm(&settings));
        QVERIFY(form);
        QObject *const root = Core::aspectFormRoot(form.get());
        QVERIFY2(root, "no front end said what the dialog was drawn from");

        QObject *table = nullptr;
        QTRY_VERIFY(table = root->findChild<QObject *>("sessionTable"));

        QSignalSpy activated(&settings.sessions, &TableAspect::rowActivated);
        QVERIFY(QMetaObject::invokeMethod(table, "rowActivated", Q_ARG(int, 0)));
        QTRY_COMPARE(activated.count(), 1);
        QCOMPARE(settings.sessions.currentRow(), 0);

        // And the dialog can put the reader on a row, which the view follows.
        settings.sessions.showRow(0);
        QTRY_COMPARE(table->property("currentRow").toInt(), 0);
    }
};

QObject *createSessionDialogTest()
{
    return new SessionDialogTest;
}

#endif // WITH_TESTS

} // namespace Core::Internal

#include "sessiondialog.moc"
