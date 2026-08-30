// Copyright (C) 2016 Nicolas Arnaud-Cormos
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "macromanager.h"

#include "actionmacrohandler.h"
#include "findmacrohandler.h"
#include "imacrohandler.h"
#include "macro.h"
#include "macroevent.h"
#include "macrosconstants.h"
#include "macrostr.h"
#include "texteditormacrohandler.h"

#include <coreplugin/actionmanager/actioncontainer.h>
#include <coreplugin/actionmanager/actionmanager.h>
#include <coreplugin/actionmanager/command.h>
#include <coreplugin/editormanager/editormanager.h>
#include <coreplugin/editormanager/ieditor.h>
#include <coreplugin/icontext.h>
#include <coreplugin/icore.h>
#include <coreplugin/dialogs/ioptionspage.h>

#include <utils/aspects.h>

#include <texteditor/texteditorconstants.h>

#include <utils/layoutbuilder.h>
#include <utils/qtcassert.h>

#include <QAction>
#ifdef WITH_TESTS
#include <QTest>
#endif

#include <QDialogButtonBox>
#include <QVBoxLayout>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QLineEdit>
#include <QList>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpressionValidator>

namespace Macros::Internal {

/*!
    \namespace Macros
    \brief The Macros namespace contains support for macros in \QC.
*/

/*!

    \class Macro::MacroManager
    \brief The MacroManager class implements a manager for macros.

    The MacroManager manages all macros, loads them on startup, keeps track of the
    current macro, and creates new macros.

    There are two important functions in this class that can be used outside the Macros plugin:
    \list
    \li registerEventHandler: add a new event handler
    \li registerAction: add a macro event when this action is triggered
    \endlist

    This class is a singleton and can be accessed using the instance function.
*/

/*!
    \fn void registerAction(QAction *action, const QString &id)

    Appends \a action to the list of actions registered in a macro. \a id is
    the action id passed to the ActionManager.
*/

class MacroManagerPrivate
{
public:
    MacroManagerPrivate(MacroManager *qq);

    MacroManager *q;
    QMap<QString, Macro *> macros;
    QMap<QString, QAction *> actions;
    Macro *currentMacro = nullptr;
    bool isRecording = false;

    QList<IMacroHandler*> handlers;

    ActionMacroHandler *actionHandler;
    TextEditorMacroHandler *textEditorHandler;
    FindMacroHandler *findHandler;

    void initialize();
    void addMacro(Macro *macro);
    void removeMacro(const QString &name);
    void changeMacroDescription(Macro *macro, const QString &description);

