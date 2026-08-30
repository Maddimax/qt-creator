// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "androidconfigurations.h"
#include "androidsdkmanager.h"
#include "androidtr.h"
#include "sdkmanageroutputparser.h"

#include <coreplugin/dialogs/ioptionspage.h>
#include <coreplugin/icore.h>
#include <coreplugin/outputpaneview.h>

#include <solutions/spinner/spinner.h>
#include <QtTaskTree/QConditional>
#include <QtTaskTree/QSingleTaskTreeRunner>

#include <utils/algorithm.h>
#include <utils/aspects.h>
#include <utils/environment.h>
#include <utils/layoutbuilder.h>
#include <utils/outputformatter.h>
#include <utils/qtcprocess.h>

#ifdef WITH_TESTS
#include <QTest>
#endif

#include <QDialogButtonBox>
#include <QVBoxLayout>
#include <QLabel>
#include <QLoggingCategory>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPointer>
#include <QProgressBar>
#include <QRegularExpression>

namespace {
Q_LOGGING_CATEGORY(sdkManagerLog, "qtc.android.sdkManager", QtWarningMsg)
}

using namespace SpinnerSolution;
using namespace QtTaskTree;
using namespace Utils;

using namespace std::chrono;
using namespace std::chrono_literals;

namespace Android::Internal {

// What the reader watches while the SDK manager runs: its output, the licence
// question when one comes, and how far along it is.
class SdkProgressSettings final : public AspectContainer
{
public:
    SdkProgressSettings()
    {
        setAutoApply(true);
        setQmlSource(QUrl("qrc:/qt/qml/QtCreator/Android/SdkManagerProgress.qml"));

        question.setQmlName("Question");
        question.setText(Tr::tr("Do you want to accept the Android SDK license?"));
        question.setVisible(false);

        yes.setQmlName("Yes");
        yes.setActionText(Tr::tr("Yes"));
        yes.setVisible(false);
        no.setQmlName("No");
        no.setActionText(Tr::tr("No"));
        no.setVisible(false);

        progress.setQmlName("Progress");
        progress.setRange(0, 100);
    }

    // The question and the two buttons come and go together: there is nothing
    // to answer until one is asked.
    void setQuestionVisible(bool visible)
    {
        question.setVisible(visible);
        yes.setVisible(visible);
        no.setVisible(visible);
    }

    void setQuestionEnabled(bool enable)
    {
        question.setEnabled(enable);
        yes.setEnabled(enable);
        no.setEnabled(enable);
    }

