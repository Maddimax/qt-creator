// Copyright (C) 2016 BlackBerry Limited. All rights reserved.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "qnxdeployqtlibrariesdialog.h"

#include "qnxconstants.h"
#include "qnxqtversion.h"
#include "qnxtr.h"

#include <coreplugin/icore.h>

#include <projectexplorer/deployablefile.h>
#include <projectexplorer/devicesupport/filetransfer.h>
#include <projectexplorer/devicesupport/idevice.h>

#include <qtsupport/qtversionmanager.h>

#include <coreplugin/dialogs/ioptionspage.h>
#include <coreplugin/outputpaneview.h>

#include <utils/algorithm.h>
#include <utils/aspects.h>
#include <utils/dialogtask.h>
#include <utils/guiutils.h>
#include <utils/hostosinfo.h>
#include <utils/layoutbuilder.h>
#include <utils/qtcprocess.h>
#include <utils/processinterface.h>
#include <utils/qtcassert.h>

#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#ifdef WITH_TESTS
#include <QTest>
#endif

#include <QHBoxLayout>
#include <QPlainTextEdit>
#include <QVBoxLayout>
#include <QProgressBar>
#include <QPushButton>
#include <QtTaskTree/QConditional>
#include <QtTaskTree/QSingleTaskTreeRunner>

using namespace ProjectExplorer;
using namespace QtSupport;
using namespace QtTaskTree;
using namespace Utils;

namespace Qnx::Internal {

// How many files a line of sftp chatter says have arrived. The upload reports
// one line per file, and a symlink counts too.
int progressStepsIn(const QString &message)
{
    return message.count("sftp> put") + message.count("sftp> ln -s");
}

// Whether closing has to be confirmed: a deployment that is still running is
// stopped by it.
bool mustConfirmClose(bool deploying)
{
    return deploying;
}

class DeployQtSettings final : public AspectContainer
{
public:
    DeployQtSettings()
    {
        setAutoApply(true);
        setQmlSource(QUrl("qrc:/qt/qml/QtCreator/Qnx/DeployQtLibrariesDialog.qml"));

        library.setQmlName("Library");
        library.setLabelText(Tr::tr("Qt library to deploy:"));
        library.setDisplayStyle(SelectionAspect::DisplayStyle::ComboBox);

        deploy.setQmlName("Deploy");
        deploy.setActionText(Tr::tr("Deploy"));

        basePath.setQmlName("BasePath");

        remoteDirectory.setQmlName("RemoteDirectory");
        remoteDirectory.setLabelText(Tr::tr("Remote directory:"));
        remoteDirectory.setDisplayStyle(StringAspect::LineEditDisplay);
        remoteDirectory.setValue("/qt");

        progress.setQmlName("Progress");
        progress.setRange(0, 0);
    }

    // While a deployment runs, what it is deploying and where cannot change.
    void setDeploying(bool deploying)
    {
        library.setEnabled(!deploying);
        deploy.setEnabled(!deploying);
        remoteDirectory.setEnabled(!deploying);
    }

    SelectionAspect library{this};
    ActionAspect deploy{this};
    TextDisplay basePath{this};
    StringAspect remoteDirectory{this};
    ProgressAspect progress{this};
};

class QnxDeployQtLibrariesDialog : public QDialog
{
public:
    explicit QnxDeployQtLibrariesDialog(const ProjectExplorer::IDeviceConstPtr &device);
    ~QnxDeployQtLibrariesDialog() override;

private:
    void closeEvent(QCloseEvent *event) override;

    void start();
    void stop();

    void updateProgress(const QString &progressMessage);
    void handleUploadFinished();

    QList<ProjectExplorer::DeployableFile> gatherFiles();
    QList<ProjectExplorer::DeployableFile> gatherFiles(const QString &dirPath,
                                                       const QString &baseDir = {},
                                                       const QStringList &nameFilters = {});

    QString fullRemoteDirectory() const { return m_settings.remoteDirectory(); }

    void appendLog(const QString &msg)
    {
        m_log->appendMessage(msg + '\n', Utils::NormalMessageFormat);
    }