    bool executeMacro(Macro *macro);
    void showSaveDialog();
};

MacroManagerPrivate::MacroManagerPrivate(MacroManager *qq):
    q(qq)
{
    // Load existing macros
    initialize();

    actionHandler = new ActionMacroHandler;
    textEditorHandler = new TextEditorMacroHandler;
    findHandler = new FindMacroHandler;
}

void MacroManagerPrivate::initialize()
{
    macros.clear();
    const QDir dir(MacroManager::macrosDirectory());
    QStringList filter;
    filter << QLatin1String("*.") + QLatin1String(Constants::M_EXTENSION);
    const QStringList files = dir.entryList(filter, QDir::Files);

    for (const QString &name : files) {
        QString fileName = dir.absolutePath() + QLatin1Char('/') + name;
        auto macro = new Macro;
        if (macro->loadHeader(fileName))
            addMacro(macro);
        else
            delete macro;
    }
}

static Utils::Id makeId(const QString &name)
{
    return Utils::Id(Macros::Constants::PREFIX_MACRO).withSuffix(name);
}

void MacroManagerPrivate::addMacro(Macro *macro)
{
    // Add sortcut
    Core::Context context(TextEditor::Constants::C_TEXTEDITOR);
    auto action = new QAction(macro->description(), q);
    Core::Command *command = Core::ActionManager::registerAction(
                action, makeId(macro->displayName()), context);
    command->setAttribute(Core::Command::CA_UpdateText);
    QObject::connect(action, &QAction::triggered, q, [this, macro]() {
        q->executeMacro(macro->displayName());
    });

    // Add macro to the map
    macros[macro->displayName()] = macro;
    actions[macro->displayName()] = action;
}

void MacroManagerPrivate::removeMacro(const QString &name)
{
    if (!macros.contains(name))
        return;
    // Remove shortcut
    QAction *action = actions.take(name);
    Core::ActionManager::unregisterAction(action, makeId(name));
    delete action;

    // Remove macro from the map
    Macro *macro = macros.take(name);
    if (macro == currentMacro)
        currentMacro = nullptr;
    delete macro;
}

void MacroManagerPrivate::changeMacroDescription(Macro *macro, const QString &description)
{
    if (!macro->load())
        return;
    macro->setDescription(description);
    macro->save(macro->fileName());

    QAction *action = actions[macro->displayName()];
    QTC_ASSERT(action, return);
    action->setText(description);
}

bool MacroManagerPrivate::executeMacro(Macro *macro)
{
    bool error = !macro->load();
    const QList<MacroEvent> macroEvents = macro->events();
    for (const MacroEvent &macroEvent : macroEvents) {
        if (error)
            break;
        for (IMacroHandler *handler : std::as_const(handlers)) {
            if (handler->canExecuteEvent(macroEvent)) {
                if (!handler->executeEvent(macroEvent))
                    error = true;
                break;
            }
        }
    }

    if (error) {
        QMessageBox::warning(
            Core::ICore::dialogParent(),
            Tr::tr("Playing Macro"),
            Tr::tr("An error occurred while replaying the macro, execution stopped."));
    }

    // Set the focus back to the editor
    // TODO: is it really needed??
    if (Core::IEditor *current = Core::EditorManager::currentEditor())
        current->widget()->setFocus(Qt::OtherFocusReason);

    return !error;
}

// Whether a macro can be saved under this name. The widget dialog put a
// QRegularExpressionValidator on the field, which refuses the keystroke, and
// then asked hasAcceptableInput() whether to offer Save - so the rule was
// written twice and could only be asked by typing into a dialog.
bool isAcceptableMacroName(const QString &name)
{
    static const QRegularExpression word("\\A\\w+\\z");
    return word.match(name).hasMatch();
}

// Where a macro of that name is kept. The name is part of a file name, which
// is the reason the rule above is as narrow as it is.
Utils::FilePath macroFilePath(const QString &name)
{
    return Utils::FilePath::fromString(MacroManager::macrosDirectory())
           / (name + '.' + QLatin1String(Constants::M_EXTENSION));
}

class SaveMacroSettings final : public Utils::AspectContainer
{
public:
    SaveMacroSettings()
    {
        setAutoApply(true);
        setQmlSource(QUrl("qrc:/qt/qml/QtCreator/Macros/SaveMacroDialog.qml"));

        name.setQmlName("Name");
        name.setLabelText(Tr::tr("Name:"));
        name.setDisplayStyle(Utils::StringAspect::LineEditDisplay);
        // Said rather than refused: the widget field dropped the keystroke, so
        // a space simply did not appear and nothing explained why.
        name.setValidationFunction([](const QString &candidate) -> Utils::Result<> {
            if (candidate.isEmpty() || isAcceptableMacroName(candidate))
                return Utils::ResultOk;
            return Utils::ResultError(Tr::tr("A macro name can hold only letters, digits "
                                      "and underscores."));
        });

        description.setQmlName("Description");
        description.setLabelText(Tr::tr("Description:"));
        description.setDisplayStyle(Utils::StringAspect::LineEditDisplay);
    }