    TextDisplay question{this};
    ActionAspect yes{this};
    ActionAspect no{this};
    ProgressAspect progress{this};
};

class QuestionProgressDialog : public QDialog
{
    Q_OBJECT

public:
    QuestionProgressDialog()
        : QDialog(Core::ICore::dialogParent())
        , m_output(new Core::OutputPaneView)
        , m_settings(new SdkProgressSettings)
        , m_dialogButtonBox(new QDialogButtonBox(QDialogButtonBox::Cancel))
    {
        setWindowTitle(Tr::tr("Android SDK Manager"));

        m_settings->yes.setAction([this] { emit answerClicked(true); });
        m_settings->no.setAction([this] { emit answerClicked(false); });

        auto layout = new QVBoxLayout(this);
        layout->addWidget(m_output);
        layout->addWidget(Core::createAspectForm(m_settings.get()));
        layout->addWidget(m_dialogButtonBox);

        m_settings->setQuestionVisible(false);
        m_settings->setQuestionEnabled(false);

        connect(m_dialogButtonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
        connect(m_dialogButtonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);

        // GUI tuning
        setModal(true);
        resize(800, 600);
        show();
    }

    ~QuestionProgressDialog() override = default;

    void setQuestionEnabled(bool enable) { m_settings->setQuestionEnabled(enable); }
    void setQuestionVisible(bool visible) { m_settings->setQuestionVisible(visible); }

    void appendMessage(const QString &text, OutputFormat format)
    {
        m_output->appendMessage(text, format);
    }

    void setProgress(int value) { m_settings->progress.setValue(value); }

    void setDone()
    {
        m_dialogButtonBox->setStandardButtons(QDialogButtonBox::Close);
    }

signals:
    void answerClicked(bool accepted);

private:
    Core::OutputPaneView *m_output = nullptr;
    const std::unique_ptr<SdkProgressSettings> m_settings;
    QDialogButtonBox *m_dialogButtonBox = nullptr;

#ifdef WITH_TESTS
    friend class SdkManagerProgressTest;
#endif
};

static QString sdkRootArg()
{
    return "--sdk_root=" + AndroidConfig::sdkLocation().path();
}

const QRegularExpression &assertionRegExp()
{
    static const QRegularExpression theRegExp
        (R"((\(\s*y\s*[\/\\]\s*n\s*\)\s*)(?<mark>[\:\?]))", // (y/N)?
        QRegularExpression::CaseInsensitiveOption | QRegularExpression::MultilineOption);

    return theRegExp;
}

static std::optional<int> parseProgress(const QString &out)
{
    if (out.isEmpty())
        return {};

    static const QRegularExpression reg("(?<progress>\\d*)%");
    static const QRegularExpression regEndOfLine("[\\n\\r]");
    const QStringList lines = out.split(regEndOfLine, Qt::SkipEmptyParts);
    std::optional<int> progress;
    for (const QString &line : lines) {
        QRegularExpressionMatch match = reg.match(line);
        if (match.hasMatch()) {
            const int parsedProgress = match.captured("progress").toInt();
            if (parsedProgress >= 0 && parsedProgress <= 100)
                progress = parsedProgress;
        }
    }
    return progress;
}

struct DialogStorage
{
    DialogStorage() { m_dialog.reset(new QuestionProgressDialog); };
    std::unique_ptr<QuestionProgressDialog> m_dialog;
};

static GroupItem licensesRecipe(const Storage<DialogStorage> &dialogStorage)
{
    struct OutputData
    {
        QString buffer;
        int current = 0;
        int total = 0;
    };

    const Storage<OutputData> outputStorage;

    const auto onLicenseSetup = [dialogStorage, outputStorage](Process &process) {
        QuestionProgressDialog *dialog = dialogStorage->m_dialog.get();
        dialog->setProgress(0);
        dialog->appendMessage(Tr::tr("Checking pending licenses...") + "\n", NormalMessageFormat);
        dialog->appendMessage(Tr::tr("The installation of Android SDK packages may fail if the "
                                     "respective licenses are not accepted.") + "\n\n",
                              LogMessageFormat);
        process.setProcessMode(ProcessMode::Writer);
        process.setEnvironment(AndroidConfig::toolsEnvironment());
        process.setCommand(CommandLine(AndroidConfig::sdkManagerToolPath(),
                                       {"--licenses", sdkRootArg()}));
        process.setUseCtrlCStub(true);

        Process *processPtr = &process;
        OutputData *outputPtr = outputStorage.activeStorage();
        QObject::connect(processPtr, &Process::readyReadStandardOutput, dialog,
                         [processPtr, outputPtr, dialog] {
            const QString stdOut = processPtr->readAllStandardOutput();
            outputPtr->buffer += stdOut;
            dialog->appendMessage(stdOut, StdOutFormat);
            const auto progress = parseProgress(stdOut);
            if (progress)
                dialog->setProgress(*progress);
            if (assertionRegExp().match(outputPtr->buffer).hasMatch()) {
                dialog->setQuestionVisible(true);
                dialog->setQuestionEnabled(true);
                if (outputPtr->total == 0) {
                    // Example output to match:
                    //   5 of 6 SDK package licenses not accepted.
                    //   Review licenses that have not been accepted (y/N)?
                    static const QRegularExpression reg(R"(((?<steps>\d+)\sof\s)\d+)");
                    const QRegularExpressionMatch match = reg.match(outputPtr->buffer);
                    if (match.hasMatch()) {
                        outputPtr->total = match.captured("steps").toInt();
                        const QByteArray reply = "y\n";
                        dialog->appendMessage(QString::fromUtf8(reply), NormalMessageFormat);
                        processPtr->writeRaw(reply);
                        dialog->setProgress(0);
                    }
                }
                outputPtr->buffer.clear();
            }
        });

        QObject::connect(dialog, &QuestionProgressDialog::answerClicked, processPtr,
                         [processPtr, outputPtr, dialog](bool accepted) {
            dialog->setQuestionEnabled(false);
            const QByteArray reply = accepted ? "y\n" : "n\n";
            dialog->appendMessage(QString::fromUtf8(reply), NormalMessageFormat);
            processPtr->writeRaw(reply);
            ++outputPtr->current;
            if (outputPtr->total != 0)
                dialog->setProgress(outputPtr->current * 100.0 / outputPtr->total);
        });
    };

    return Group { outputStorage, ProcessTask(onLicenseSetup) };
}

static void setupSdkProcess(const QStringList &args, Process *process,
                            QuestionProgressDialog *dialog, int current, int total)
{
    process->setEnvironment(AndroidConfig::toolsEnvironment());
    process->setCommand({AndroidConfig::sdkManagerToolPath(),
                         args + AndroidConfig::sdkManagerToolArgs()});
    QObject::connect(process, &Process::readyReadStandardOutput, dialog,
                     [process, dialog, current, total] {
        const auto progress = parseProgress(process->readAllStandardOutput());
        if (!progress)
            return;
        dialog->setProgress((current * 100.0 + *progress) / total);
    });
    QObject::connect(process, &Process::readyReadStandardError, dialog, [process, dialog] {
        dialog->appendMessage(process->readAllStandardError(), StdErrFormat);
    });
};

static void handleSdkProcess(QuestionProgressDialog *dialog, DoneWith result)
{
    if (result == DoneWith::Success)
        dialog->appendMessage(Tr::tr("Finished successfully.") + "\n\n", StdOutFormat);
    else
        dialog->appendMessage(Tr::tr("Failed.") + "\n\n", StdErrFormat);
}

static GroupItem installationRecipe(const Storage<DialogStorage> &dialogStorage,
                                    const InstallationChange &change)
{
    const auto onSetup = [dialogStorage] {
        dialogStorage->m_dialog->appendMessage(
            Tr::tr("Installing / Uninstalling selected packages...") + '\n', NormalMessageFormat);
        const QString optionsMessage = HostOsInfo::isMacHost()
            ? Tr::tr("Closing the preferences dialog will cancel the running and scheduled SDK "
                     "operations.")
            : Tr::tr("Closing the options dialog will cancel the running and scheduled SDK "
                     "operations.");
        dialogStorage->m_dialog->appendMessage(optionsMessage + '\n', LogMessageFormat);
    };

    const int total = change.count();
    const ListIterator uninstallIterator(change.toUninstall);
    const auto onUninstallSetup = [dialogStorage, uninstallIterator, total](Process &process) {
        const QStringList args = {"--uninstall", *uninstallIterator, sdkRootArg()};
        QuestionProgressDialog *dialog = dialogStorage->m_dialog.get();
        setupSdkProcess(args, &process, dialog, uninstallIterator.iteration(), total);
        dialog->appendMessage(Tr::tr("Uninstalling %1...").arg(*uninstallIterator) + '\n',
                              StdOutFormat);
        dialog->setProgress(uninstallIterator.iteration() * 100.0 / total);
    };

    const ListIterator installIterator(change.toInstall);
    const int offset = change.toUninstall.count();
    const auto onInstallSetup = [dialogStorage, installIterator, offset, total](Process &process) {
        const QStringList args = {*installIterator, sdkRootArg()};
        QuestionProgressDialog *dialog = dialogStorage->m_dialog.get();
        setupSdkProcess(args, &process, dialog, offset + installIterator.iteration(), total);
        dialog->appendMessage(Tr::tr("Installing %1...").arg(*installIterator) + '\n',
                              StdOutFormat);
        dialog->setProgress((offset + installIterator.iteration()) * 100.0 / total);
    };

    const auto onDone = [dialogStorage](DoneWith result) {
        handleSdkProcess(dialogStorage->m_dialog.get(), result);
    };

    return Group {
        continueOnError,
        onGroupSetup(onSetup),
        For (uninstallIterator) >> Do {
            continueOnError,
            ProcessTask(onUninstallSetup, onDone)
        },
        For (installIterator) >> Do {
            continueOnError,
            ProcessTask(onInstallSetup, onDone)
        },
        onGroupDone([dialogStorage] { dialogStorage->m_dialog->setProgress(100); })
    };
}

static GroupItem updateRecipe(const Storage<DialogStorage> &dialogStorage)
{
    const auto onSetup = [dialogStorage](Process &process) {
        const QStringList args = {"--update", sdkRootArg()};
        QuestionProgressDialog *dialog = dialogStorage->m_dialog.get();
        setupSdkProcess(args, &process, dialog, 0, 1);
        dialog->appendMessage(Tr::tr("Updating installed packages...") + '\n', NormalMessageFormat);
        dialog->setProgress(0);
    };
    const auto onDone = [dialogStorage](DoneWith result) {
        handleSdkProcess(dialogStorage->m_dialog.get(), result);
    };

    return ProcessTask(onSetup, onDone);
}

class AndroidSdkManagerPrivate
{
public:
    AndroidSdkManagerPrivate(AndroidSdkManager &sdkManager);
    ~AndroidSdkManagerPrivate();