    DeployQtSettings m_settings;
    Core::OutputPaneView *m_log = nullptr;

    ProjectExplorer::IDeviceConstPtr m_device;

    int m_progressCount = 0;

    void emitProgressMessage(const QString &msg)
    {
        updateProgress(msg);
        appendLog(msg);
    }

    void emitErrorMessage(const QString &msg)
    {
        appendLog(msg);
    }

private:
    Group deployRecipe();
    GroupItem checkDirTask(const Storage<bool> &directoryExists);
    GroupItem confirmOverwriteTask();
    GroupItem removeDirTask();
    GroupItem uploadTask();

    mutable QList<DeployableFile> m_deployableFiles;
    QSingleTaskTreeRunner m_taskTreeRunner;
};

static QList<DeployableFile> collectFilesToUpload(const DeployableFile &deployable)
{
    QList<DeployableFile> collected;
    FilePath localFile = deployable.localFilePath();
    if (localFile.isDir()) {
        const FilePaths files = localFile.dirEntries(DirFilterFlag::Files | DirFilterFlag::Dirs | DirFilterFlag::NoDotAndDotDot);
        const QString remoteDir = deployable.remoteDirectory() + '/' + localFile.fileName();
        for (const FilePath &localFilePath : files)
            collected.append(collectFilesToUpload(DeployableFile(localFilePath, remoteDir)));
    } else {
        collected << deployable;
    }
    return collected;
}

GroupItem QnxDeployQtLibrariesDialog::checkDirTask(const Storage<bool> &directoryExists)
{
    const auto onSetup = [this](Process &process) {
        appendLog(Tr::tr("Checking existence of \"%1\"")
                                           .arg(fullRemoteDirectory()));
        process.setCommand({m_device->filePath("test"), {"-d", fullRemoteDirectory()}});
    };
    const auto onDone = [this, directoryExists](const Process &process, DoneWith result) {
        if (result == DoneWith::Success) {
            *directoryExists = true;
            return DoneResult::Success;
        }
        if (process.result() == ProcessResult::FinishedWithError) {
            // The remote directory does not exist - nothing to remove.
            *directoryExists = false;
            return DoneResult::Success;
        }
        appendLog(Tr::tr("Connection failed: %1")
                                           .arg(process.errorString()));
        return DoneResult::Error;
    };
    return ProcessTask(onSetup, onDone);
}

GroupItem QnxDeployQtLibrariesDialog::confirmOverwriteTask()
{
    const auto onSetup = [this](DialogWrapper<QMessageBox> &task) {
        task.setParent(Core::ICore::dialogParent());
        QMessageBox *box = task.dialog();
        box->setIcon(QMessageBox::Question);
        box->setWindowTitle(windowTitle());
        box->setText(Tr::tr("The remote directory \"%1\" already exists.\n"
                       "Deploying to that directory will remove any files already present.\n\n"
                       "Are you sure you want to continue?").arg(fullRemoteDirectory()));
        box->setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    };
    // "No" reports an error, which aborts the deployment via the recipe's stop-on-error policy.
    return DialogTask<QMessageBox>(onSetup);
}

GroupItem QnxDeployQtLibrariesDialog::removeDirTask()
{
    const auto onSetup = [this](Process &process) {
        appendLog(Tr::tr("Removing \"%1\"").arg(fullRemoteDirectory()));
        process.setCommand({m_device->filePath("rm"), {"-rf", fullRemoteDirectory()}});
    };
    const auto onError = [this](const Process &process) {
        QTC_ASSERT(process.exitCode() == 0, return);
        appendLog(Tr::tr("Connection failed: %1")
                                           .arg(process.errorString()));
    };
    return ProcessTask(onSetup, onError, CallDoneFlag::OnError);
}

GroupItem QnxDeployQtLibrariesDialog::uploadTask()
{
    const auto onSetup = [this](FileTransfer &transfer) {
        if (m_deployableFiles.isEmpty()) {
            emitProgressMessage(Tr::tr("No files need to be uploaded."));
            return SetupResult::StopWithSuccess;
        }
        emitProgressMessage(Tr::tr("%n file(s) need to be uploaded.", "",
                                   m_deployableFiles.size()));
        FilesToTransfer files;
        for (const DeployableFile &file : std::as_const(m_deployableFiles)) {
            if (!file.localFilePath().exists()) {
                const QString message = Tr::tr("Local file \"%1\" does not exist.")
                                              .arg(file.localFilePath().toUserOutput());
                emitErrorMessage(message);
                return SetupResult::StopWithError;
            }
            const FilePermissions permissions = file.isExecutable()
                ? FilePermissions::ForceExecutable : FilePermissions::Default;
            files.append({file.localFilePath(), m_device->filePath(file.remoteFilePath()),
                          permissions});
        }
        if (files.isEmpty()) {
            emitProgressMessage(Tr::tr("No files need to be uploaded."));
            return SetupResult::StopWithSuccess;
        }
        transfer.setFilesToTransfer(files);
        QObject::connect(&transfer, &FileTransfer::progress,
                         this, &QnxDeployQtLibrariesDialog::emitProgressMessage);
        return SetupResult::Continue;
    };
    const auto onError = [this](const FileTransfer &transfer) {
        // The transfer error is the raw sftp stderr. On a first deployment it is
        // dominated by harmless lines that bury the real failure. Drop those so
        // it stays visible, and hint at the usual cause.
        const QString errorString = transfer.resultData().m_errorString;
        QStringList lines;
        for (const QString &line : errorString.split('\n', Qt::SkipEmptyParts)) {
            const QString trimmed = line.trimmed();
            const bool harmlessRm = (trimmed.startsWith("remote delete ")
                                     || trimmed.startsWith("Couldn't delete file"))
                                    && trimmed.endsWith("No such file or directory");
            const bool harmlessMkdir = trimmed.startsWith("remote mkdir ")
                                       && trimmed.endsWith("Failure");
            if (!harmlessRm && !harmlessMkdir)
                lines.append(line);
        }
        if (!lines.isEmpty())
            emitErrorMessage(lines.join('\n'));
        emitErrorMessage(Tr::tr("Deployment failed. Make sure the device has enough free disk "
                                "space and that the remote directory \"%1\" is writable.")
                             .arg(fullRemoteDirectory()));
    };
    return FileTransferTask(onSetup, onError, CallDoneFlag::OnError);
}

Group QnxDeployQtLibrariesDialog::deployRecipe()
{
    const auto setupHandler = [this] {
        if (!m_device) {
            emitErrorMessage(Tr::tr("No device configuration set."));
            return SetupResult::StopWithError;
        }
        QList<DeployableFile> collected;
        for (int i = 0; i < m_deployableFiles.count(); ++i)
            collected.append(collectFilesToUpload(m_deployableFiles.at(i)));

        QTC_CHECK(collected.size() >= m_deployableFiles.size());
        m_deployableFiles = collected;
        if (!m_deployableFiles.isEmpty())
            return SetupResult::Continue;

        emitProgressMessage(Tr::tr("No deployment action necessary. Skipping."));
        return SetupResult::StopWithSuccess;
    };
    const auto doneHandler = [this] {
        emitProgressMessage(Tr::tr("All files successfully deployed."));
    };
    const Storage<bool> directoryExists;
    return {
        directoryExists,
        onGroupSetup(setupHandler),
        checkDirTask(directoryExists),
        If ([directoryExists] { return *directoryExists; }) >> Then {
            confirmOverwriteTask(),
            removeDirTask()
        },
        uploadTask(),
        onGroupDone(doneHandler, CallDoneFlag::OnSuccess)
    };
}

void QnxDeployQtLibrariesDialog::start()
{
    QTC_ASSERT(m_device, return);
    QTC_ASSERT(!m_taskTreeRunner.isRunning(), return);
    if (fullRemoteDirectory().isEmpty()) {
        QMessageBox::warning(this, windowTitle(),
                             Tr::tr("Please input a remote directory to deploy to."));
        return;
    }

    m_progressCount = 0;
    m_settings.progress.setValue(0);
    m_settings.setDeploying(true);
    m_log->clear();

    m_deployableFiles = gatherFiles();
    m_settings.progress.setRange(0, m_deployableFiles.count());

    m_taskTreeRunner.start(deployRecipe(), {}, [this] { handleUploadFinished(); });
}

void QnxDeployQtLibrariesDialog::stop()
{
    if (!m_taskTreeRunner.isRunning())
        return;
    m_taskTreeRunner.reset();
    handleUploadFinished();
}

QnxDeployQtLibrariesDialog::QnxDeployQtLibrariesDialog(const IDevice::ConstPtr &device)
    : QDialog(dialogParent())
    , m_log(new Core::OutputPaneView)
    , m_device(device)
{
    setWindowTitle(Tr::tr("Deploy Qt to QNX Device"));

    const QtVersions qtVersions = QtVersionManager::sortVersions(
                QtVersionManager::versions(QtVersion::isValidPredicate(
                equal(&QtVersion::type, QString::fromLatin1(Constants::QNX_QNX_QT)))));
    for (QtVersion *v : qtVersions)
        m_settings.library.addOption({v->displayName(), {}, v->uniqueId()});

    m_settings.deploy.setAction([this] { start(); });

    auto closeButton = new QPushButton(Tr::tr("Close"), this);
    connect(closeButton, &QAbstractButton::clicked, this, &QWidget::close);

    auto buttonRow = new QHBoxLayout;
    buttonRow->addStretch();
    buttonRow->addWidget(closeButton);

    auto layout = new QVBoxLayout(this);
    layout->addWidget(Core::createAspectForm(&m_settings));
    layout->addWidget(m_log);
    layout->addLayout(buttonRow);
}

QnxDeployQtLibrariesDialog::~QnxDeployQtLibrariesDialog() = default;

void QnxDeployQtLibrariesDialog::closeEvent(QCloseEvent *event)
{
    // A disabled Deploy button indicates the upload is still running
    if (mustConfirmClose(!m_settings.deploy.isEnabled())) {
        const int answer = QMessageBox::question(this, windowTitle(),
            Tr::tr("Closing the dialog will stop the deployment. Are you sure you want to do this?"),
            QMessageBox::Yes | QMessageBox::No);
        if (answer == QMessageBox::No)
            event->ignore();
        else if (answer == QMessageBox::Yes)
            stop();
    }
}

void QnxDeployQtLibrariesDialog::updateProgress(const QString &progressMessage)
{
    const int progress = progressStepsIn(progressMessage);
    if (progress != 0) {
        m_progressCount += progress;
        m_settings.progress.setValue(m_progressCount);
    }
}

void QnxDeployQtLibrariesDialog::handleUploadFinished()
{
    m_settings.setDeploying(false);
}

QList<DeployableFile> QnxDeployQtLibrariesDialog::gatherFiles()
{
    QList<DeployableFile> result;

    const int qtVersionId = m_settings.library.itemValue().toInt();

    auto qtVersion = dynamic_cast<const QnxQtVersion *>(QtVersionManager::version(qtVersionId));

    QTC_ASSERT(qtVersion, return result);

    if (HostOsInfo::isWindowsHost()) {
        result.append(gatherFiles(qtVersion->libraryPath().toUrlishString(), {}, {{"*.so.?"}}));
        result.append(gatherFiles(qtVersion->libraryPath().toUrlishString() + QLatin1String("/fonts")));
    } else {
        result.append(gatherFiles(qtVersion->libraryPath().toUrlishString()));
    }

    result.append(gatherFiles(qtVersion->pluginPath().toUrlishString()));
    result.append(gatherFiles(qtVersion->importsPath().toUrlishString()));
    result.append(gatherFiles(qtVersion->qmlPath().toUrlishString()));
    return result;
}

QList<DeployableFile> QnxDeployQtLibrariesDialog::gatherFiles(
        const QString &dirPath, const QString &baseDirPath, const QStringList &nameFilters)
{
    QList<DeployableFile> result;
    if (dirPath.isEmpty())
        return result;

    static const QStringList unusedDirs = {"include", "mkspecs", "cmake", "pkgconfig"};
    const QString dp = dirPath.endsWith('/') ? dirPath.left(dirPath.size() - 1) : dirPath;
    if (unusedDirs.contains(dp))
        return result;

    const QDir dir(dirPath);
    const QFileInfoList list = dir.entryInfoList(nameFilters,
                                                 QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot);
    for (const QFileInfo &fileInfo : list) {
        if (fileInfo.isDir()) {
            result.append(gatherFiles(fileInfo.absoluteFilePath(), baseDirPath.isEmpty() ?
                                          dirPath : baseDirPath));
        } else {
            static const QStringList unusedSuffixes = {"cmake", "la", "prl", "a", "pc"};
            if (unusedSuffixes.contains(fileInfo.suffix()))
                continue;

            QString remoteDir;
            if (baseDirPath.isEmpty()) {
                remoteDir = fullRemoteDirectory() + '/' + QFileInfo(dirPath).baseName();
            } else {
                QDir baseDir(baseDirPath);
                baseDir.cdUp();
                remoteDir = fullRemoteDirectory() + '/' + baseDir.relativeFilePath(dirPath);
            }
            result.append(DeployableFile(FilePath::fromString(fileInfo.absoluteFilePath()),
                                         remoteDir));
        }
    }
    return result;
}

void executeQnxDeployQtLibrariesDialog(const IDeviceConstPtr &device)
{
    QnxDeployQtLibrariesDialog dialog(device);
    dialog.exec();
}

#ifdef WITH_TESTS

class DeployQtLibrariesTest final : public QObject
{
    Q_OBJECT

private slots:
    void testTheDialogDrawsWithTheQmlItNames()
    {
        DeployQtSettings settings;
        const Result<> rendered
            = Core::aspectFormRenders(&settings, "DeployQtLibrariesDialog.qml");
        QVERIFY2(rendered, qPrintable(rendered ? QString() : rendered.error()));
    }

