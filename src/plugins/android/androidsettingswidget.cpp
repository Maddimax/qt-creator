// Copyright (C) 2016 BogDan Vatra <bog_dan_ro@yahoo.com>
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "androidconfigurations.h"
#include "androidconstants.h"
#include "androidsdkdownloader.h"
#include "androidsdkmanager.h"
#include "androidsdkmanagerdialog.h"
#include "androidsettingswidget.h"
#include "androidtr.h"

#include <coreplugin/dialogs/ioptionspage.h>
#include <coreplugin/icore.h>
#include <coreplugin/messagemanager.h>

#include <projectexplorer/devicesupport/devicekitaspects.h>
#include <projectexplorer/kitmanager.h>
#include <projectexplorer/projectexplorerconstants.h>

#include <qtsupport/qtversionmanager.h>

#include <QtTaskTree/QSingleTaskTreeRunner>

#include <utils/filedialogs.h>
#include <utils/algorithm.h>
#include <utils/async.h>
#include <utils/detailswidget.h>
#include <utils/fileutils.h>
#include <utils/guiutils.h>
#include <utils/hostosinfo.h>
#include <utils/layoutbuilder.h>
#include <utils/pathchooser.h>
#include <utils/qtcprocess.h>
#include <utils/shutdownguard.h>
#include <utils/summaryaspect.h>
#include <utils/stylehelper.h>
#include <utils/summarywidget.h>
#include <utils/utilsicons.h>

#include <QCheckBox>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QGroupBox>
#include <QGuiApplication>
#include <QList>
#include <QStandardItemModel>

#ifdef WITH_TESTS
#include <QTest>
#endif
#include <QLoggingCategory>
#include <QMessageBox>
#include <QProgressDialog>
#include <QPushButton>
#include <QStandardPaths>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

using namespace QtTaskTree;
using namespace Utils;

namespace Android::Internal {

static Q_LOGGING_CATEGORY(androidsettingswidget, "qtc.android.androidsettingswidget", QtWarningMsg);
constexpr int requiredJavaMajorVersion = 21;

// The NDKs, as the page lists them: where each one is and where it came from.
// The lock icon and the italic font the list used to carry both said something
// a cell cannot draw, so they are words in a second column.
class NdkModel final : public QStandardItemModel
{
public:
    NdkModel()
    {
        setHorizontalHeaderLabels({Tr::tr("NDK"), Tr::tr("Source")});
    }

    QVariant data(const QModelIndex &index, int role) const override
    {
        // A widget view reads flags() for these two; a Qt Quick view cannot.
        if (role == AspectTable::EditableRole || role == AspectTable::CheckableRole)
            return false;
        return QStandardItemModel::data(index, role);
    }

    QHash<int, QByteArray> roleNames() const override
    {
        return AspectTable::withRoleNames(QStandardItemModel::roleNames());
    }
};

class NdkListAspect final : public BaseAspect
{
    Q_OBJECT

public:
    using BaseAspect::BaseAspect;

    AspectPresentation presentation() const override
    {
        AspectPresentation p = BaseAspect::presentation();
        p.control = AspectControls::Tree;
        return p;
    }

    QAbstractItemModel *tableModel() override { return &m_model; }
    NdkModel &model() { return m_model; }

    Q_INVOKABLE void setCurrentIndex(const QModelIndex &index)
    {
        if (index.row() == m_currentRow)
            return;
        m_currentRow = index.isValid() ? index.row() : -1;
        emit currentChanged();
    }

    int currentRow() const { return m_currentRow; }

    // The NDK the buttons act on, or an empty path when none is picked.
    FilePath currentNdk() const
    {
        if (m_currentRow < 0 || m_currentRow >= m_model.rowCount())
            return {};
        return FilePath::fromUserInput(m_model.item(m_currentRow, 0)->data(PathRole).toString());
    }

    bool currentIsCustom() const
    {
        if (m_currentRow < 0 || m_currentRow >= m_model.rowCount())
            return false;
        return m_model.item(m_currentRow, 0)->data(CustomRole).toBool();
    }

    static constexpr int PathRole = Qt::UserRole + 100;
    static constexpr int CustomRole = Qt::UserRole + 101;

signals:
    void currentChanged();

private:
    NdkModel m_model;
    int m_currentRow = -1;
};

class AndroidSettingsWidget final : public AspectContainer
{
public:
    AndroidSettingsWidget();

    // Applying the page is what creates and updates the Android kits.
    void apply() final
    {
        AspectContainer::apply();
        AndroidConfigurations::applyConfig();
    }

private:
    void validateJdk();
    void updateNdkList();
    void onSdkPathChanged();
    void validateSdk();
    void downloadOpenSslRepo(const bool silent = false);
    void updateUI();
    void downloadSdk();
    void addCustomNdkItem();
    bool isDefaultNdkSelected() const;
    void validateOpenSsl();