    AndroidSdkPackageList filteredPackages(AndroidSdkPackage::PackageState state,
                                           AndroidSdkPackage::PackageType type)
    {
        m_sdkManager.refreshPackages();
        return Utils::filtered(m_allPackages, [state, type](const AndroidSdkPackage *p) {
            return p->state() & state && p->type() & type;
        });
    }
    const AndroidSdkPackageList &allPackages();

    void reloadSdkPackages();

    void runDialogRecipe(const Storage<DialogStorage> &dialogStorage,
                         const GroupItem &licenseRecipe, const GroupItem &continuationRecipe);

    QPointer<QWidget> m_spinnerTarget;
    AndroidSdkManager &m_sdkManager;
    AndroidSdkPackageList m_allPackages;
    FilePath lastSdkManagerPath;
    bool m_packageListingSuccessful = false;
    QSingleTaskTreeRunner m_taskTreeRunner;
};

AndroidSdkManager::AndroidSdkManager() : m_d(new AndroidSdkManagerPrivate(*this)) {}

AndroidSdkManager::~AndroidSdkManager() = default;

void AndroidSdkManager::setSpinnerTarget(QWidget *spinnerTarget)
{
    m_d->m_spinnerTarget = spinnerTarget;
}

SdkPlatformList AndroidSdkManager::installedSdkPlatforms()
{
    const AndroidSdkPackageList list = m_d->filteredPackages(AndroidSdkPackage::Installed,
                                                             AndroidSdkPackage::SdkPlatformPackage);
    return Utils::static_container_cast<SdkPlatform *>(list);
}

const AndroidSdkPackageList &AndroidSdkManager::allSdkPackages()
{
    return m_d->allPackages();
}

QStringList AndroidSdkManager::notFoundEssentialSdkPackages()
{
    QStringList essentials = AndroidConfig::allEssentials();
    const AndroidSdkPackageList &packages = allSdkPackages();
    for (AndroidSdkPackage *package : packages) {
        essentials.removeOne(package->sdkStylePath());
        if (essentials.isEmpty())
            return {};
    }
    return essentials;
}

QStringList AndroidSdkManager::missingEssentialSdkPackages()
{
    const QStringList essentials = AndroidConfig::allEssentials();
    const AndroidSdkPackageList &packages = allSdkPackages();
    QStringList missingPackages;
    for (AndroidSdkPackage *package : packages) {
        if (essentials.contains(package->sdkStylePath())
            && package->state() != AndroidSdkPackage::Installed) {
            missingPackages.append(package->sdkStylePath());
        }
    }
    return missingPackages;
}

AndroidSdkPackageList AndroidSdkManager::installedSdkPackages()
{
    return m_d->filteredPackages(AndroidSdkPackage::Installed, AndroidSdkPackage::AnyValidType);
}

SystemImageList AndroidSdkManager::installedSystemImages()
{
    const AndroidSdkPackageList list = m_d->filteredPackages(AndroidSdkPackage::AnyValidState,
                                                             AndroidSdkPackage::SdkPlatformPackage);
    const QList<SdkPlatform *> platforms = Utils::static_container_cast<SdkPlatform *>(list);
    SystemImageList result;
    for (SdkPlatform *platform : platforms) {
        if (!platform->systemImages().isEmpty())
            result.append(platform->systemImages());
    }
    return result;
}

NdkList AndroidSdkManager::installedNdkPackages()
{
    const AndroidSdkPackageList list = m_d->filteredPackages(AndroidSdkPackage::Installed,
                                                             AndroidSdkPackage::NDKPackage);
    return Utils::static_container_cast<Ndk *>(list);
}

SdkPlatform *AndroidSdkManager::latestAndroidSdkPlatform(AndroidSdkPackage::PackageState state)
{
    SdkPlatform *result = nullptr;
    const AndroidSdkPackageList list = m_d->filteredPackages(state,
                                                             AndroidSdkPackage::SdkPlatformPackage);
    for (AndroidSdkPackage *p : list) {
        auto platform = static_cast<SdkPlatform *>(p);
        if (!result || result->apiLevel() < platform->apiLevel())
            result = platform;
    }
    return result;
}

SdkPlatformList AndroidSdkManager::filteredSdkPlatforms(int minApiLevel,
                                                        AndroidSdkPackage::PackageState state)
{
    const AndroidSdkPackageList list = m_d->filteredPackages(state,
                                                             AndroidSdkPackage::SdkPlatformPackage);
    SdkPlatformList result;
    for (AndroidSdkPackage *p : list) {
        auto platform = static_cast<SdkPlatform *>(p);
        if (platform && platform->apiLevel() >= minApiLevel)
            result << platform;
    }
    return result;
}

BuildToolsList AndroidSdkManager::filteredBuildTools(int minApiLevel,
                                                     AndroidSdkPackage::PackageState state)
{
    const AndroidSdkPackageList list = m_d->filteredPackages(state,
                                                             AndroidSdkPackage::BuildToolsPackage);
    BuildToolsList result;
    for (AndroidSdkPackage *p : list) {
        auto platform = dynamic_cast<BuildTools *>(p);
        if (platform && platform->revision().majorVersion() >= minApiLevel)
            result << platform;
    }
    return result;
}

void AndroidSdkManager::refreshPackages()
{
    if (AndroidConfig::sdkManagerToolPath() != m_d->lastSdkManagerPath)
        reloadPackages();
}

void AndroidSdkManager::reloadPackages()
{
    m_d->reloadSdkPackages();
}

bool AndroidSdkManager::packageListingSuccessful() const
{
    return m_d->m_packageListingSuccessful;
}

/*!
    Runs the \c sdkmanger tool with arguments \a args. Returns \c true if the command is
    successfully executed. Output is copied into \a output. The function blocks the calling thread.
 */
static bool sdkManagerCommand(const QStringList &args, QString *output)
{
    QStringList newArgs = args;
    newArgs.append(sdkRootArg());
    Process proc;
    proc.setEnvironment(AndroidConfig::toolsEnvironment());
    proc.setCommand({AndroidConfig::sdkManagerToolPath(), newArgs});
    qCDebug(sdkManagerLog).noquote() << "Running SDK Manager command (sync):"
                                     << proc.commandLine().toUserOutput();
    proc.runBlocking(60s);
    if (output)
        *output = proc.allOutput();
    return proc.result() == ProcessResult::FinishedWithSuccess;
}

AndroidSdkManagerPrivate::AndroidSdkManagerPrivate(AndroidSdkManager &sdkManager)
    : m_sdkManager(sdkManager)
{}

AndroidSdkManagerPrivate::~AndroidSdkManagerPrivate()
{
    qDeleteAll(m_allPackages);
}

const AndroidSdkPackageList &AndroidSdkManagerPrivate::allPackages()
{
    m_sdkManager.refreshPackages();
    return m_allPackages;
}

void AndroidSdkManagerPrivate::reloadSdkPackages()
{
    std::unique_ptr<Spinner> spinner;
    if (m_spinnerTarget) {
        spinner.reset(new Spinner(SpinnerSize::Medium, m_spinnerTarget));
        spinner->show();
    }

    lastSdkManagerPath = AndroidConfig::sdkManagerToolPath();
    m_packageListingSuccessful = false;

    if (AndroidConfig::sdkToolsVersion().isNull()) {
        // Configuration has invalid sdk path or corrupt installation.
        qDeleteAll(m_allPackages);
        m_allPackages.clear();
        emit m_sdkManager.packagesReloaded();
        return;
    }

    QString packageListing;
    QStringList args({"--list", "--verbose"});
    args << AndroidConfig::sdkManagerToolArgs();
    m_packageListingSuccessful = sdkManagerCommand(args, &packageListing);
    qDeleteAll(m_allPackages); // Must be done after the blocking command execution. See QTCREATORBUG-31920.
    m_allPackages.clear();
    if (m_packageListingSuccessful) {
        SdkManagerOutputParser parser(m_allPackages);
        parser.parsePackageListing(packageListing);
    } else {
        qCWarning(sdkManagerLog) << "Failed parsing packages:" << packageListing;
    }

    emit m_sdkManager.packagesReloaded();
}

void AndroidSdkManagerPrivate::runDialogRecipe(const Storage<DialogStorage> &dialogStorage,
                                               const GroupItem &licensesRecipe,
                                               const GroupItem &continuationRecipe)
{
    const auto onCancelSetup = [dialogStorage] {
        return makeObjectSignal(dialogStorage->m_dialog.get(), &QDialog::rejected);
    };
    const auto onAcceptSetup = [dialogStorage] {
        return makeObjectSignal(dialogStorage->m_dialog.get(), &QDialog::accepted);
    };
    const auto onError = [dialogStorage] { dialogStorage->m_dialog->setDone(); };
    const Group recipe {
        dialogStorage,
        Group {
            If (!Group {
                licensesRecipe,
                QSyncTask([dialogStorage] { dialogStorage->m_dialog->setQuestionVisible(false); }),
                continuationRecipe
            }) >> Then {
                QSyncTask(onError).withAccept(onAcceptSetup)
            }
        }.withCancel(onCancelSetup)
    };
    m_taskTreeRunner.start(recipe, {}, [this] {
        QMetaObject::invokeMethod(&m_sdkManager, &AndroidSdkManager::reloadPackages,
                                  Qt::QueuedConnection);
    });
}

void AndroidSdkManager::runInstallationChange(const InstallationChange &change,
                                              const QString &extraMessage)
{
    if (change.count() == 0)
        return;

    QString message = Tr::tr("%n Android SDK packages shall be updated.", "", change.count());
    if (!extraMessage.isEmpty())
        message.prepend(extraMessage + "\n\n");

    QMessageBox messageDlg(QMessageBox::Information, Tr::tr("Android SDK Changes"),
                           message, QMessageBox::Ok | QMessageBox::Cancel,
                           Core::ICore::dialogParent());

    QString details;
    if (!change.toUninstall.isEmpty()) {
        QStringList toUninstall = {Tr::tr("[Packages to be uninstalled:]")};
        toUninstall += change.toUninstall;
        details += toUninstall.join("\n   ");
    }
    if (!change.toInstall.isEmpty()) {
        if (!change.toUninstall.isEmpty())
            details.append("\n\n");
        QStringList toInstall = {Tr::tr("[Packages to be installed:]")};
        toInstall += change.toInstall;
        details += toInstall.join("\n   ");
    }
    messageDlg.setDetailedText(details);
    if (messageDlg.exec() == QMessageBox::Cancel)
        return;

    const Storage<DialogStorage> dialogStorage;
    m_d->runDialogRecipe(dialogStorage,
        change.toInstall.count() ? licensesRecipe(dialogStorage) : nullItem,
        installationRecipe(dialogStorage, change));
}

void AndroidSdkManager::runUpdate()
{
    const Storage<DialogStorage> dialogStorage;
    m_d->runDialogRecipe(dialogStorage, licensesRecipe(dialogStorage), updateRecipe(dialogStorage));
}

AndroidSdkManager &sdkManager()
{
    static AndroidSdkManager theAndroidSdkManager;
    return theAndroidSdkManager;
}

#ifdef WITH_TESTS

class SdkManagerProgressTest final : public QObject
{
    Q_OBJECT

private slots:
    void testTheFormDrawsWithTheQmlItNames()
    {
        SdkProgressSettings settings;
        const Result<> rendered
            = Core::aspectFormRenders(&settings, "SdkManagerProgress.qml");
        QVERIFY2(rendered, qPrintable(rendered ? QString() : rendered.error()));
    }