    Utils::StringAspect name{this};
    Utils::StringAspect description{this};
};

class SaveDialog : public QDialog
{
public:
    SaveDialog()
        : QDialog(Core::ICore::dialogParent())
        , m_buttonBox(new QDialogButtonBox(QDialogButtonBox::Cancel | QDialogButtonBox::Save))
    {
        resize(219, 91);
        setWindowTitle(Tr::tr("Save Macro"));

        const auto layout = new QVBoxLayout(this);
        layout->addWidget(Core::createAspectForm(&m_settings));
        layout->addWidget(m_buttonBox);

        connect(m_buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(m_buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

        const auto refreshSave = [this] {
            m_buttonBox->button(QDialogButtonBox::Save)
                ->setEnabled(isAcceptableMacroName(m_settings.name()));
        };
        m_settings.name.addOnChanged(this, refreshSave);
        refreshSave();
    }

    QString name() const { return m_settings.name(); }
    QString description() const { return m_settings.description(); }

private:
    SaveMacroSettings m_settings;
    QDialogButtonBox * const m_buttonBox;
};

void MacroManagerPrivate::showSaveDialog()
{
    SaveDialog dialog;
    if (dialog.exec()) {
        if (dialog.name().isEmpty())
            return;

        currentMacro->setDescription(dialog.description());
        currentMacro->save(macroFilePath(dialog.name()).toUrlishString());
        addMacro(currentMacro);
        emit q->macroAdded();
    }
}


// ---------- MacroManager ------------
MacroManager *m_instance = nullptr;

MacroManager::MacroManager() :
    d(new MacroManagerPrivate(this))
{
    m_instance = this;
    registerMacroHandler(d->actionHandler);
    registerMacroHandler(d->findHandler);
    registerMacroHandler(d->textEditorHandler);
}

MacroManager::~MacroManager()
{
    // Cleanup macro
    const QStringList macroList = d->macros.keys();
    for (const QString &name : macroList)
        d->removeMacro(name);

    // Cleanup handlers
    qDeleteAll(d->handlers);

    delete d;
}

void MacroManager::startMacro()
{
    d->isRecording = true;
    // Delete anonymous macro
    if (d->currentMacro && d->currentMacro->displayName().isEmpty())
        delete d->currentMacro;
    d->currentMacro = new Macro;

    Core::ActionManager::command(Constants::START_MACRO)->action()->setEnabled(false);
    Core::ActionManager::command(Constants::END_MACRO)->action()->setEnabled(true);
    Core::ActionManager::command(Constants::EXECUTE_LAST_MACRO)->action()->setEnabled(false);
    Core::ActionManager::command(Constants::SAVE_LAST_MACRO)->action()->setEnabled(false);
    for (IMacroHandler *handler : std::as_const(d->handlers))
        handler->startRecording(d->currentMacro);

    const QString endShortcut = Core::ActionManager::command(Constants::END_MACRO)
                                    ->keySequence()
                                    .toString(QKeySequence::NativeText);
    const QString executeShortcut = Core::ActionManager::command(Constants::EXECUTE_LAST_MACRO)
                                        ->keySequence()
                                        .toString(QKeySequence::NativeText);
    const QString help
        = Tr::tr("Macro mode. Type \"%1\" to stop recording and \"%2\" to play the macro.")
              .arg(endShortcut, executeShortcut);
    Core::EditorManager::showEditorStatusBar(Constants::M_STATUS_BUFFER, help,
                                             Tr::tr("Stop Recording Macro"),
                                             this, [this] { endMacro(); });
}

void MacroManager::endMacro()
{
    Core::EditorManager::hideEditorStatusBar(QLatin1String(Constants::M_STATUS_BUFFER));

    Core::ActionManager::command(Constants::START_MACRO)->action()->setEnabled(true);
    Core::ActionManager::command(Constants::END_MACRO)->action()->setEnabled(false);
    Core::ActionManager::command(Constants::EXECUTE_LAST_MACRO)->action()->setEnabled(true);
    Core::ActionManager::command(Constants::SAVE_LAST_MACRO)->action()->setEnabled(true);
    for (IMacroHandler *handler : std::as_const(d->handlers))
        handler->endRecordingMacro(d->currentMacro);

    d->isRecording = false;
}

void MacroManager::executeLastMacro()
{
    if (!d->currentMacro)
        return;

    // make sure the macro doesn't accidentally invoke a macro action
    Core::ActionManager::command(Constants::START_MACRO)->action()->setEnabled(false);
    Core::ActionManager::command(Constants::END_MACRO)->action()->setEnabled(false);
    Core::ActionManager::command(Constants::EXECUTE_LAST_MACRO)->action()->setEnabled(false);
    Core::ActionManager::command(Constants::SAVE_LAST_MACRO)->action()->setEnabled(false);

    d->executeMacro(d->currentMacro);

    Core::ActionManager::command(Constants::START_MACRO)->action()->setEnabled(true);
    Core::ActionManager::command(Constants::END_MACRO)->action()->setEnabled(false);
    Core::ActionManager::command(Constants::EXECUTE_LAST_MACRO)->action()->setEnabled(true);
    Core::ActionManager::command(Constants::SAVE_LAST_MACRO)->action()->setEnabled(true);
}

bool MacroManager::executeMacro(const QString &name)
{
    // Don't execute macro while recording
    if (d->isRecording || !d->macros.contains(name))
        return false;

    Macro *macro = d->macros.value(name);
    if (!d->executeMacro(macro))
        return false;

    // Delete anonymous macro
    if (d->currentMacro && d->currentMacro->displayName().isEmpty())
        delete d->currentMacro;
    d->currentMacro = macro;

    Core::ActionManager::command(Constants::SAVE_LAST_MACRO)->action()->setEnabled(true);

    return true;
}

void MacroManager::deleteMacro(const QString &name)
{
    Macro *macro = d->macros.value(name);
    if (macro) {
        QString fileName = macro->fileName();
        d->removeMacro(name);
        QFile::remove(fileName);
    }
}

const QMap<QString,Macro*> &MacroManager::macros()
{
    return m_instance->d->macros;
}

void MacroManager::registerMacroHandler(IMacroHandler *handler)
{
    m_instance->d->handlers.prepend(handler);
}

MacroManager *MacroManager::instance()
{
    return m_instance;
}

void MacroManager::changeMacro(const QString &name, const QString &description)
{
    if (!d->macros.contains(name))
        return;
    Macro *macro = d->macros.value(name);

    // Change description
    if (macro->description() != description)
        d->changeMacroDescription(macro, description);
}

void MacroManager::saveLastMacro()
{
    if (!d->currentMacro->events().isEmpty())
        d->showSaveDialog();
}

QString MacroManager::macrosDirectory()
{
    const QString path = Core::ICore::userResourcePath("macros").toUrlishString();
    if (QFileInfo::exists(path) || QDir().mkpath(path))
        return path;
    return QString();
}

#ifdef WITH_TESTS

class SaveMacroDialogTest final : public QObject
{
    Q_OBJECT

private slots:
    void testTheDialogDrawsWithTheQmlItNames()
    {
        SaveMacroSettings settings;
        const Utils::Result<> rendered
            = Core::aspectFormRenders(&settings, "SaveMacroDialog.qml");
        QVERIFY2(rendered, qPrintable(rendered ? QString() : rendered.error()));
    }

    void testWhatAMacroMayBeCalled()
    {
        QVERIFY(isAcceptableMacroName("build_and_run"));
        QVERIFY(isAcceptableMacroName("Macro2"));

        // The name becomes part of a file name, so the rule is narrow: no
        // spaces, no separators, and nothing at all is not a name.
        QVERIFY(!isAcceptableMacroName("my macro"));
        QVERIFY(!isAcceptableMacroName("../escape"));
        QVERIFY(!isAcceptableMacroName("with.dot"));
        QVERIFY(!isAcceptableMacroName({}));

        // Anchored at both ends: a name that merely *contains* something
        // acceptable is not acceptable. The widget validator was anchored by
        // being a validator; a plain match here would not be.
        QVERIFY2(!isAcceptableMacroName("ok name"), "a name with a space was accepted");
        QVERIFY2(!isAcceptableMacroName("/etc/passwd"), "a path was accepted as a name");
    }

    void testTheFieldSaysWhyRatherThanRefusingTheKeystroke()
    {
        // The widget field dropped the keystroke, so a space simply did not
        // appear and nothing said why. The aspect takes it and complains.
        SaveMacroSettings settings;

        QCOMPARE(settings.name.validationMessage("fine_name"), QString());

        const QString refused = settings.name.validationMessage("not fine");
        QVERIFY2(!refused.isEmpty(), "a name with a space was accepted without comment");

        // An empty field is not yet wrong - it is untouched. What is withheld
        // until there is a name is Save, not an answer.
        QCOMPARE(settings.name.validationMessage(QString()), QString());
        QVERIFY(!isAcceptableMacroName({}));
    }

    void testWhereAMacroIsKept()
    {
        const Utils::FilePath path = macroFilePath("Macro2");
        QCOMPARE(path.fileName(), QString("Macro2.") + Constants::M_EXTENSION);
        QCOMPARE(path.parentDir(),
                 Utils::FilePath::fromString(MacroManager::macrosDirectory()));
    }
};

QObject *createSaveMacroDialogTest()
{
    return new SaveMacroDialogTest;
}

#endif // WITH_TESTS

} // Macros::Internal

#ifdef WITH_TESTS
#include "macromanager.moc"
#endif
