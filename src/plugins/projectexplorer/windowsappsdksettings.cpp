// Copyright (C) 2024 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "windowsappsdksettings.h"

#include "projectexplorerconstants.h"
#include "projectexplorertr.h"

#include <coreplugin/dialogs/ioptionspage.h>

#include <QtTaskTree/QNetworkReplyWrapper>
#include <QtTaskTree/QSingleTaskTreeRunner>

#include <utils/detailswidget.h>
#include <utils/environment.h>
#include <utils/hostosinfo.h>
#include <utils/layoutbuilder.h>
#include <utils/networkaccessmanager.h>
#include <utils/pathchooser.h>
#include <utils/qtcprocess.h>
#include <utils/qtcassert.h>
#include <utils/qtcprocess.h>
#include <utils/stylehelper.h>
#include <utils/infolabel.h>
#include <utils/summaryaspect.h>
#include <utils/widgets.h>

#include <QCheckBox>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QGroupBox>
#include <QGuiApplication>
#include <QList>
#include <QListWidget>
#include <QLoggingCategory>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QProgressDialog>
#include <QPushButton>
#include <QStandardPaths>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

using namespace Utils;
using namespace QtTaskTree;

namespace ProjectExplorer::Internal {

static Q_LOGGING_CATEGORY(windowssettingswidget, "qtc.windows.windowssettingswidget", QtWarningMsg);

WindowsAppSdkSettings &windowsAppSdkSettings()
{
    static WindowsAppSdkSettings theWindowsConfigurations;
    return theWindowsConfigurations;
}

// The page's behaviour: what the three paths have to satisfy, and the two
// downloads that fill them in. Not a widget - the form is QML - so it holds
// no controls, only the work.
class WindowsAppSdkSettingsPrivate : public QObject
{
public:
    explicit WindowsAppSdkSettingsPrivate(WindowsAppSdkSettings *settings)
        : q(settings)
    {}

    GroupItem downloadNugetRecipe();
    void downloadNuget();
    void downloadWindowsAppSdk();

    void validateDownloadPath();
    void validateNuget();
    void validateWindowsAppSdk();