    QSingleTaskTreeRunner m_sdkDownloader;
    bool m_isInitialReloadDone = false;

    TextDisplay m_info{this};

    AspectContainer m_androidGroup{this};
    FilePathAspect m_openJdkLocation{&m_androidGroup};
    ActionAspect m_downloadOpenJdk{&m_androidGroup};
    FilePathAspect m_sdkLocation{&m_androidGroup};
    ActionAspect m_setUpSdk{&m_androidGroup};
    ActionAspect m_downloadSdkTools{&m_androidGroup};
    ActionAspect m_sdkManager{&m_androidGroup};
    NdkListAspect m_ndkList{&m_androidGroup};
    ActionAspect m_addNdk{&m_androidGroup};
    ActionAspect m_removeNdk{&m_androidGroup};
    ActionAspect m_makeDefaultNdk{&m_androidGroup};
    ActionAspect m_downloadNdk{&m_androidGroup};
    SummaryAspect m_androidSummary;
    BoolAspect m_createKit{&m_androidGroup};

    AspectContainer m_openSslGroup{this};
    FilePathAspect m_openSslLocation{&m_openSslGroup};
    ActionAspect m_downloadOpenSsl{&m_openSslGroup};
    SummaryAspect m_openSslSummary;
};

enum AndroidValidation {
    JavaPathExistsAndWritableRow,
    SdkPathExistsAndWritableRow,
    SdkToolsInstalledRow,
    SdkManagerSuccessfulRow,
    PlatformToolsInstalledRow,
    PlatformSdkInstalledRow,
    BuildToolsInstalledRow,
    AllEssentialsInstalledRow,
};

enum OpenSslValidation {
    OpenSslPathExistsRow,
    OpenSslPriPathExists,
    OpenSslCmakeListsPathExists
};

static Result<> testJavaC(const FilePath &jdkPath)
{
    if (!jdkPath.isReadableDir())
        return ResultError(Tr::tr("The selected path does not exist or is not readable."));

    const QString javacCommand("javac");
    const QString versionParameter("-version");
    const FilePath bin = jdkPath / "bin" / (javacCommand + QTC_HOST_EXE_SUFFIX);

    if (!bin.isExecutableFile())
        return ResultError(
            Tr::tr("Could not find \"%1\" in the selected path.")
                .arg(bin.toUserOutput()));

    QVersionNumber jdkVersion;

    Process javacProcess;
    const CommandLine cmd(bin, {versionParameter});
    javacProcess.setProcessChannelMode(ProcessChannelMode::MergedChannels);
    javacProcess.setCommand(cmd);
    javacProcess.runBlocking();

    const QString stdOut = javacProcess.stdOut().trimmed();

    if (javacProcess.exitCode() != 0)
        return ResultError(
            Tr::tr("The selected path does not contain a valid JDK. (%1 failed: %2)")
                .arg(cmd.toUserOutput(), stdOut));

    // We expect "javac <version>" where <version> is "major.minor.patch"
    const QString outputPrefix = javacCommand + " ";
    if (!stdOut.startsWith(outputPrefix))
        return ResultError(Tr::tr("Unexpected output from \"%1\": %2")
                                   .arg(cmd.toUserOutput(), stdOut));

    jdkVersion = QVersionNumber::fromString(stdOut.mid(outputPrefix.size()).split('\n').first());

    if (jdkVersion.isNull() /* || jdkVersion.majorVersion() != requiredJavaMajorVersion */ ) {
        return ResultError(Tr::tr("Unsupported JDK version (needs to be %1): %2 (parsed: %3)")
                                   .arg(requiredJavaMajorVersion)
                                   .arg(stdOut, jdkVersion.toString()));
    }

    return {};
}

static bool androidQtVersionsInstalledButNoKits();

AndroidSettingsWidget::AndroidSettingsWidget()
    : m_androidSummary(&m_androidGroup,
                       {{JavaPathExistsAndWritableRow, Tr::tr("JDK path exists and is writable.")},
                        {SdkPathExistsAndWritableRow,
                         Tr::tr("Android SDK path exists and is writable.")},
                        {SdkToolsInstalledRow, Tr::tr("Android SDK Command-line Tools installed.")},
                        {SdkManagerSuccessfulRow, Tr::tr("Android SDK Command-line Tools runs.")},
                        {PlatformToolsInstalledRow, Tr::tr("Android SDK Platform-Tools installed.")},
                        {AllEssentialsInstalledRow,
                         Tr::tr("All essential packages installed for all installed Qt versions.")},
                        {BuildToolsInstalledRow, Tr::tr("Android SDK Build-Tools installed.")},
                        {PlatformSdkInstalledRow, Tr::tr("Android Platform SDK (version) installed.")}},
                       Tr::tr("Android settings are OK."),
                       Tr::tr("Android settings have errors."))
    , m_openSslSummary(&m_openSslGroup,
                       {{OpenSslPathExistsRow, Tr::tr("OpenSSL path exists.")},
                        {OpenSslPriPathExists,
                         Tr::tr("QMake include project (openssl.pri) exists.")},
                        {OpenSslCmakeListsPathExists,
                         Tr::tr("CMake include project (CMakeLists.txt) exists.")}},
                       Tr::tr("OpenSSL Settings are OK."),
                       Tr::tr("OpenSSL settings have errors."))
{
    setQmlSource(QUrl("qrc:/qt/qml/QtCreator/Android/AndroidSettingsPage.qml"));

    m_info.setQmlName("Info");
    m_info.setText(Tr::tr("All changes on this page take effect immediately."));
    m_info.setIconType(InfoType::Information);

    m_androidGroup.setQmlName("Android");
    m_androidGroup.setLabelText(Tr::tr("Android Settings"));

    m_openJdkLocation.setQmlName("JdkLocation");
    m_openJdkLocation.setLabelText(Tr::tr("JDK location:"));
    m_openJdkLocation.setExpectedKind(PathChooserKind::ExistingDirectory);
    m_openJdkLocation.setPromptDialogTitle(Tr::tr("Select JDK Path"));
    m_openJdkLocation.setValidationFunction([](const QString &s) {
        return Utils::asyncRun([s]() -> Result<QString> {
            Result<> test = testJavaC(FilePath::fromUserInput(s));
            if (!test) {
                Core::MessageManager::writeSilently(test.error());
                return ResultError(test.error());
            }
            return s;
        });
    });

    m_downloadOpenJdk.setQmlName("DownloadJdk");
    m_downloadOpenJdk.setActionText(Tr::tr("Download JDK"));
    m_downloadOpenJdk.setToolTip(Tr::tr("Open JDK download URL in the system's browser."));
    m_downloadOpenJdk.setAction([] {
        QDesktopServices::openUrl(QUrl::fromUserInput(
            QString("https://adoptium.net/temurin/releases/?package=jdk&version=%1")
                .arg(requiredJavaMajorVersion)));
    });

    m_sdkLocation.setQmlName("SdkLocation");
    m_sdkLocation.setLabelText(Tr::tr("Android SDK location:"));
    m_sdkLocation.setExpectedKind(PathChooserKind::ExistingDirectory);
    m_sdkLocation.setPromptDialogTitle(Tr::tr("Select Android SDK Folder"));

    m_setUpSdk.setQmlName("SetUpSdk");
    m_setUpSdk.setActionText(Tr::tr("Set Up SDK"));
    m_setUpSdk.setToolTip(
        Tr::tr("Automatically download Android SDK Tools to selected location.\n\n"
               "If the selected path contains no valid SDK Tools, the SDK Tools package is downloaded\n"
               "from %1,\n"
               "and extracted to the selected path.\n"
               "After the SDK Tools are properly set up, you are prompted to install any essential\n"
               "packages required for Qt to build for Android.")
            .arg(AndroidConfig::sdkToolsUrl().toString()));
    m_setUpSdk.setAction([this] { downloadSdk(); });

    m_downloadSdkTools.setQmlName("DownloadSdk");
    m_downloadSdkTools.setActionText(Tr::tr("Download SDK"));
    m_downloadSdkTools.setToolTip(Tr::tr("Open Android SDK download URL in the system's browser."));
    m_downloadSdkTools.setAction([] {
        QDesktopServices::openUrl(QUrl::fromUserInput(
            "https://developer.android.com/studio#command-line-tools-only"));
    });

    m_sdkManager.setQmlName("SdkManager");
    m_sdkManager.setActionText(Tr::tr("SDK Manager"));
    m_sdkManager.setAction([] { executeAndroidSdkManagerDialog(); });

    m_ndkList.setQmlName("NdkList");
    m_ndkList.setLabelText(Tr::tr("Android NDK list:"));

    m_addNdk.setQmlName("AddNdk");
    m_addNdk.setActionText(Tr::tr("Add..."));
    m_addNdk.setToolTip(Tr::tr("Add the selected custom NDK. The toolchains "
                               "and debuggers will be created automatically."));
    m_addNdk.setAction([this] { addCustomNdkItem(); });

    m_removeNdk.setQmlName("RemoveNdk");
    m_removeNdk.setActionText(Tr::tr("Remove"));
    m_removeNdk.setToolTip(Tr::tr("Remove the selected NDK if it has been added manually."));
    m_removeNdk.setEnabled(false);
    m_removeNdk.setAction([this] {
        const FilePath ndk = m_ndkList.currentNdk();
        if (ndk.isEmpty())
            return;
        if (isDefaultNdkSelected())
            AndroidConfig::setDefaultNdk({});
        AndroidConfig::removeCustomNdk(ndk);
        updateNdkList();
    });

    m_makeDefaultNdk.setQmlName("MakeDefaultNdk");
    m_makeDefaultNdk.setActionText(Tr::tr("Make Default"));
    m_makeDefaultNdk.setToolTip(Tr::tr("Force a specific NDK installation to be used by all "
                                       "Android kits.<br/>Note that the forced NDK might not "
                                       "be compatible with all registered Qt versions."));
    m_makeDefaultNdk.setAction([this] {
        AndroidConfig::setDefaultNdk(isDefaultNdkSelected() ? FilePath() : m_ndkList.currentNdk());
        updateNdkList();
    });

    m_downloadNdk.setQmlName("DownloadNdk");
    m_downloadNdk.setActionText(Tr::tr("Download NDK"));
    m_downloadNdk.setToolTip(Tr::tr("Open Android NDK download URL in the system's browser."));
    m_downloadNdk.setAction([] {
        QDesktopServices::openUrl(QUrl::fromUserInput("https://developer.android.com/ndk/downloads/"));
    });

    m_androidSummary.setQmlName("AndroidSummary");

    m_createKit.setQmlName("CreateKit");
    m_createKit.setLabel(Tr::tr("Automatically create kits for Android tool chains"));
    m_createKit.setLabelPlacement(BoolAspect::LabelPlacement::Compact);
    m_createKit.setValue(AndroidConfig::automaticKitCreation());

    m_openSslGroup.setQmlName("OpenSsl");
    m_openSslGroup.setLabelText(Tr::tr("Android OpenSSL Settings (Optional)"));

    m_openSslLocation.setQmlName("OpenSslLocation");
    m_openSslLocation.setLabelText(Tr::tr("OpenSSL binaries location:"));
    m_openSslLocation.setExpectedKind(PathChooserKind::ExistingDirectory);
    m_openSslLocation.setPromptDialogTitle(Tr::tr("Select OpenSSL Include Project File"));
    m_openSslLocation.setToolTip(Tr::tr("Select the path of the prebuilt OpenSSL binaries."));

    m_downloadOpenSsl.setQmlName("DownloadOpenSsl");
    m_downloadOpenSsl.setActionText(Tr::tr("Download OpenSSL"));
    m_downloadOpenSsl.setToolTip(
        Tr::tr("Automatically download OpenSSL prebuilt libraries.\n\n"
               "These libraries can be shipped with your application if any SSL operations\n"
               "are performed. Find the checkbox under \"Projects > Build > Build Steps >\n"
               "Build Android APK > Additional Libraries\".\n"
               "If the automatic download fails, Qt Creator proposes to open the download URL\n"
               "in the system's browser for manual download."));
    m_downloadOpenSsl.setAction([this] { downloadOpenSslRepo(); });

    m_openSslSummary.setQmlName("OpenSslSummary");

    if (AndroidConfig::openJDKLocation().isEmpty())
        AndroidConfig::setOpenJDKLocation(AndroidConfig::getJdkPath());
    m_openJdkLocation.setValue(AndroidConfig::openJDKLocation());

    if (AndroidConfig::sdkLocation().isEmpty())
        AndroidConfig::setSdkLocation(AndroidConfig::defaultSdkPath());
    m_sdkLocation.setValue(AndroidConfig::sdkLocation());

    if (AndroidConfig::openSslLocation().isEmpty())
        AndroidConfig::setOpenSslLocation(AndroidConfig::sdkLocation() / "android_openssl");
    m_openSslLocation.setValue(AndroidConfig::openSslLocation());

    // Behaviour, not layout.
    connect(&m_openJdkLocation, &BaseAspect::changed, this, &AndroidSettingsWidget::validateJdk);
    connect(&m_sdkLocation, &BaseAspect::changed, this, &AndroidSettingsWidget::onSdkPathChanged);
    connect(&m_openSslLocation, &BaseAspect::changed,
            this, &AndroidSettingsWidget::validateOpenSsl);
    connect(&m_createKit, &BaseAspect::changed, this, [this] {
        AndroidConfig::setAutomaticKitCreation(m_createKit());
    });
    connect(&m_ndkList, &NdkListAspect::currentChanged, this, &AndroidSettingsWidget::updateUI);
    // Asking the SDK manager what it has costs a process run each time, so it
    // waits until the page is looked at rather than running for every census.
    connect(this, &AspectContainer::shown, this, [this] {
        if (AndroidConfig::sdkFullyConfigured() && androidQtVersionsInstalledButNoKits())
            markSettingsDirty();
        if (m_isInitialReloadDone)
            return;
        validateJdk();
        // Reloading SDK packages (force) is still synchronous. Use zero timer
        // to let the settings dialog open first.
        QTimer::singleShot(0, this, [this] {
            sdkManager().refreshPackages();
            validateSdk();
            // Validate SDK again after any change in SDK packages.
            connect(&sdkManager(), &AndroidSdkManager::packagesReloaded, this, [this] {
                m_androidSummary.setInProgressText("Packages reloaded");
                validateSdk();
            }, Qt::QueuedConnection); // Hack: Let AndroidSdkModel::refreshData() be called first,
                                      // otherwise the nested loop inside validateSdk() may trigger
                                      // the repaint for the old data, containing pointers
                                      // to the deleted packages. That's why we queue the signal.
        });
        validateOpenSsl();
        m_isInitialReloadDone = true;
    });

    updateUI();
}

static bool androidQtVersionsInstalledButNoKits()
{
    const bool qtForAndroidInstalled = !QtSupport::QtVersionManager::versions(
                                            &QtSupport::QtVersion::isAndroidQtVersion)
                                            .isEmpty();

    const bool qtForAndroidKitsConfigured = Utils::anyOf(
        ProjectExplorer::KitManager::kits(), [](ProjectExplorer::Kit *k) {
            return ProjectExplorer::RunDeviceTypeKitAspect::deviceTypeId(k)
            == Constants::ANDROID_DEVICE_TYPE;
        });

    return qtForAndroidInstalled && !qtForAndroidKitsConfigured;
}

void AndroidSettingsWidget::updateNdkList()
{
    DirtySettingsGuard suppressor;
    NdkModel &model = m_ndkList.model();
    model.removeRows(0, model.rowCount());

    const auto addRow = [&model](const FilePath &path, bool custom) {
        const bool isDefault = !AndroidConfig::defaultNdk().isEmpty()
                               && path == AndroidConfig::defaultNdk();
        // The list used to say both of these with a lock icon and an italic
        // font, neither of which a cell can draw.
        const auto pathItem = new QStandardItem(
            isDefault ? Tr::tr("%1 (default)").arg(path.toUserOutput()) : path.toUserOutput());
        pathItem->setData(path.toUserOutput(), NdkListAspect::PathRole);
        pathItem->setData(custom, NdkListAspect::CustomRole);
        model.appendRow({pathItem,
                         new QStandardItem(custom ? Tr::tr("Custom")
                                                  : Tr::tr("SDK Manager"))});
    };

    for (const Ndk *ndk : sdkManager().installedNdkPackages())
        addRow(ndk->installedLocation(), false);

    for (const FilePath &ndk : AndroidConfig::getCustomNdkList()) {
        if (AndroidConfig::isValidNdk(ndk))
            addRow(ndk, true);
        else
            AndroidConfig::removeCustomNdk(ndk);
    }

    updateUI();
}

void AndroidSettingsWidget::addCustomNdkItem()
{
    const FilePath homePath = FilePath::fromUserInput(QStandardPaths::standardLocations(QStandardPaths::HomeLocation)
            .constFirst());
    const FilePath ndkPath = FileUtils::getExistingDirectory(Tr::tr("Select an NDK"), homePath);

    if (AndroidConfig::isValidNdk(ndkPath)) {
        AndroidConfig::addCustomNdk(ndkPath);
        updateNdkList();
    } else if (!ndkPath.isEmpty()) {
        QMessageBox::warning(
                    Core::ICore::dialogParent(),
                    Tr::tr("Add Custom NDK"),
                    Tr::tr("The selected path has an invalid NDK. This might mean that the path contains space "
                           "characters, or that it does not have a \"toolchains\" sub-directory, or that the "
                           "NDK version could not be retrieved because of a missing \"source.properties\" or "
                           "\"RELEASE.TXT\" file"));
    }
}


bool AndroidSettingsWidget::isDefaultNdkSelected() const
{
    if (AndroidConfig::defaultNdk().isEmpty())
        return false;
    const FilePath current = m_ndkList.currentNdk();
    return !current.isEmpty() && current == AndroidConfig::defaultNdk();
}

void AndroidSettingsWidget::validateJdk()
{
    AndroidConfig::setOpenJDKLocation(m_openJdkLocation());
    Result<> test = testJavaC(AndroidConfig::openJDKLocation());

    m_androidSummary.setPointValid(JavaPathExistsAndWritableRow, test);

    updateUI();

    if (m_isInitialReloadDone)
        sdkManager().reloadPackages();
}

void AndroidSettingsWidget::validateOpenSsl()
{
    AndroidConfig::setOpenSslLocation(m_openSslLocation());

    m_openSslSummary.setPointValid(OpenSslPathExistsRow, AndroidConfig::openSslLocation().exists());

    const bool priFileExists = AndroidConfig::openSslLocation().pathAppended("openssl.pri").exists();
    m_openSslSummary.setPointValid(OpenSslPriPathExists, priFileExists);
    const bool cmakeListsExists
        = AndroidConfig::openSslLocation().pathAppended("CMakeLists.txt").exists();
    m_openSslSummary.setPointValid(OpenSslCmakeListsPathExists, cmakeListsExists);

    updateUI();
}

void AndroidSettingsWidget::onSdkPathChanged()
{
    const FilePath sdkPath = m_sdkLocation().cleanPath();
    AndroidConfig::setSdkLocation(sdkPath);
    FilePath currentOpenSslPath = AndroidConfig::openSslLocation();
    if (currentOpenSslPath.isEmpty() || !currentOpenSslPath.exists())
        currentOpenSslPath = sdkPath.pathAppended("android_openssl");
    m_openSslLocation.setValue(currentOpenSslPath);
    // Package reload will trigger validateSdk.
    sdkManager().refreshPackages();
}

void AndroidSettingsWidget::validateSdk()
{
    const FilePath sdkPath = m_sdkLocation().cleanPath();
    AndroidConfig::setSdkLocation(sdkPath);

    m_androidSummary.setPointValid(SdkPathExistsAndWritableRow,
                                    sdkPath.exists() && sdkPath.isWritableDir());
    m_androidSummary.setPointValid(SdkToolsInstalledRow,
                                    !AndroidConfig::sdkToolsVersion().isNull());
    m_androidSummary.setPointValid(SdkManagerSuccessfulRow, // TODO: track me
                                    sdkManager().packageListingSuccessful());
    m_androidSummary.setPointValid(PlatformToolsInstalledRow, // TODO: track me
                                    AndroidConfig::adbToolPath().exists());
    m_androidSummary.setPointValid(AllEssentialsInstalledRow,
                                    AndroidConfig::allEssentialsInstalled());
    m_androidSummary.setPointValid(BuildToolsInstalledRow,
                                    !AndroidConfig::buildToolsVersion().isNull());
    // installedSdkPlatforms should not trigger a package reload as validate SDK is only called
    // after AndroidSdkManager::packageReloadFinished.
    m_androidSummary.setPointValid(PlatformSdkInstalledRow,
                                    !sdkManager().installedSdkPlatforms().isEmpty());

    const bool sdkToolsOk = m_androidSummary.rowsOk({SdkPathExistsAndWritableRow,
                                                      SdkToolsInstalledRow,
                                                      SdkManagerSuccessfulRow});
    const bool componentsOk = m_androidSummary.rowsOk({PlatformToolsInstalledRow,
                                                        BuildToolsInstalledRow,
                                                        PlatformSdkInstalledRow,
                                                        AllEssentialsInstalledRow});
    AndroidConfig::setSdkFullyConfigured(sdkToolsOk && componentsOk);
    if (sdkToolsOk && !componentsOk) {
        const QStringList notFoundEssentials = sdkManager().notFoundEssentialSdkPackages();
        if (!notFoundEssentials.isEmpty()) {
            QMessageBox::warning(Core::ICore::dialogParent(),
                Tr::tr("Android SDK Changes"),
                Tr::tr("%1 cannot find the following essential packages: \"%2\".\n"
                       "Install them manually after the current operation is done.\n")
                    .arg(QGuiApplication::applicationDisplayName(),
                         notFoundEssentials.join("\", \"")));
        }
        QStringList missingPkgs = sdkManager().missingEssentialSdkPackages();
        // Add the a system image with highest API level only if there are other
        // essentials needed, so it would practicaly be somewhat optional.
        if (!missingPkgs.isEmpty()) {
            const QString sysImage = AndroidConfig::optionalSystemImagePackage();
            if (!sysImage.isEmpty())
                missingPkgs.append(sysImage);
        }
        sdkManager().runInstallationChange({missingPkgs},
            Tr::tr("Android SDK installation is missing necessary packages. "
                   "Do you want to install the missing packages?"));
    }

    updateNdkList();
    updateUI();
}

void AndroidSettingsWidget::downloadOpenSslRepo(const bool silent)
{
    const FilePath openSslPath = m_openSslLocation();
    const QString openSslCloneTitle(Tr::tr("OpenSSL Cloning"));

    if (m_openSslSummary.allRowsOk()) {
        if (!silent) {
            QMessageBox::information(Core::ICore::dialogParent(), openSslCloneTitle,
                Tr::tr("OpenSSL prebuilt libraries repository is already configured."));
        }
        return;
    }

    if (openSslPath.exists() && !openSslPath.isEmpty()) {
        QMessageBox::information(
            Core::ICore::dialogParent(),
            openSslCloneTitle,
            Tr::tr(
                "The selected download path (%1) for OpenSSL already exists and the directory is "
                "not empty. Select a different path or make sure it is an empty directory.")
                .arg(openSslPath.toUserOutput()));
        return;
    }

    QProgressDialog *openSslProgressDialog
        = new QProgressDialog(Tr::tr("Cloning OpenSSL prebuilt libraries..."),
                              Tr::tr("Cancel"), 0, 0);
    openSslProgressDialog->setWindowModality(Qt::ApplicationModal);
    openSslProgressDialog->setWindowTitle(openSslCloneTitle);
    openSslProgressDialog->setFixedSize(openSslProgressDialog->sizeHint());

    const QString openSslRepo("https://github.com/KDAB/android_openssl.git");
    Process *gitCloner = new Process(this);
    const CommandLine
        gitCloneCommand("git", {"clone", "--depth=1", openSslRepo, openSslPath.path()});
    gitCloner->setCommand(gitCloneCommand);

    qCDebug(androidsettingswidget) << "Cloning OpenSSL repo: " << gitCloneCommand.toUserOutput();

    connect(openSslProgressDialog, &QProgressDialog::canceled, gitCloner, &QObject::deleteLater);

    const auto failDialog = [openSslRepo](const QString &msgSuffix = {}) {
        QStringList sl;
        sl << Tr::tr("OpenSSL prebuilt libraries cloning failed.");
        if (!msgSuffix.isEmpty())
            sl << msgSuffix;
        sl << Tr::tr("Opening OpenSSL URL for manual download.");
        QMessageBox msgBox;
        msgBox.setText(sl.join(" "));
        msgBox.addButton(Tr::tr("Cancel"), QMessageBox::RejectRole);
        QAbstractButton *openButton = msgBox.addButton(Tr::tr("Open Download URL"), QMessageBox::ActionRole);
        msgBox.exec();

        if (msgBox.clickedButton() == openButton)
            QDesktopServices::openUrl(QUrl::fromUserInput(openSslRepo));
        openButton->deleteLater();
    };

    connect(gitCloner, &Process::done, this, [this, openSslProgressDialog, gitCloner, failDialog] {
        openSslProgressDialog->close();
        if (gitCloner->error() != ProcessError::UnknownError) {
            if (gitCloner->error() == ProcessError::FailedToStart) {
                failDialog(Tr::tr("The Git tool might not be installed properly on your system."));
                return;
            } else {
                failDialog();
            }
        }
        validateOpenSsl();

        if (!openSslProgressDialog->wasCanceled()
                || gitCloner->result() == ProcessResult::FinishedWithError) {
            failDialog();
        }
    });

    openSslProgressDialog->show();
    gitCloner->start();
}

void AndroidSettingsWidget::updateUI()
{
    const bool androidSetupOk = m_androidSummary.allRowsOk();

    const QString infoText = Tr::tr("(SDK Version: %1)")
            .arg(AndroidConfig::sdkToolsVersion().toString());
    m_androidSummary.setInfoText(androidSetupOk ? infoText : "");

    const bool haveCurrent = !m_ndkList.currentNdk().isEmpty();
    m_makeDefaultNdk.setEnabled(haveCurrent);
    m_makeDefaultNdk.setActionText(isDefaultNdkSelected() ? Tr::tr("Unset Default")
                                                          : Tr::tr("Make Default"));
    // Only an NDK that was added by hand can be taken away again.
    m_removeNdk.setEnabled(haveCurrent && m_ndkList.currentIsCustom());
}

void AndroidSettingsWidget::downloadSdk()
{
    if (AndroidConfig::sdkToolsOk()) {
        QMessageBox::warning(Core::ICore::dialogParent(), Android::Internal::dialogTitle(),
                             Tr::tr("The selected path already has a valid SDK Tools package."));
        validateSdk();
        return;
    }

    const QString message = Tr::tr("Download and install Android SDK Tools to %1?")
            .arg("\n\"" + m_sdkLocation().cleanPath().toUserOutput()
                 + "\"");
    if (QMessageBox::information(Core::ICore::dialogParent(), Android::Internal::dialogTitle(),
                                 message, QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes) {
        return;
    }
    m_sdkDownloader.start({Android::Internal::downloadSdkRecipe()}, {}, [this](DoneWith result) {
        if (result != DoneWith::Success)
            return;
        // Make sure the sdk path is created before installing packages
        const FilePath sdkPath = AndroidConfig::sdkLocation();
        if (!sdkPath.createDir()) {
            QMessageBox::warning(Core::ICore::dialogParent(), Android::Internal::dialogTitle(),
                                 Tr::tr("Failed to create the SDK Tools path %1.")
                                     .arg("\n\"" + sdkPath.toUserOutput() + "\""));
        }
        sdkManager().reloadPackages();
        updateUI();
        apply();

        connect(&sdkManager(), &AndroidSdkManager::packagesReloaded, this, [this] {
            downloadOpenSslRepo(true);
        }, Qt::SingleShotConnection);
    });
}

// AndroidSettingsPage

class AndroidSettingsPage final : public Core::IOptionsPage
{
public:
    AndroidSettingsPage()
    {
        setId(Constants::ANDROID_SETTINGS_ID);
        setDisplayName(Tr::tr("Android"));
        setCategory(ProjectExplorer::Constants::SDK_SETTINGS_CATEGORY);
        setSettingsProvider([] {
            static GuardedObject<AndroidSettingsWidget> theAspects;
            return theAspects.get();
        });
    }
};

void setupAndroidSettingsPage()
{
    static AndroidSettingsPage theAndroidSettingsPage;
}

#ifdef WITH_TESTS
class AndroidSettingsPageTest final : public QObject
{
    Q_OBJECT

private slots:
    void testThePageNamesEveryAspectItsFormAsksFor()
    {
        // A page's .qml reaches its aspects as aspects.<qmlName>, which is a
        // property-map lookup: a name that does not match is not a build error
        // and not a qmllint diagnostic, just a control missing from the page.
        AndroidSettingsWidget page;

        const auto namesOf = [](const AspectContainer &container) {
            QStringList names;
            for (BaseAspect * const aspect : container.aspects())
                names << aspect->qmlName();
            return names;
        };

        QCOMPARE(namesOf(page), (QStringList{"Info", "Android", "OpenSsl"}));

        auto android = qobject_cast<AspectContainer *>(page.aspects().at(1));
        QVERIFY(android);
        QCOMPARE(namesOf(*android),
                 (QStringList{"JdkLocation", "DownloadJdk", "SdkLocation", "SetUpSdk",
                              "DownloadSdk", "SdkManager", "NdkList", "AddNdk", "RemoveNdk",
                              "MakeDefaultNdk", "DownloadNdk", "AndroidSummary", "CreateKit"}));

        auto openSsl = qobject_cast<AspectContainer *>(page.aspects().at(2));
        QVERIFY(openSsl);
        QCOMPARE(namesOf(*openSsl),
                 (QStringList{"OpenSslLocation", "DownloadOpenSsl", "OpenSslSummary"}));
    }

    void testTheNdkListSaysWhereEachOneCameFrom()
    {
        // The list used to say it with a lock icon and an italic font for the
        // forced one, neither of which a Qt Quick cell can draw. Both are a
        // second column and a suffix now, so a test can read them.
        AndroidSettingsWidget page;
        auto android = qobject_cast<AspectContainer *>(page.aspects().at(1));
        QVERIFY(android);
        auto ndkList = qobject_cast<NdkListAspect *>(android->aspects().at(6));
        QVERIFY(ndkList);
        QCOMPARE(ndkList->qmlName(), QString("NdkList"));

        QAbstractItemModel * const model = ndkList->tableModel();
        QVERIFY(model);
        QCOMPARE(model->columnCount(), 2);
        QCOMPARE(model->headerData(0, Qt::Horizontal).toString(), Tr::tr("NDK"));
        QCOMPARE(model->headerData(1, Qt::Horizontal).toString(), Tr::tr("Source"));
        // A Qt Quick view reads these off the cell; a QTreeView took them from
        // flags(), which QML cannot reach.
        const QHash<int, QByteArray> roles = model->roleNames();
        QVERIFY(roles.contains(AspectTable::EditableRole));
        QVERIFY(roles.contains(AspectTable::CheckableRole));

        // Rows of its own rather than whatever is installed here: what is
        // being checked is that a picked row maps back to a path, not that
        // this machine has an NDK.
        NdkModel &ndkModel = ndkList->model();
        ndkModel.removeRows(0, ndkModel.rowCount());
        const auto addRow = [&ndkModel](const QString &display, const QString &path, bool custom) {
            const auto item = new QStandardItem(display);
            item->setData(path, NdkListAspect::PathRole);
            item->setData(custom, NdkListAspect::CustomRole);
            ndkModel.appendRow({item, new QStandardItem(custom ? QString("Custom") : QString("SDK Manager"))});
        };
        addRow("/ndk/22 (default)", "/ndk/22", false);
        addRow("/ndk/23", "/ndk/23", true);

        // Nothing is picked to start with, so neither button that acts on a
        // pick has anything to act on.
        QVERIFY(ndkList->currentNdk().isEmpty());
        QCOMPARE(ndkList->currentRow(), -1);
        QVERIFY(!ndkList->currentIsCustom());

        // The cell says which one is forced; the path the buttons act on is
        // kept beside it, so the suffix does not end up in a file path.
        ndkList->setCurrentIndex(ndkModel.index(0, 0));
        QCOMPARE(ndkList->currentNdk(), FilePath::fromUserInput("/ndk/22"));
        QVERIFY(!ndkList->currentIsCustom());
        QCOMPARE(ndkModel.index(0, 1).data(Qt::DisplayRole).toString(), QString("SDK Manager"));

        // Only an NDK that was added by hand can be taken away again.
        ndkList->setCurrentIndex(ndkModel.index(1, 0));
        QCOMPARE(ndkList->currentNdk(), FilePath::fromUserInput("/ndk/23"));
        QVERIFY(ndkList->currentIsCustom());
    }
};

QObject *createAndroidSettingsPageTest()
{
    return new AndroidSettingsPageTest;
}
#endif // WITH_TESTS

} // Android::Internal

#include "androidsettingswidget.moc"