    void testHowFarTheUploadHasGot()
    {
        // The upload says nothing about progress directly; what it prints is
        // sftp chatter, one line per file, and a symlink counts as a file too.
        QCOMPARE(progressStepsIn("sftp> put /some/file"), 1);
        QCOMPARE(progressStepsIn("sftp> ln -s a b"), 1);
        QCOMPARE(progressStepsIn("sftp> put a\nsftp> put b\nsftp> ln -s c d"), 3);

        // Anything else is not progress - a connection message must not move
        // the bar.
        QCOMPARE(progressStepsIn("Connection failed: no route to host"), 0);
        QCOMPARE(progressStepsIn({}), 0);
    }

    void testWhatCannotChangeWhileDeploying()
    {
        // What is being deployed and where cannot change under a running
        // upload.
        DeployQtSettings settings;
        settings.setDeploying(true);
        QVERIFY2(!settings.library.isEnabled(), "the Qt version could be changed mid-upload");
        QVERIFY2(!settings.remoteDirectory.isEnabled(),
                 "the remote directory could be changed mid-upload");
        QVERIFY2(!settings.deploy.isEnabled(), "a second deployment could be started");

        settings.setDeploying(false);
        QVERIFY(settings.library.isEnabled());
        QVERIFY(settings.remoteDirectory.isEnabled());
        QVERIFY(settings.deploy.isEnabled());
    }

    void testWhenClosingHasToBeConfirmed()
    {
        // Closing stops a running deployment, so it is worth asking about;
        // closing an idle dialog is not.
        QVERIFY(mustConfirmClose(true));
        QVERIFY2(!mustConfirmClose(false), "an idle dialog asked before closing");
    }

    void testWhereItDeploysTo()
    {
        DeployQtSettings settings;
        QCOMPARE(settings.remoteDirectory(), QString("/qt"));
        QCOMPARE(settings.progress.presentation().control, AspectControls::ProgressBar);
    }
};

QObject *createDeployQtLibrariesTest()
{
    return new DeployQtLibrariesTest;
}

#endif // WITH_TESTS

} // Qnx::Internal

#ifdef WITH_TESTS
#include "qnxdeployqtlibrariesdialog.moc"
#endif