    WindowsAppSdkSettings *q = nullptr;
    QNetworkAccessManager manager;
    QSingleTaskTreeRunner m_nugetDownloader;
};

bool hasWindowsAppSdkPackage(const FilePath &directory)
{
    return !QDir(directory.path()).entryList({"Microsoft.WindowsAppSDK.*.nupkg"}).isEmpty();
}

FilePath windowsAppSdkPackageDir(const FilePath &downloadPath)
{
    QDir dir(downloadPath.path());
    const QStringList unpacked = dir.entryList({"Microsoft.WindowsAppSDK.*"});
    if (unpacked.isEmpty())
        return {};
    dir.cd(unpacked.first());
    return FilePath::fromString(dir.path());
}

WindowsAppSdkSettings::WindowsAppSdkSettings()
    : summary(this,
              {{DownloadPathExistsRow, Tr::tr("Download path exists.")},
               {NugetPathExistsRow, Tr::tr("NuGet path exists.")},
               {WindowsAppSdkPathExists, Tr::tr("Windows App SDK path exists.")}},
              Tr::tr("Windows App SDK settings are OK."),
              Tr::tr("Windows App SDK settings have errors."))
    , d(new WindowsAppSdkSettingsPrivate(this))
{
    setSettingsGroup("WindowsConfigurations");
    setQmlSource(QUrl("qrc:/qt/qml/QtCreator/ProjectExplorer/WindowsAppSdkPage.qml"));

    downloadLocation.setSettingsKey("DownloadLocation");
    downloadLocation.setQmlName("DownloadLocation");
    downloadLocation.setLabelText(Tr::tr("Download location:"));
    downloadLocation.setToolTip(
        Tr::tr("Select the download path of NuGet and Windows App SDK."));
    downloadLocation.setPromptDialogTitle(Tr::tr("Select Download Path"));
    downloadLocation.setExpectedKind(PathChooserKind::ExistingDirectory);

    nugetLocation.setSettingsKey("NugetLocation");
    nugetLocation.setQmlName("NugetLocation");
    nugetLocation.setLabelText(Tr::tr("NuGet location:"));
    nugetLocation.setToolTip(Tr::tr("Select the path of NuGet."));
    nugetLocation.setPromptDialogTitle(Tr::tr("Select nuget.exe File"));
    nugetLocation.setExpectedKind(PathChooserKind::Any);

    windowsAppSdkLocation.setSettingsKey("WindowsAppSDKLocation");
    windowsAppSdkLocation.setQmlName("WindowsAppSdkLocation");
    windowsAppSdkLocation.setLabelText(Tr::tr("Windows App SDK location:"));
    windowsAppSdkLocation.setToolTip(Tr::tr("Select the path of the Windows App SDK."));
    windowsAppSdkLocation.setPromptDialogTitle(Tr::tr("Select Windows App SDK Path"));

    downloadNuget.setQmlName("DownloadNuget");
    downloadNuget.setActionText(Tr::tr("Download NuGet"));
    downloadNuget.setToolTip(Tr::tr("Automatically download NuGet.\n\n"
                                    "NuGet is needed for downloading Windows App SDK."));
    downloadNuget.setAction([this] { d->downloadNuget(); });

    downloadWindowsAppSdk.setQmlName("DownloadWindowsAppSdk");
    downloadWindowsAppSdk.setActionText(Tr::tr("Download Windows App SDK"));
    downloadWindowsAppSdk.setToolTip(
        Tr::tr("Automatically download Windows App SDK with NuGet.\n\n"
               "If the automatic download fails, Qt Creator proposes to open the download URL\n"
               "in the system browser for manual download."));
    downloadWindowsAppSdk.setAction([this] { d->downloadWindowsAppSdk(); });

    summary.setQmlName("Summary");

    // The summary reports on what is typed, not on what was last saved, so
    // every edit re-runs the check it belongs to.
    downloadLocation.addOnChanged(this, [this] { d->validateDownloadPath(); });
    nugetLocation.addOnChanged(this, [this] { d->validateNuget(); });
    windowsAppSdkLocation.addOnChanged(this, [this] { d->validateWindowsAppSdk(); });

    AspectContainer::readSettings();

    if (downloadLocation().isEmpty()) {
        QString path = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation)
                       + QStringLiteral("/WindowsAppSDK");
        downloadLocation.setValue(path);
    }

    if (windowsAppSdkLocation().isEmpty()) {
        windowsAppSdkLocation.setValue(FilePath::fromUserInput(
            Environment::systemEnvironment().value(Constants::WINDOWS_WINAPPSDK_ROOT_ENV_KEY)));
    }
    // Nowhere else to look: the SDK is downloaded into the download path, so
    // that is where it will be.
    if (windowsAppSdkLocation().isEmpty())
        windowsAppSdkLocation.setValue(downloadLocation());

    // The registrations above turn auto-apply back on, and a settings page
    // that auto-applies never writes anything.
    setAutoApply(false);
    validate();
}

WindowsAppSdkSettings::~WindowsAppSdkSettings() = default;

void WindowsAppSdkSettings::validate()
{
    d->validateDownloadPath();
    d->validateNuget();
    d->validateWindowsAppSdk();
}

static bool isHttpRedirect(QNetworkReply *reply)
{
    const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    return statusCode == 301 || statusCode == 302 || statusCode == 303 || statusCode == 305
           || statusCode == 307 || statusCode == 308;
}

// TODO: Make it a separate async task in a chain?
static std::optional<QString> saveToDisk(const FilePath &filename, QIODevice *data)
{
    const Result<qint64> result = filename.writeFileContents(data->readAll());
    if (!result) {
        return Tr::tr("Could not open \"%1\" for writing: %2.")
        .arg(filename.toUserOutput(), result.error());
    }

    return {};
}


