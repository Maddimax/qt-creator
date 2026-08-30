// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "startremotedialog.h"

#include "valgrindtr.h"

#include <coreplugin/coreconstants.h>
#include <coreplugin/dialogs/ioptionspage.h>
#include <coreplugin/icore.h>
#include <coreplugin/perspective.h>

#include <projectexplorer/devicesupport/devicekitaspects.h>
#include <projectexplorer/devicesupport/idevice.h>
#include <projectexplorer/devicesupport/sshparameters.h>
#include <projectexplorer/kitchooser.h>
#include <projectexplorer/runconfiguration.h>
#include <projectexplorer/runcontrol.h>
#include <projectexplorer/taskhub.h>

#include <utils/aspects.h>
#include <utils/filepath.h>
#include <utils/pathvalidation.h>
#include <utils/qtcsettings.h>

#ifdef WITH_TESTS
#include <QTemporaryDir>
#include <QTest>
#endif

#include <QDialog>
#include <QDialogButtonBox>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

#include <memory>

using namespace Core;
using namespace ProjectExplorer;
using namespace Utils;

namespace Valgrind::Internal {

// Which kits this dialog can analyse with: the analysis is run on the other
// machine over ssh, so a kit with nowhere to reach is no use here.
static bool kitCanRunRemoteAnalysis(const Kit *kit)
{
    const IDevice::ConstPtr device = RunDeviceKitAspect::device(kit);
    return kit->isValid() && device && !device->sshParameters().host().isEmpty();
}

const char settingsGroup[] = "AnalyzerStartRemoteDialog";

class StartRemoteSettings final : public AspectContainer
{
public:
    StartRemoteSettings()
    {
        setAutoApply(true);
        setQmlSource(QUrl("qrc:/qt/qml/QtCreator/Valgrind/StartRemoteDialog.qml"));

        kitChooser.setQmlName("Kit");
        kitChooser.setKitPredicate(&kitCanRunRemoteAnalysis);

        executable.setQmlName("Executable");
        executable.setLabelText(Tr::tr("Executable:"));
        executable.setExpectedKind(PathChooserKind::ExistingCommand);

        arguments.setQmlName("Arguments");
        arguments.setLabelText(Tr::tr("Arguments:"));
        arguments.setDisplayStyle(StringAspect::LineEditDisplay);

        workingDirectory.setQmlName("WorkingDirectory");
        workingDirectory.setLabelText(Tr::tr("Working directory:"));
        workingDirectory.setExpectedKind(PathChooserKind::ExistingDirectory);
    }

    KitChooserAspect kitChooser{this};
    FilePathAspect executable{this};
    StringAspect arguments{this};
    FilePathAspect workingDirectory{this};
};

// Kept as the widget wrote it rather than given to the aspects: these four
// keys are in every reader's settings file already, and a SelectionAspect
// would store the chooser's row where "profile" holds a kit id.
static void restoreFrom(QtcSettings *s, StartRemoteSettings *settings)
{
    s->beginGroup(settingsGroup);
    settings->kitChooser.setCurrentKitId(Id::fromSetting(s->value("profile")));
    settings->executable.setValue(FilePath::fromString(s->value("executable").toString()));
    settings->workingDirectory.setValue(
        FilePath::fromString(s->value("workingDirectory").toString()));
    settings->arguments.setValue(s->value("arguments").toString());
    s->endGroup();
}

static void saveTo(QtcSettings *s, const StartRemoteSettings *settings)
{
    s->beginGroup(settingsGroup);
    s->setValue("profile", settings->kitChooser.currentKitId().toString());
    s->setValue("executable", settings->executable().toFSPathString());
    s->setValue("workingDirectory", settings->workingDirectory().toFSPathString());
    s->setValue("arguments", settings->arguments());
    s->endGroup();
}

class StartRemoteDialog : public QDialog
{
public:
    StartRemoteDialog();
    ~StartRemoteDialog() override;

    CommandLine commandLine() const;
    FilePath workingDirectory() const;

private:
    void validate();
    void accept() override;