    void testTheQuestionAndItsAnswersComeAndGoTogether()
    {
        // There is nothing to answer until a licence is asked about, and a
        // button offered with no question beside it would be unanswerable.
        SdkProgressSettings settings;
        settings.setQuestionVisible(false);
        QVERIFY(!settings.question.isVisible());
        QVERIFY2(!settings.yes.isVisible(), "Yes was offered with no question");
        QVERIFY2(!settings.no.isVisible(), "No was offered with no question");

        settings.setQuestionVisible(true);
        QVERIFY(settings.question.isVisible());
        QVERIFY(settings.yes.isVisible());
        QVERIFY(settings.no.isVisible());

        // And they are enabled together too - the question arrives before the
        // process is ready to be answered.
        settings.setQuestionEnabled(false);
        QVERIFY(!settings.yes.isEnabled());
        QVERIFY(!settings.no.isEnabled());
        settings.setQuestionEnabled(true);
        QVERIFY(settings.yes.isEnabled());
        QVERIFY(settings.no.isEnabled());
    }

    void testHowFarAlongItSays()
    {
        SdkProgressSettings settings;
        QCOMPARE(settings.progress.presentation().control, AspectControls::ProgressBar);
        QCOMPARE(settings.progress.maximum(), 100);

        settings.progress.setValue(30);
        QCOMPARE(settings.progress(), 30);
    }
};

QObject *createSdkManagerProgressTest()
{
    return new SdkManagerProgressTest;
}

#endif // WITH_TESTS

} // namespace Android::Internal

#include "androidsdkmanager.moc"