void WindowsAppSdkSettingsPrivate::validateDownloadPath()
{
    q->summary.setPointValid(DownloadPathExistsRow,
                             q->downloadLocation.expandedVolatileValue().exists());
}

void WindowsAppSdkSettingsPrivate::validateNuget()
{
    q->summary.setPointValid(NugetPathExistsRow,
                             q->nugetLocation.expandedVolatileValue().exists());
}

void WindowsAppSdkSettingsPrivate::validateWindowsAppSdk()
{
    // Not that the directory is there - that it has the SDK in it.
    q->summary.setPointValid(WindowsAppSdkPathExists,
                             hasWindowsAppSdkPackage(q->windowsAppSdkLocation.expandedVolatileValue()));
}

GroupItem WindowsAppSdkSettingsPrivate::downloadNugetRecipe()
{
    const FilePath downloadPath = q->downloadLocation.expandedVolatileValue();
    const QString nugetUrl("https://dist.nuget.org/win-x86-commandline/latest/nuget.exe");

    const auto failDialog = [=](const QString &msgSuffix = {}) {
        QStringList sl;
        sl << Tr::tr("NuGet download failed.");
        if (!msgSuffix.isEmpty())
            sl << msgSuffix;
        sl << Tr::tr("Open NuGet URL for manual download?");
        QMessageBox msgBox;
        msgBox.setText(sl.join(" "));
        msgBox.addButton(Tr::tr("Cancel"), QMessageBox::RejectRole);
        QAbstractButton *openButton = msgBox.addButton(Tr::tr("Open Download URL"),
                                                       QMessageBox::ActionRole);
        msgBox.exec();

        if (msgBox.clickedButton() == openButton)
            QDesktopServices::openUrl(QUrl::fromUserInput("https://www.nuget.org/downloads"));
        openButton->deleteLater();
    };

    struct StorageStruct
    {
        StorageStruct() {
            progressDialog.reset(createProgressDialog(100, Tr::tr("Downloading"),
                                                      Tr::tr("Downloading NuGet...")));
        }
        std::unique_ptr<QProgressDialog> progressDialog;
        std::optional<FilePath> fileName;
    };

    Storage<StorageStruct> storage;

    const auto onSetup = [downloadPath, failDialog] {
        if (downloadPath.isEmpty()) {
            failDialog(Tr::tr("The SDK Tools download URL is empty."));
            return SetupResult::StopWithError;
        }
        return SetupResult::Continue;
    };

    const auto onQuerySetup = [storage, nugetUrl, failDialog](QNetworkReplyWrapper &query) {
        query.setRequest(QNetworkRequest(QUrl(nugetUrl)));
        query.setNetworkAccessManager(NetworkAccessManager::instance());
        QProgressDialog *progressDialog = storage->progressDialog.get();
        QObject::connect(&query, &QNetworkReplyWrapper::downloadProgress,
                         progressDialog, [progressDialog](qint64 received, qint64 max) {
                             progressDialog->setRange(0, max);
                             progressDialog->setValue(received);
                         });
#if QT_CONFIG(ssl)
        QObject::connect(&query, &QNetworkReplyWrapper::sslErrors,
                         &query, [queryPtr = &query, failDialog](const QList<QSslError> &sslErrs) {
                             for (const QSslError &error : sslErrs)
                                 qCDebug(windowssettingswidget, "SSL error: %s\n",
                                         qPrintable(error.errorString()));
                             failDialog(Tr::tr("Encountered SSL errors, download is aborted."));
                             queryPtr->reply()->abort();
                         });
#endif
    };
    const auto onQueryDone = [this,
                              storage,
                              failDialog,
                              downloadPath](const QNetworkReplyWrapper &query, DoneWith result) {
        if (result == DoneWith::Cancel)
            return;

        QNetworkReply *reply = query.reply();
        QTC_ASSERT(reply, return);
        const QUrl url = reply->url();
        if (result != DoneWith::Success) {
            failDialog(Tr::tr("Downloading NuGet from URL %1 has failed: %2.")
                         .arg(url.toString(), reply->errorString()));
            return;
        }
        if (isHttpRedirect(reply)) {
            failDialog(Tr::tr("Download from %1 was redirected.").arg(url.toString()));
            return;
        }
        const QString path = url.path();
        QString basename = QFileInfo(path).fileName();
        const FilePath fileName = downloadPath / basename;
        const std::optional<QString> saveResult = saveToDisk(fileName, reply);
        if (saveResult) {
            failDialog(*saveResult);
            return;
        }
        storage->fileName = fileName;
        q->nugetLocation.setVolatileValue(fileName.toUrlishString());
    };
    const auto onCancelSetup = [storage] { return makeObjectSignal(storage->progressDialog.get(),
                                                                   &QProgressDialog::canceled); };

    return Group {
        storage,
        Group {
            onGroupSetup(onSetup),
            QNetworkReplyWrapperTask(onQuerySetup, onQueryDone),
        }.withCancel(onCancelSetup)
    };
}