    const std::unique_ptr<StartRemoteSettings> m_settings;
    QDialogButtonBox *m_buttonBox;

#ifdef WITH_TESTS
    friend class StartRemoteDialogTest;
#endif
};

StartRemoteDialog::StartRemoteDialog()
    : QDialog(Core::ICore::dialogParent())
    , m_settings(new StartRemoteSettings)
{
    setWindowTitle(Tr::tr("Start Remote Analysis"));

    m_buttonBox = new QDialogButtonBox(QDialogButtonBox::Cancel | QDialogButtonBox::Ok, this);

    auto verticalLayout = new QVBoxLayout(this);
    verticalLayout->addWidget(Core::createAspectForm(m_settings.get()));
    verticalLayout->addWidget(m_buttonBox);

    m_settings->kitChooser.populate();
    restoreFrom(Core::ICore::settings(), m_settings.get());

    connect(&m_settings->executable, &FilePathAspect::validChanged,
            this, &StartRemoteDialog::validate);
    connect(m_buttonBox, &QDialogButtonBox::accepted, this, &StartRemoteDialog::accept);
    connect(m_buttonBox, &QDialogButtonBox::rejected, this, &StartRemoteDialog::reject);

    validate();
}

StartRemoteDialog::~StartRemoteDialog() = default;

void StartRemoteDialog::accept()
{
    saveTo(Core::ICore::settings(), m_settings.get());
    m_settings->kitChooser.rememberChoice();

    QDialog::accept();
}

void StartRemoteDialog::validate()
{
    m_buttonBox->button(QDialogButtonBox::Ok)->setEnabled(m_settings->executable.isValid());
}

CommandLine StartRemoteDialog::commandLine() const
{
    const Kit *kit = m_settings->kitChooser.currentKit();
    const QString executable = m_settings->executable().toFSPathString();
    const FilePath filePath = RunDeviceKitAspect::deviceFilePath(kit, executable);
    return {filePath, m_settings->arguments(), CommandLine::Raw};
}

FilePath StartRemoteDialog::workingDirectory() const
{
    return m_settings->workingDirectory();
}

void setupExternalAnalyzer(QAction *action, Perspective *perspective, Id runMode)
{
    QObject::connect(action, &QAction::triggered, perspective, [action, perspective, runMode] {
        RunConfiguration *runConfig = activeRunConfigForActiveProject();
        if (!runConfig) {
            auto errorDialog = new QMessageBox(Core::ICore::dialogParent());
            errorDialog->setAttribute(Qt::WA_DeleteOnClose);
            errorDialog->setIcon(QMessageBox::Warning);
            errorDialog->setWindowTitle(action->text());
            errorDialog->setText(Tr::tr("Cannot start %1 without a project. Open the "
                                        "project and try again.").arg(action->text()));
            errorDialog->setStandardButtons(QMessageBox::Ok);
            errorDialog->setDefaultButton(QMessageBox::Ok);
            errorDialog->show();
            return;
        }
        StartRemoteDialog dlg;
        if (dlg.exec() != QDialog::Accepted)
            return;

        TaskHub::clearTasks(Core::Constants::ANALYZERTASK_ID);
        perspective->select();
        RunControl *runControl = new RunControl(runMode);
        runControl->copyDataFromRunConfiguration(runConfig);
        runControl->createMainRecipe();
        runControl->setCommandLine(dlg.commandLine());
        runControl->setWorkingDirectory(dlg.workingDirectory());
        runControl->start();
    });
}

#ifdef WITH_TESTS

class StartRemoteDialogTest final : public QObject
{
    Q_OBJECT

private slots:
    void testTheDialogDrawsWithTheQmlItNames()
    {
        StartRemoteSettings settings;
        const Result<> rendered = Core::aspectFormRenders(&settings, "StartRemoteDialog.qml");
        QVERIFY2(rendered, qPrintable(rendered ? QString() : rendered.error()));
    }

    void testWhichKitsCanBeAnalysedRemotely()
    {
        // The rule is about reaching another machine, so check the two halves
        // separately: a kit that is refused for being invalid would say
        // nothing about the ssh host.
        Kit local(Id::fromName("kit.local"));
        const IDevice::ConstPtr device = RunDeviceKitAspect::device(&local);
        QVERIFY2(device, "the kit under test resolved no device at all");
        QVERIFY2(device->sshParameters().host().isEmpty(),
                 "the default device has an ssh host, so this case proves nothing");
        QVERIFY2(!kitCanRunRemoteAnalysis(&local),
                 "a kit with nowhere to reach was offered for remote analysis");

        // And the chooser is given that rule rather than keeping its own.
        StartRemoteSettings settings;
        settings.kitChooser.populate();
        for (int i = 0; i < settings.kitChooser.kit.optionCount(); ++i) {
            settings.kitChooser.kit.setValue(i);
            Kit *const kit = settings.kitChooser.currentKit();
            QVERIFY2(kit && kitCanRunRemoteAnalysis(kit),
                     "a kit the dialog cannot analyse with was offered");
        }
    }