void WindowsAppSdkSettingsPrivate::downloadNuget()
{
    const FilePath downloadPath = q->downloadLocation.expandedVolatileValue();
    const FilePath nugetPath = q->nugetLocation.expandedVolatileValue();
    const QString nugetDownloadingTitle(Tr::tr("Downloading"));

    if (nugetPath.exists() && nugetPath.isFile() && !nugetPath.isEmpty()) {
        QMessageBox::information(
            nullptr,
            nugetDownloadingTitle,
            Tr::tr(
                "The selected download path (%1) for NuGet already exists.\n"
                "Select a different path.")
                .arg(nugetPath.toUserOutput()));
        return;
    }

    if (!q->summary.rowsOk({DownloadPathExistsRow}) &&
        !downloadPath.isEmpty()) {
        downloadPath.ensureWritableDir();
        validateDownloadPath();
    }

    if (!q->summary.rowsOk({DownloadPathExistsRow})) {
        QMessageBox::information(nullptr, nugetDownloadingTitle,
                                 Tr::tr("Download path is not configured."));
        return;
    }

    m_nugetDownloader.start({downloadNugetRecipe()}, {}, [this](DoneWith result) {
        if (result != DoneWith::Success)
            return;
        validateNuget();
        // After downloading, the path exists; the page applies it as usual.
        q->apply();
        q->writeSettings();
    });
}

void WindowsAppSdkSettingsPrivate::downloadWindowsAppSdk()
{
    const FilePath downloadPath = q->downloadLocation.expandedVolatileValue();
    const FilePath winAppSdkPath = q->windowsAppSdkLocation.expandedVolatileValue();
    const FilePath nugetPath = q->nugetLocation.expandedVolatileValue();
    const QString winAppSdkDownloadTitle(Tr::tr("Downloading Windows App SDK"));
    const QString winAppSdkDownloadUrl = "https://learn.microsoft.com/en-us/windows/apps/windows-app-sdk/downloads";

    if (q->summary.rowsOk({WindowsAppSdkPathExists})) {
        QMessageBox::information(nullptr, winAppSdkDownloadTitle,
            Tr::tr("Windows App SDK is already configured."));
        return;
    }

    if (!q->summary.rowsOk({DownloadPathExistsRow}) &&
        !downloadPath.isEmpty()) {
        downloadPath.ensureWritableDir();
        validateDownloadPath();
    }

    if (!q->summary.rowsOk({DownloadPathExistsRow})) {
        QMessageBox::information(nullptr, winAppSdkDownloadTitle,
                                 Tr::tr("Download path is not configured."));
        return;
    }

    QProgressDialog *winAppSdkProgressDialog
        = new QProgressDialog(Tr::tr("Downloading Windows App SDK..."),
                              Tr::tr("Cancel"), 0, 0);
    winAppSdkProgressDialog->setWindowModality(Qt::ApplicationModal);
    winAppSdkProgressDialog->setWindowTitle(winAppSdkDownloadTitle);
    winAppSdkProgressDialog->setFixedSize(winAppSdkProgressDialog->sizeHint());

    const QString winAppSdkLibraryName("Microsoft.WindowsAppSDK");
    Process *nugetDownloader = new Process(this);
    const CommandLine gitCloneCommand(nugetPath, {"install",
                                                  winAppSdkLibraryName,
                                                  "-OutputDirectory",
                                                  downloadPath.path()});
    nugetDownloader->setCommand(gitCloneCommand);

    qCDebug(windowssettingswidget) << "Downloading Windows App SDK: "
                                   << gitCloneCommand.toUserOutput();

    connect(winAppSdkProgressDialog,
            &QProgressDialog::canceled, nugetDownloader, &QObject::deleteLater);

    const auto failDialog = [=](const QString &msgSuffix = {}) {
        QStringList sl;
        sl << Tr::tr("Windows App SDK download failed.");
        if (!msgSuffix.isEmpty())
            sl << msgSuffix;
        sl << Tr::tr("Open Windows App SDK URL for manual download?");
        QMessageBox msgBox;
        msgBox.setText(sl.join(" "));
        msgBox.addButton(Tr::tr("Cancel"), QMessageBox::RejectRole);
        QAbstractButton *openButton = msgBox.addButton(Tr::tr("Open Download URL"),
                                                       QMessageBox::ActionRole);
        msgBox.exec();

        if (msgBox.clickedButton() == openButton)
            QDesktopServices::openUrl(QUrl::fromUserInput(winAppSdkDownloadUrl));
        openButton->deleteLater();
    };

    connect(nugetDownloader,
            &Process::done,
            this,
            [this, winAppSdkProgressDialog, nugetDownloader, failDialog, downloadPath] {
        winAppSdkProgressDialog->close();
        if (nugetDownloader->error() != ProcessError::UnknownError) {
            if (nugetDownloader->error() == ProcessError::FailedToStart) {
                failDialog();
                return;
            } else {
                failDialog();
            }
        }
        // Where NuGet put it, which is not where it was told to put it: the
        // package unpacks into a versioned directory of its own.
        const FilePath unpacked = windowsAppSdkPackageDir(downloadPath);
        if (!unpacked.isEmpty())
            q->windowsAppSdkLocation.setVolatileValue(unpacked.toUrlishString());
        validateWindowsAppSdk(); // After unpacking, the path exists
        nugetDownloader->deleteLater();

        if (!winAppSdkProgressDialog->wasCanceled()
                || nugetDownloader->result() == ProcessResult::FinishedWithError) {
            failDialog();
        }
        // The SDK is on disk now; the page saves where it went.
        q->apply();
        q->writeSettings();
    });

    winAppSdkProgressDialog->show();
    nugetDownloader->start();
}

// WindowsSettingsPage

class WindowsSettingsPage final : public Core::IOptionsPage
{
public:
    WindowsSettingsPage()
    {
        setId(Constants::WINDOWS_SETTINGS_ID);
        setDisplayName(Tr::tr("Windows App SDK"));
        setCategory(Constants::SDK_SETTINGS_CATEGORY);
        setSettingsProvider([] { return &windowsAppSdkSettings(); });
    }
};

void setupWindowsAppSdkSettings()
{
    if (!HostOsInfo::isWindowsHost())
        return;

    static WindowsSettingsPage theWindowsSettingsPage;

    (void) windowsAppSdkSettings();
}

} // namespace ProjectExplorer::Internal