    void testWhatTheFieldsAccept()
    {
        StartRemoteSettings settings;

        // The executable has to be there to be run, and the working directory
        // has to be a directory: this is what the two path choosers asked for.
        QCOMPARE(settings.executable.presentation().pathKind,
                 AspectControls::PathKind::ExistingCommand);
        QCOMPARE(settings.workingDirectory.presentation().pathKind,
                 AspectControls::PathKind::ExistingDirectory);

        // Arguments are a line to type in, not the label a string aspect
        // draws by default.
        QCOMPARE(settings.arguments.presentation().control, AspectControls::LineEdit);
    }

    void testWhenTheAnalysisCanBeStarted()
    {
        StartRemoteSettings settings;

        // What the Ok button follows. The answer is fetched, so asking is what
        // makes the flag true - which is why the dialog follows validChanged
        // rather than reading isValid() once in its constructor.
        const FilePath self = FilePath::fromString(QCoreApplication::applicationFilePath());
        QTRY_VERIFY2(settings.executable.validationMessage(self.toFSPathString()).isEmpty()
                         && settings.executable.isValid(),
                     "an executable that is there did not make the analysis startable");

        // And an executable that is not there does not. Asked in this order
        // because an empty field is the candidate the aspect starts on, so
        // asking about it first is answered from the cache without looking.
        QTRY_VERIFY2(!settings.executable.validationMessage(QString()).isEmpty(),
                     "an empty executable was accepted");
        QVERIFY2(!settings.executable.isValid(),
                 "an empty executable made the analysis startable");
    }

    void testTheOkButtonFollowsTheExecutable()
    {
        // The dialog itself, so that the wiring is covered and not only the
        // rule: the button follows the aspect for as long as the dialog is up.
        StartRemoteDialog dlg;
        QPushButton *const ok = dlg.m_buttonBox->button(QDialogButtonBox::Ok);
        QVERIFY(ok);

        dlg.m_settings->executable.setValue(FilePath::fromString("/no/such/command"));
        QTRY_VERIFY2(!dlg.m_settings->executable
                          .validationMessage(QString("/no/such/command")).isEmpty(),
                     "an executable that is not there was accepted");
        QVERIFY2(!ok->isEnabled(), "the analysis could be started with no executable to run");

        const FilePath self = FilePath::fromString(QCoreApplication::applicationFilePath());
        dlg.m_settings->executable.setValue(self);
        QTRY_VERIFY2(dlg.m_settings->executable
                         .validationMessage(self.toFSPathString()).isEmpty()
                         && ok->isEnabled(),
                     "an executable that is there did not enable the analysis");
    }

    void testTheKeysItRemembers()
    {
        // The four keys are in every reader's settings file already, so the
        // names and the group are the compatibility surface.
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QtcSettings s(dir.filePath("test.ini"), QSettings::IniFormat);

        StartRemoteSettings written;
        written.executable.setValue(FilePath::fromString("/usr/bin/valgrind"));
        written.workingDirectory.setValue(FilePath::fromString("/tmp"));
        written.arguments.setValue(QString("--leak-check=full"));
        saveTo(&s, &written);

        QCOMPARE(s.value("AnalyzerStartRemoteDialog/executable").toString(),
                 QString("/usr/bin/valgrind"));
        QCOMPARE(s.value("AnalyzerStartRemoteDialog/workingDirectory").toString(),
                 QString("/tmp"));
        QCOMPARE(s.value("AnalyzerStartRemoteDialog/arguments").toString(),
                 QString("--leak-check=full"));
        QVERIFY(s.contains("AnalyzerStartRemoteDialog/profile"));

        StartRemoteSettings read;
        restoreFrom(&s, &read);
        QCOMPARE(read.executable().toFSPathString(), QString("/usr/bin/valgrind"));
        QCOMPARE(read.workingDirectory().toFSPathString(), QString("/tmp"));
        QCOMPARE(read.arguments(), QString("--leak-check=full"));
    }
};

QObject *createStartRemoteDialogTest()
{
    return new StartRemoteDialogTest;
}

#endif // WITH_TESTS

} // Valgrind::Internal

#ifdef WITH_TESTS
#include "startremotedialog.moc"
#endif
