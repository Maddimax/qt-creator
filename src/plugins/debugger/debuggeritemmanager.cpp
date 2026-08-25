// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "debuggeritemmanager.h"

#include "debuggerkitaspect.h"
#include "debuggertr.h"

#include <coreplugin/dialogs/ioptionspage.h>
#include <coreplugin/icore.h>

#include <nanotrace/nanotrace.h>

#include <projectexplorer/devicesupport/devicemanager.h>
#include <projectexplorer/devicesupport/deviceselectionaspect.h>
#include <projectexplorer/devicesupport/idevice.h>
#include <projectexplorer/kitaspect.h>
#include <projectexplorer/projectexplorerconstants.h>

#include <utils/algorithm.h>
#include <utils/async.h>
#include <utils/environment.h>
#include <utils/groupedlistaspect.h>
#include <utils/groupedmodel.h>
#include <utils/hostosinfo.h>
#include <utils/layoutbuilder.h>
#include <utils/pathchooser.h>
#include <utils/persistentsettings.h>
#include <utils/qtcassert.h>
#include <utils/qtcprocess.h>
#include <utils/shutdownguard.h>
#include <utils/winutils.h>

#ifdef WITH_TESTS
#include <QTest>
#endif

#include <QDebug>
#include <QDir>
#include <QFont>
#include <QFileInfo>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QObject>
#include <QPushButton>
#include <QTimer>
#include <QTreeView>
#include <QVersionNumber>
#include <QWidget>

#include <QtTaskTree/QSingleTaskTreeRunner>

using namespace Core;
using namespace Debugger;
using namespace Debugger::Internal;
using namespace ProjectExplorer;
using namespace QtTaskTree;
using namespace Utils;

static DebuggerItem makeAutoDetectedDebuggerItem(
    const FilePath &command,
    const DebuggerItem::TechnicalData &technicalData,
    const DetectionSource &detectionSource)
{
    DebuggerItem item;
    item.createId();
    item.setCommand(command);
    item.setDetectionSource(detectionSource);
    item.setEngineType(technicalData.engineType);
    item.setAbis(technicalData.abis);
    item.setVersion(technicalData.version);
    item.setLastModified(command.lastModified());
    return item;
}

static Result<DebuggerItem> makeAutoDetectedDebuggerItem(
    const FilePath &command, const DetectionSource &detectionSource)
{
    Result<DebuggerItem::TechnicalData> technicalData
        = DebuggerItem::TechnicalData::extract(command, {});

    if (!technicalData)
        return make_unexpected(std::move(technicalData).error());

    return makeAutoDetectedDebuggerItem(command, *technicalData, detectionSource);
}

namespace Debugger {
namespace Internal {

const char DEBUGGER_COUNT_KEY[] = "DebuggerItem.Count";
const char DEBUGGER_DATA_KEY[] = "DebuggerItem.";
const char DEBUGGER_FILE_VERSION_KEY[] = "Version";
const char DEBUGGER_FILENAME[] = "debuggers.xml";
const char debuggingToolsWikiLinkC[] = "http://wiki.qt.io/Qt_Creator_Windows_Debugging";

static FilePath userSettingsFileName()
{
    return ICore::userResourcePath(DEBUGGER_FILENAME);
}

// --------------------------------------------------------------------------
// DebuggerItemModel
// --------------------------------------------------------------------------

class DebuggerModel final : public Utils::TypedGroupedModel<DebuggerItem>
{
public:
    DebuggerModel();

    void updateDebugger(const DebuggerItem &item);

    void detectDebuggers(const IDeviceConstPtr &device, const FilePaths &searchPaths,
                         const ToolDetectionLogger &logger = {});
    void restoreDebuggers();
    void saveDebuggers();

    QVariant registerDebugger(const DebuggerItem &item);
    void deregisterDebugger(const QVariant &id);

    void readDebuggers(const FilePath &fileName, bool isSdk);
    void autoDetectCdbDebuggers(const ToolDetectionLogger &logger = {});
    void autoDetectGdbOrLldbDebuggers(
        const FilePaths &searchPaths,
        const DetectionSource &detectionSource,
        const ToolDetectionLogger &logger = {});
    void autoDetectUvscDebuggers(const ToolDetectionLogger &logger = {});
    int cloneRow(int row) override;
    QString uniqueDisplayName(const QString &base);

    QVariant variantData(int row, int column, int role) const final
    {
        return item(row).data(column, role);
    }

private:
    PersistentSettingsWriter m_writer;
};

static DebuggerModel &debuggerModel()
{
    static GuardedObject<DebuggerModel> theModel;
    return theModel;
}

DebuggerModel::DebuggerModel()
    : m_writer(userSettingsFileName(), "QtCreatorDebuggers")
{
    setHeader({Tr::tr("Name"), Tr::tr("Path"), Tr::tr("Type")});

    setFilters(ProjectExplorer::Constants::msgAutoDetected(),
               {{ProjectExplorer::Constants::msgManual(), [this](int row) {
                   return !item(row).detectionSource().isAutoDetected();
               }}});

    connect(ICore::instance(), &ICore::saveSettingsRequested,
            this, &DebuggerModel::saveDebuggers);
    connect(DeviceManager::instance(), &DeviceManager::toolDetectionRequested, this,
            [this](Id devId, const FilePaths &searchPaths, quint64 token,
                   const ToolDetectionLogger &logger) {
        const IDevicePtr dev = DeviceManager::find(devId);
        QTC_ASSERT(dev, return);
        dev->registerToolDetectionTask(token);
        detectDebuggers(dev, searchPaths, logger);
        dev->deregisterToolDetectionTask(token);
    });
}

int DebuggerModel::cloneRow(int row)
{
    const DebuggerItem itm = item(row);
    if (!itm || !itm.canClone())
        return -1;
    DebuggerItem newItem;
    newItem.createId();
    newItem.setCommand(itm.command());
    newItem.setUnexpandedDisplayName(uniqueDisplayName(Tr::tr("Clone of %1").arg(itm.displayName())));
    newItem.reinitializeFromFile();
    newItem.setDetectionSource({DetectionSource::Manual, itm.detectionSource().id});
    newItem.setEngineType(itm.engineType());
    return appendVolatileItem(newItem);
}

void DebuggerModel::updateDebugger(const DebuggerItem &ditem)
{
    for (int i = 0; i < itemCount(); ++i) {
        if (item(i).id() == ditem.id()) {
            setVolatileItem(i, ditem);
            notifyRowChanged(i);
            return;
        }
    }
}


void DebuggerModel::autoDetectCdbDebuggers(const ToolDetectionLogger &logger)
{
    if (logger)
        logger.logTopLevel(Tr::tr("Searching for CDB..."));

    FilePaths cdbs;

    const QStringList programDirs = {qtcEnvironmentVariable("ProgramFiles"),
                                     qtcEnvironmentVariable("ProgramFiles(x86)"),
                                     qtcEnvironmentVariable("ProgramW6432")};

    QFileInfoList kitFolders;

    for (const QString &dirName : programDirs) {
        if (dirName.isEmpty())
            continue;
        const QDir dir(dirName);
        // Windows SDK's starting from version 8 live in
        // "ProgramDir\Windows Kits\<version>"
        const QString windowsKitsFolderName = "Windows Kits";
        if (dir.exists(windowsKitsFolderName)) {
            QDir windowKitsFolder = dir;
            if (windowKitsFolder.cd(windowsKitsFolderName)) {
                // Check in reverse order (latest first)
                kitFolders.append(windowKitsFolder.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot,
                                                                 QDir::Time | QDir::Reversed));
            }
        }

        // Pre Windows SDK 8: Check 'Debugging Tools for Windows'
        for (const QFileInfo &fi : dir.entryInfoList({"Debugging Tools for Windows*"},
                                                     QDir::Dirs | QDir::NoDotAndDotDot)) {
            const FilePath filePath = FilePath::fromFileInfo(fi).pathAppended("cdb.exe");
            if (!cdbs.contains(filePath))
                cdbs.append(filePath);
        }
    }


    constexpr char RootVal[]   = "KitsRoot";
    constexpr char RootVal81[] = "KitsRoot81";
    constexpr char RootVal10[] = "KitsRoot10";
    const QSettings installedRoots(
                "HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows Kits\\Installed Roots",
                QSettings::NativeFormat);
    for (auto rootVal : {RootVal, RootVal81, RootVal10}) {
        QFileInfo root(installedRoots.value(QLatin1String(rootVal)).toString());
        if (root.exists() && !kitFolders.contains(root))
            kitFolders.append(root);
    }

    for (const QFileInfo &kitFolderFi : kitFolders) {
        const QString path = kitFolderFi.absoluteFilePath();
        QStringList abis = {"x86", "x64"};
        if (HostOsInfo::hostArchitecture() == Utils::OsArchArm64)
            abis << "arm64";
        for (const QString &abi: std::as_const(abis)) {
            const QFileInfo cdbBinary(path + "/Debuggers/" + abi + "/cdb.exe");
            if (cdbBinary.isExecutable())
                cdbs.append(FilePath::fromString(cdbBinary.absoluteFilePath()));
        }
    }

    for (const FilePath &cdb : std::as_const(cdbs)) {
        if (DebuggerItemManager::findByCommand(cdb))
            continue;
        DebuggerItem item;
        item.createId();
        item.setDetectionSource(DetectionSource::FromSystem);
        item.setAbis(Abi::abisOfBinary(cdb));
        item.setCommand(cdb);
        item.setEngineType(CdbEngineType);
        appendItem(item);
        if (logger)
            logger.logItem(Tr::tr("Found: \"%1\".").arg(cdb.toUserOutput()));
    }
}

static Utils::FilePaths searchGdbPathsFromRegistry()
{
    if (!HostOsInfo::isWindowsHost())
        return {};

    // Registry token for the "GNU Tools for ARM Embedded Processors".
    static const char kRegistryToken[] = "HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\" \
                                         "Windows\\CurrentVersion\\Uninstall\\";

    Utils::FilePaths searchPaths;

    QSettings registry(kRegistryToken, QSettings::NativeFormat);
    const auto productGroups = registry.childGroups();
    for (const QString &productKey : productGroups) {
        if (!productKey.startsWith("GNU Tools for ARM Embedded Processors"))
            continue;
        registry.beginGroup(productKey);
        QString uninstallFilePath = registry.value("UninstallString").toString();
        if (uninstallFilePath.startsWith(QLatin1Char('"')))
            uninstallFilePath.remove(0, 1);
        if (uninstallFilePath.endsWith(QLatin1Char('"')))
            uninstallFilePath.remove(uninstallFilePath.size() - 1, 1);
        registry.endGroup();

        const QString toolkitRootPath = QFileInfo(uninstallFilePath).path();
        const QString toolchainPath = toolkitRootPath + QLatin1String("/bin");
        searchPaths.push_back(FilePath::fromString(toolchainPath));
    }

    return searchPaths;
}

static QStringList debuggerSearchFilters()
{
    QStringList filters
        = {"gdb-i686-pc-mingw32",
           "gdb-i686-pc-mingw32.exe",
           "gdb",
           "gdb.exe",
           "gdb-multiarch",
           "lldb",
           "lldb.exe",
           "lldb-[1-9]*",
           "arm-none-eabi-gdb-py.exe",
           "*-*-*-gdb"};

    if (nativeDapDebuggersEnabled()) {
        filters.append({
            "lldb-dap",
            "lldb-dap.exe",
            "lldb-dap-*",
            // LLDB DAP server was named lldb-vscode prior LLVM 18.0.0
            "lldb-vscode",
            "lldb-vscode.exe",
            "lldb-vscode-*",
        });
    }

    return filters;
}

void DebuggerModel::autoDetectGdbOrLldbDebuggers(
    const FilePaths &searchPaths, const DetectionSource &detectionSource,
    const ToolDetectionLogger &logger)
{
    const QStringList filters = debuggerSearchFilters();

    if (searchPaths.isEmpty())
        return;

    FilePaths suspects;

    // FIXME: Devicify.
    if (searchPaths.front().osType() == OsTypeMac) {
        Process proc;
        proc.setCommand({"xcrun", {"--find", "lldb"}});
        using namespace std::chrono_literals;
        proc.runBlocking(2s);
        if (proc.result() == ProcessResult::FinishedWithSuccess) {
            const FilePath lPath = FilePath::fromUserInput(proc.allOutput().trimmed());
            if (lPath.isExecutableFile())
                suspects.append(lPath);
        }
    }

    FilePaths paths = searchPaths;
    if (searchPaths.front().isLocal()) {
        paths.append(searchGdbPathsFromRegistry());

        const Result<FilePath> lldb = Core::ICore::lldbExecutable(CLANG_BINDIR);
        if (lldb)
            suspects.append(*lldb);
    }

    paths = Utils::filteredUnique(paths);

    for (const FilePath &path : std::as_const(paths))
        suspects.append(path.dirEntries(Utils::FileFilter{filters, Utils::DirFilterFlag::Files | Utils::DirFilterFlag::Executable}));

    if (logger)
        logger.logTopLevel(Tr::tr("Searching for GDB and LLDB..."));
    for (const FilePath &command : std::as_const(suspects)) {
        int existingRow = -1;
        for (int i = 0; i < itemCount(); ++i) {
            if (item(i).command() == command) {
                existingRow = i;
                break;
            }
        }
        if (existingRow >= 0) {
            DebuggerItem existingItem = item(existingRow);
            if (command.lastModified() != existingItem.lastModified()) {
                existingItem.reinitializeFromFile();
                setVolatileItem(existingRow, existingItem);
                notifyRowChanged(existingRow);
            }

            if (nativeDapDebuggersEnabled()) {
                if (existingItem.engineType() != GdbEngineType)
                    continue;

                // GDB starting version 14.1.0 supports DAP interface, but unlike LLDB,
                // it uses the same binary, hence this hack.
                const QVersionNumber dapSupportMinVersion{14, 1, 0};
                if (QVersionNumber::fromString(existingItem.version()) < dapSupportMinVersion)
                    continue;
                // This is the "update" path: there's already a capable GDB in the settings,
                // we only need to add a corresponding DAP entry if it's missing.
                const bool hasDap = Utils::anyOf(volatileItems(), [&command](const DebuggerItem &item) {
                    return item.command() == command && item.engineType() == GdbDapEngineType;
                });
                if (hasDap)
                    continue;

                const DebuggerItem dapItem = makeAutoDetectedDebuggerItem(
                    command,
                    {
                        .engineType = GdbDapEngineType,
                        .abis = existingItem.abis(),
                        .version = existingItem.version(),
                    },
                    detectionSource);
                appendItem(dapItem);
                if (logger) {
                    logger.logItem(
                        Tr::tr("Added a surrogate GDB DAP item for existing entry \"%1\".")
                            .arg(command.toUserOutput()));
                }
            }
            continue;
        }

        const Result<DebuggerItem> item
            = makeAutoDetectedDebuggerItem(command, detectionSource);
        if (!item) {
            if (logger)
                logger.logItem(item.error());
            continue;
        }

        appendItem(*item);
        if (logger)
            logger.logItem(Tr::tr("Found: \"%1\".").arg(command.toUserOutput()));
        if (nativeDapDebuggersEnabled()) {
            if (item->engineType() != GdbEngineType)
                continue;

            // GDB starting version 14.1.0 supports DAP interface, but unlike LLDB,
            // it uses the same binary, hence this hack
            const QVersionNumber dapSupportMinVersion{14, 1, 0};
            if (QVersionNumber::fromString(item->version()) < dapSupportMinVersion)
                continue;

            const DebuggerItem dapItem = makeAutoDetectedDebuggerItem(
                command,
                {
                    .engineType = GdbDapEngineType,
                    .abis = item->abis(),
                    .version = item->version(),
                },
                detectionSource);
            appendItem(dapItem);
            if (logger) {
                logger.logItem(
                    Tr::tr("Added a surrogate GDB DAP item for \"%1\".").arg(command.toUserOutput()));
            }
        }
    }
}

void DebuggerModel::autoDetectUvscDebuggers(const ToolDetectionLogger &logger)
{
    if (!HostOsInfo::isWindowsHost())
        return;

    if (logger)
        logger.logTopLevel(Tr::tr("Searching for uVision..."));

    // Registry token for the "KEIL uVision" instance.
    static const char kRegistryToken[] = "HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\" \
                                         "Windows\\CurrentVersion\\Uninstall\\Keil \u00B5Vision4";

    QSettings registry(QLatin1String(kRegistryToken), QSettings::NativeFormat);
    const auto productGroups = registry.childGroups();
    for (const QString &productKey : productGroups) {
        if (!productKey.startsWith("App"))
            continue;
        registry.beginGroup(productKey);
        const QDir rootPath(registry.value("Directory").toString());
        registry.endGroup();
        const FilePath uVision = FilePath::fromString(
                    rootPath.absoluteFilePath("UV4/UV4.exe"));
        if (!uVision.exists())
            continue;
        if (DebuggerItemManager::findByCommand(uVision))
            continue;

        QString errorMsg;
        const QString uVisionVersion = winGetDLLVersion(
                    WinDLLFileVersion, uVision.toUrlishString(), &errorMsg);

        DebuggerItem item;
        item.createId();
        item.setDetectionSource(DetectionSource::FromSystem);
        item.setCommand(uVision);
        item.setVersion(uVisionVersion);
        item.setEngineType(UvscEngineType);
        appendItem(item);
        if (logger)
            logger.logItem(Tr::tr("Found: \"%1\".").arg(uVision.toUserOutput()));
    }
}

QString DebuggerModel::uniqueDisplayName(const QString &base)
{
    for (const DebuggerItem &item : volatileItems())
        if (item.unexpandedDisplayName() == base)
            return uniqueDisplayName(base + " (1)");
    return base;
}

QVariant DebuggerModel::registerDebugger(const DebuggerItem &item)
{
    // Try re-using existing item first.
    for (const DebuggerItem &d : volatileItems()) {
        if (d.command() == item.command()
            && d.detectionSource().isAutoDetected() == item.detectionSource().isAutoDetected()
            && d.engineType() == item.engineType()
            && d.unexpandedDisplayName() == item.unexpandedDisplayName()
            && d.abis() == item.abis())
            return d.id();
    }

    // If item already has an id, use it. Otherwise, create a new id.
    DebuggerItem di = item;
    if (!di.id().isValid())
        di.createId();

    appendItem(di);
    return di.id();
}

void DebuggerModel::deregisterDebugger(const QVariant &id)
{
    for (int i = 0; i < itemCount(); ++i) {
        if (item(i).id() == id) {
            removeItem(i);
            return;
        }
    }
}

void DebuggerModel::readDebuggers(const FilePath &fileName, bool isSdk)
{
    PersistentSettingsReader reader;
    if (!reader.load(fileName))
        return;
    Store data = reader.restoreValues();

    // Check version
    int version = data.value(DEBUGGER_FILE_VERSION_KEY, 0).toInt();
    if (version < 1)
        return;

    int count = data.value(DEBUGGER_COUNT_KEY, 0).toInt();
    for (int i = 0; i < count; ++i) {
        const Key key = numberedKey(DEBUGGER_DATA_KEY, i);
        if (!data.contains(key))
            continue;
        const Store dbMap = storeFromVariant(data.value(key));
        DebuggerItem item(dbMap);
        if (isSdk) {
            item.setDetectionSource(DetectionSource::FromSdk);
            // SDK debuggers are always considered to be up-to-date, so no need to recheck them.
        } else {
            // User settings.
            if (item.detectionSource().isAutoDetected()) {
                if (!item.isValid() || item.engineType() == NoEngineType) {
                    qWarning() << QString("DebuggerItem \"%1\" (%2) read from \"%3\" dropped since it is not valid.")
                                  .arg(item.command().toUserOutput(), item.id().toString(), fileName.toUserOutput());
                    continue;
                }
                // FIXME: During startup, devices are not yet available, so we cannot check if the file still exists.
                if (item.command().isLocal() && !item.command().isExecutableFile()) {
                    qWarning() << QString("DebuggerItem \"%1\" (%2) read from \"%3\" dropped since the command is not executable.")
                                  .arg(item.command().toUserOutput(), item.id().toString(), fileName.toUserOutput());
                    continue;
                }
            }
        }
        registerDebugger(item);
    }
}

void DebuggerModel::restoreDebuggers()
{
    // Read debuggers from SDK
    readDebuggers(ICore::installerResourcePath(DEBUGGER_FILENAME), true);

    // Read all debuggers from user file.
    readDebuggers(userSettingsFileName(), false);

    // Auto detect current.
    const IDeviceConstPtr desktopDevice = DeviceManager::defaultDesktopDevice();
    if (QTC_GUARD(desktopDevice))
        detectDebuggers(desktopDevice, desktopDevice->systemEnvironment().path());
}

void DebuggerModel::detectDebuggers(
    const IDeviceConstPtr &device, const FilePaths &searchPaths,
    const ToolDetectionLogger &logger)
{
    QTC_ASSERT(device, return);
    const bool isDesktopDevice = device->id() == ProjectExplorer::Constants::DESKTOP_DEVICE_ID;
    const DetectionSource detectionSource = isDesktopDevice ? DetectionSource::FromSystem
                                                            : DetectionSource::Manual;
    autoDetectGdbOrLldbDebuggers(searchPaths, detectionSource, logger);
    if (isDesktopDevice) {
        autoDetectCdbDebuggers(logger);
        autoDetectUvscDebuggers(logger);
    }
}

void DebuggerModel::saveDebuggers()
{
    Store data;
    data.insert(DEBUGGER_FILE_VERSION_KEY, 1);

    int count = 0;
    for (const DebuggerItem &item : items()) {
        if (item.detectionSource().isTemporary())
            continue;
        if (item.isGeneric()) // do not store generic debuggers, these get added automatically
            continue;
        const bool skipNoEngine = item.detectionSource().isAutoDetected();
        if (item.isValid() && (!skipNoEngine || item.engineType() != NoEngineType)) {
            Store tmp = item.toMap();
            if (!tmp.isEmpty()) {
                data.insert(numberedKey(DEBUGGER_DATA_KEY, count), variantFromStore(tmp));
                ++count;
            }
        }
    }
    data.insert(DEBUGGER_COUNT_KEY, count);
    m_writer.save(data);

    // Do not save default debuggers as they are set by the SDK.
}

using ExecutableItem = QtTaskTree::ExecutableItem; // trick lupdate, QTBUG-140636

ExecutableItem autoDetectDebuggerRecipe(
    ProjectExplorer::Kit *kit,
    const Utils::FilePaths &searchPaths,
    const DetectionSource &detectionSource,
    const LogCallback &logCallback)
{
    const QStringList searchFilters = debuggerSearchFilters();

    static const auto searchDebuggers = [](QPromise<DebuggerItem> &promise,
                                           const FilePaths &searchPaths,
                                           const DetectionSource &detectionSource,
                                           const QStringList &searchFilters) {
        FilePaths suspects;

        for (const FilePath &path : searchPaths)
            suspects.append(path.dirEntries(Utils::FileFilter{searchFilters, Utils::DirFilterFlag::Files | Utils::DirFilterFlag::Executable}));

        for (const FilePath &command : std::as_const(suspects)) {
            const Result<DebuggerItem> item = makeAutoDetectedDebuggerItem(command, detectionSource);

            if (item)
                promise.addResult(*item);
            else
                qWarning() << "Failed to auto-detect debugger from" << command.toUserOutput() << ":"
                           << item.error();
        }
    };

    auto setupSearch = [searchPaths, detectionSource, searchFilters](Async<DebuggerItem> &async) {
        async.setConcurrentCallData(searchDebuggers, searchPaths, detectionSource, searchFilters);
    };

    auto searchDone = [kit, logCallback](const Async<DebuggerItem> &async) {
        const QList<DebuggerItem> items = async.results();
        for (const DebuggerItem &item : items) {
            if (item.isValid() && item.engineType() != NoEngineType) {
                logCallback(Tr::tr("Found debugger: \"%1\".").arg(item.command().toUserOutput()));
                DebuggerItemManager::registerDebugger(item);
                DebuggerKitAspect::setDebugger(kit, item.id());
            } else
                qWarning() << "Invalid debugger item detected?!";
        }
    };

    return AsyncTask<DebuggerItem>(setupSearch, searchDone);
}

ExecutableItem removeAutoDetected(const QString &detectionSourceId, const LogCallback &logCallback)
{
    return QSyncTask([detectionSourceId, logCallback]() {
        const auto debuggers = filtered(
            DebuggerItemManager::debuggers(), [detectionSourceId](const DebuggerItem &item) {
                return item.detectionSource().id == detectionSourceId;
            });

        for (const auto &debugger : debuggers) {
            logCallback(Tr::tr("Removing debugger: \"%1\".").arg(debugger.displayName()));
            DebuggerItemManager::deregisterDebugger(debugger.id());
        }
    });
}

Utils::Result<ExecutableItem> createAspectFromJson(
    const DetectionSource &detectionSource,
    const Utils::FilePath &rootPath,
    ProjectExplorer::Kit *kit,
    const QJsonValue &json,
    const ProjectExplorer::LogCallback &logCallback)
{
    if (!json.isString())
        return ResultError(Tr::tr("Invalid JSON value for debugger: \"%1\".").arg(json.toString()));

    const FilePath command = rootPath.withNewPath(json.toString());

    if (command.isEmpty())
        return ResultError(Tr::tr("Empty command for debugger."));

    const auto setup = [command, detectionSource, logCallback](Async<Result<DebuggerItem>> &async) {
        async.setConcurrentCallData(
            [](QPromise<Result<DebuggerItem>> &promise,
               const FilePath &command,
               const DetectionSource &detectionSource) {
                promise.addResult(makeAutoDetectedDebuggerItem(command, detectionSource));
            },
            command,
            detectionSource);
    };

    const auto registerDebugger = [kit, logCallback](const Async<Result<DebuggerItem>> &async) {
        Result<DebuggerItem> item = async.result();
        if (!item) {
            logCallback(Tr::tr("Failed to create debugger from JSON: %1").arg(item.error()));
            return;
        }

        DebuggerItemManager::registerDebugger(*item);
        DebuggerKitAspect::setDebugger(kit, item->id());
    };

    return AsyncTask<Result<DebuggerItem>>(setup, registerDebugger);
}

} // namespace Internal

// --------------------------------------------------------------------------
// DebuggerItemManager
// --------------------------------------------------------------------------

void DebuggerItemManager::restoreDebuggers()
{
    NANOTRACE_SCOPE("Debugger", "DebuggerItemManager::restoreDebuggers");
    debuggerModel().restoreDebuggers();
}

const QList<DebuggerItem> DebuggerItemManager::debuggers()
{
    QList<DebuggerItem> result;
    for (const DebuggerItem &item : debuggerModel().items())
        result.append(item);
    return result;
}

DebuggerItem DebuggerItemManager::findByCommand(const FilePath &command)
{
    for (const DebuggerItem &item : debuggerModel().volatileItems())
        if (item.command() == command)
            return item;
    return {};
}

DebuggerItem DebuggerItemManager::findById(const QVariant &id)
{
    for (const DebuggerItem &item : debuggerModel().volatileItems())
        if (item.id() == id)
            return item;
    return {};
}

DebuggerItem DebuggerItemManager::findByEngineType(DebuggerEngineType engineType)
{
    for (const DebuggerItem &item : debuggerModel().volatileItems())
        if (item.engineType() == engineType)
            return item;
    return {};
}

QVariant DebuggerItemManager::registerDebugger(const DebuggerItem &item)
{
    return debuggerModel().registerDebugger(item);
}

void DebuggerItemManager::deregisterDebugger(const QVariant &id)
{
    return debuggerModel().deregisterDebugger(id);
}

// DebuggerAspects

// What the Debuggers page edits. The debuggers themselves live in
// DebuggerItemManager; debuggerModel() is the editable copy, and applying the
// page is applying it.
//
// What a debugger is - its ABIs, its version, its engine - is found by running
// it, so those are shown and not typed. The widget page read them back out of
// the labels it had put them in; they are the page's own state here.
class DebuggerAspects final : public AspectContainer
{
public:
    DebuggerAspects()
    {
        setAutoApply(false);
        setQmlSource(QUrl("qrc:/qt/qml/QtCreator/Debugger/DebuggersPage.qml"));

        device.setQmlName("Device");

        debuggers.setQmlName("Debuggers");
        debuggers.setModel(&debuggerModel());
        debuggers.setCanRemoveRow([](int row) {
            return !debuggerModel().item(row).detectionSource().isAutoDetected();
        });
        debuggers.setCanCloneRow([](int row) { return debuggerModel().item(row).canClone(); });

        add.setQmlName("Add");
        add.setActionText(Tr::tr("Add"));
        add.setAction([this] { addDebugger(); });

        detect.setQmlName("Redetect");
        detect.setActionText(Tr::tr("Re-detect"));
        detect.setAction([this] {
            for (const IDeviceConstPtr &dev : device.selectedDevices())
                debuggerModel().detectDebuggers(dev, dev->toolSearchPaths());
        });

        details.setQmlName("Details");

        displayName.setQmlName("DisplayName");
        displayName.setDisplayStyle(StringAspect::LineEditDisplay);
        displayName.setLabelText(Tr::tr("Name:"));

        binary.setQmlName("Binary");
        binary.setLabelText(Tr::tr("Path:"));
        binary.setExpectedKind(PathChooserKind::ExistingCommand);
        binary.setHistoryCompleter("DebuggerPaths");
        binary.setAllowPathFromDevice(true);
        // Beyond "is there a file there": whether it is a debugger at all is
        // decided by running it, so the answer arrives later.
        binary.setValidationFunction(AsyncValidationFunction([this](const QString &text) {
            return binary.defaultValidationFunction()(text).then(
                [](const AsyncValidationResult &result) -> AsyncValidationResult {
                    if (!result)
                        return result;
                    DebuggerItem item;
                    item.setCommand(FilePath::fromUserInput(*result));
                    QString errorMessage;
                    item.reinitializeFromFile(&errorMessage);
                    if (!errorMessage.isEmpty())
                        return make_unexpected(errorMessage);
                    return *result;
                });
        }));

        cdbHint.setQmlName("CdbHint");
        cdbHint.setTextFormat(AspectControls::TextFormat::RichText);

        type.setQmlName("Type");
        type.setLabelText(Tr::tr("Type:"));
        abis.setQmlName("Abis");
        abis.setLabelText(Tr::tr("ABIs:"));
        version.setQmlName("Version");
        version.setLabelText(Tr::tr("Version:"));

        workingDirectory.setQmlName("WorkingDirectory");
        workingDirectory.setLabelText(Tr::tr("Working directory:"));
        workingDirectory.setExpectedKind(PathChooserKind::Directory);
        workingDirectory.setHistoryCompleter("DebuggerPaths");

        // Behaviour, not layout.
        connect(&device, &DeviceSelectionAspect::currentDeviceChanged, this, [this] {
            const IDeviceConstPtr dev = device.currentDevice();
            const FilePath deviceRoot = dev ? dev->rootPath() : FilePath();
            debuggerModel().setExtraFilter(deviceRoot.isEmpty()
                ? GroupedModel::Filter{}
                : GroupedModel::Filter{[deviceRoot](int row) {
                      const FilePath path = debuggerModel().item(row).command();
                      return path.isEmpty() || path.isSameDevice(deviceRoot);
                  }});
        });

        connect(&debuggers, &GroupedListAspect::currentRowChanged,
                this, [this](int, int newRow) { showDebugger(newRow); });
        binary.addOnVolatileValueChanged(this, [this] { redetect(); });
        workingDirectory.addOnVolatileValueChanged(this, [this] { store(); });
        displayName.addOnVolatileValueChanged(this, [this] { store(); });
        showDebugger(-1);
    }

    void apply() override
    {
        AspectContainer::apply();
        debuggerModel().apply();
    }

    void cancel() override
    {
        AspectContainer::cancel();
        debuggerModel().cancel();
    }

    bool isDirty() const override
    {
        return AspectContainer::isDirty() || debuggerModel().isDirty();
    }

    DeviceSelectionAspect device{this};
    GroupedListAspect debuggers{this};
    ActionAspect add{this};
    ActionAspect detect{this};
    AspectContainer details{this};
    StringAspect displayName{&details};
    FilePathAspect binary{&details};
    TextDisplay cdbHint{&details};
    TextDisplay type{&details};
    TextDisplay abis{&details};
    TextDisplay version{&details};
    FilePathAspect workingDirectory{&details};

private:
    DebuggerItem currentItem() const
    {
        DebuggerItem item(m_id);
        // The name is read-only for an auto-detected debugger, so what was
        // loaded is kept - it may be empty, meaning the name is worked out on
        // the fly.
        item.setUnexpandedDisplayName(m_detectionSource.isAutoDetected()
                                          ? m_loadedUnexpandedDisplayName
                                          : displayName.volatileValue());
        item.setCommand(binary.expandedVolatileValue());
        item.setWorkingDirectory(workingDirectory.expandedVolatileValue());
        item.setDetectionSource(m_detectionSource);
        item.setAbis(m_abis);
        item.setVersion(m_version);
        item.setEngineType(m_engineType);
        return item;
    }

    void setFoundOut(const DebuggerItem &item)
    {
        m_abis = item.abis();
        m_version = item.version();
        m_engineType = item.engineType();
        abis.setText(item.abiNames().join(", "));
        version.setText(item.version());
        type.setText(item.engineTypeName());
    }

    void showDebugger(int row)
    {
        const DebuggerItem item = row >= 0 ? debuggerModel().item(row) : DebuggerItem{};
        details.setVisible(bool(item));
        // Nothing is current while the form is being filled in, so nothing the
        // fields say on the way is written back. See store().
        m_id = QVariant();
        if (!item)
            return;

        m_detectionSource = item.detectionSource();
        const bool autoDetected = m_detectionSource.isAutoDetected();

        m_loadedUnexpandedDisplayName = item.unexpandedDisplayName();
        displayName.setEnabled(!autoDetected);
        displayName.setValue(autoDetected ? item.displayName() : item.unexpandedDisplayName());

        binary.setReadOnly(autoDetected);
        binary.setValue(item.command());
        binary.setExpectedKind(item.isGeneric() ? PathChooserKind::Any
                                                : PathChooserKind::ExistingCommand);

        workingDirectory.setReadOnly(autoDetected);
        workingDirectory.setValue(item.workingDirectory());

        QString hint;
        QString versionCommand = "--version";
        if (item.engineType() == CdbEngineType) {
            const QString versionString = is64BitWindowsSystem() ? Tr::tr("64-bit version")
                                                                 : Tr::tr("32-bit version");
            //: Label text for path configuration. %2 is "x-bit version".
            hint = "<html><body><p>"
                   + Tr::tr("Specify the path to the "
                            "<a href=\"%1\">Windows Console Debugger executable</a>"
                            " (%2) here.").arg(QLatin1String(debuggingToolsWikiLinkC),
                                               versionString)
                   + "</p></body></html>";
            versionCommand = "-version";
        }
        cdbHint.setText(hint);
        cdbHint.setVisible(!hint.isEmpty());
        binary.setCommandVersionArguments({versionCommand});

        setFoundOut(item);
        if (const IDeviceConstPtr dev = device.currentDevice())
            binary.setInitialBrowsePathBackup(dev->rootPath());
        m_id = item.id();

        // Nothing is known about it yet, and there is something to run.
        if (m_engineType == NoEngineType && binary.expandedVolatileValue().isExecutableFile())
            redetect();
    }

    void store()
    {
        if (!m_id.isNull())
            debuggerModel().updateDebugger(currentItem());
    }

    // What the binary turns out to be. Runs it, so the answer arrives later;
    // meanwhile the page may well have moved to another debugger.
    void redetect()
    {
        if (!m_id.isValid())
            return;

        m_taskTreeRunner.reset();

        if (!binary.expandedVolatileValue().isExecutableFile()) {
            setFoundOut(DebuggerItem{});
            store();
            return;
        }

        const auto onSetup = [this](Async<DebuggerItem> &task) {
            task.setConcurrentCallData([tmp = currentItem()]() mutable {
                tmp.reinitializeFromFile();
                return tmp;
            });
        };
        const auto onDone = [this, id = m_id](const Async<DebuggerItem> &task) {
            if (!task.isResultAvailable())
                return;
            const DebuggerItem found = task.result();
            if (m_id == id) {
                setFoundOut(found);
                store();
                return;
            }
            // Another debugger is being shown now, so only what was found goes
            // back; a name or a path edited in the meantime stays.
            DebuggerItem item = DebuggerItemManager::findById(id);
            if (!item)
                return;
            item.setEngineType(found.engineType());
            item.setAbis(found.abis());
            item.setVersion(found.version());
            debuggerModel().updateDebugger(item);
        };
        m_taskTreeRunner.start({AsyncTask<DebuggerItem>(onSetup, onDone)});
        store();
    }

    void addDebugger()
    {
        DebuggerItem item;
        item.createId();
        item.setEngineType(NoEngineType);
        item.setUnexpandedDisplayName(debuggerModel().uniqueDisplayName(Tr::tr("New Debugger")));
        debuggers.setCurrentRow(debuggerModel().appendVolatileItem(item));
    }

    // The debugger the form is showing, and nothing while it is being filled
    // in.
    QVariant m_id;
    DetectionSource m_detectionSource;
    DebuggerEngineType m_engineType = NoEngineType;
    QString m_loadedUnexpandedDisplayName;
    Abis m_abis;
    QString m_version;
    QSingleTaskTreeRunner m_taskTreeRunner;
};

#ifdef WITH_TESTS

// The page's debuggers lived in a QTreeView's selection and a details widget,
// and what a debugger *is* - its ABIs, its version, its engine - was read back
// out of the labels it had been written into. None of that could be checked
// without opening the page.

class DebuggersSettingsTest : public QObject
{
    Q_OBJECT

private slots:
    void cleanup() { debuggerModel().cancel(); }

    void testNoDebuggerIsShownUntilOneIsPicked();
    void testEditingTheNameUpdatesTheDebugger();
    void testShowingADebuggerDoesNotRewriteIt();
    void testWhatWasFoundOutSurvivesAnEdit();
    void testAnAutoDetectedDebuggerIsShownButNotEditable();

private:
    static int rowThatIsAutoDetected()
    {
        for (int row = 0; row < debuggerModel().itemCount(); ++row) {
            if (debuggerModel().item(row).detectionSource().isAutoDetected())
                return row;
        }
        return -1;
    }
};

void DebuggersSettingsTest::testNoDebuggerIsShownUntilOneIsPicked()
{
    DebuggerAspects page;
    QVERIFY(!page.details.isVisible());
    QCOMPARE(page.debuggers.currentRow(), -1);
    QVERIFY(!page.debuggers.canClone());
    QVERIFY(!page.debuggers.canRemove());

    page.add.triggerAction();
    QVERIFY(page.debuggers.currentRow() >= 0);
    QVERIFY(page.details.isVisible());
    QVERIFY(page.displayName.volatileValue().startsWith("New Debugger"));
    // One the user added is theirs to edit and to take away again.
    QVERIFY(page.displayName.isEnabled());
    QVERIFY(page.debuggers.canRemove());
}

void DebuggersSettingsTest::testEditingTheNameUpdatesTheDebugger()
{
    DebuggerAspects page;
    page.add.triggerAction();
    const int row = page.debuggers.currentRow();
    QVERIFY(row >= 0);

    page.displayName.setVolatileValue(QString("Renamed"));
    QCOMPARE(debuggerModel().item(row).unexpandedDisplayName(), QString("Renamed"));

    // Showing another debugger must not write the one that was on screen into
    // it, and coming back shows what was left there.
    page.add.triggerAction();
    const int second = page.debuggers.currentRow();
    QVERIFY(second != row);
    page.displayName.setVolatileValue(QString("Second"));
    QCOMPARE(debuggerModel().item(second).unexpandedDisplayName(), QString("Second"));
    QCOMPARE(debuggerModel().item(row).unexpandedDisplayName(), QString("Renamed"));

    page.debuggers.setCurrentRow(row);
    QCOMPARE(page.displayName.volatileValue(), QString("Renamed"));
}

void DebuggersSettingsTest::testShowingADebuggerDoesNotRewriteIt()
{
    DebuggerAspects page;
    page.add.triggerAction();
    const int row = page.debuggers.currentRow();
    QVERIFY(row >= 0);

    // A path written with a variable in it stays as it was written. The field
    // hands back the expanded path, so loading a debugger into it must not
    // store what it just read.
    const QString written = "/tools/%{HostOs:PathListSeparator}/gdb";
    DebuggerItem item = debuggerModel().item(row);
    item.setCommand(FilePath::fromString(written));
    debuggerModel().updateDebugger(item);
    QVERIFY(page.binary.expandedVolatileValue() != FilePath::fromString(written));

    page.debuggers.setCurrentRow(-1);
    page.debuggers.setCurrentRow(row);

    QCOMPARE(debuggerModel().item(row).command(), FilePath::fromString(written));
}

void DebuggersSettingsTest::testWhatWasFoundOutSurvivesAnEdit()
{
    DebuggerAspects page;
    page.add.triggerAction();
    const int row = page.debuggers.currentRow();
    QVERIFY(row >= 0);

    // What a debugger is comes from running it, so an edit to what the user
    // can type must carry the rest along untouched. The page holds it; the
    // widget page went back to its own labels for it, which worked but meant
    // the truth about a debugger lived in a string on screen.
    DebuggerItem item = debuggerModel().item(row);
    item.setAbis({Abi::fromString("x86-linux-generic-elf-64bit"),
                  Abi::fromString("arm-linux-generic-elf-64bit")});
    item.setVersion("13.2 (Debian 13.2-1)");
    item.setEngineType(GdbEngineType);
    debuggerModel().updateDebugger(item);

    page.debuggers.setCurrentRow(-1);
    page.debuggers.setCurrentRow(row);
    page.displayName.setVolatileValue(QString("Edited"));

    const DebuggerItem stored = debuggerModel().item(row);
    QCOMPARE(stored.unexpandedDisplayName(), QString("Edited"));
    QCOMPARE(stored.abis().size(), 2);
    QCOMPARE(stored.abis(), item.abis());
    QCOMPARE(stored.version(), QString("13.2 (Debian 13.2-1)"));
    QCOMPARE(stored.engineType(), GdbEngineType);
}

void DebuggersSettingsTest::testAnAutoDetectedDebuggerIsShownButNotEditable()
{
    const int row = rowThatIsAutoDetected();
    if (row < 0)
        QSKIP("No debugger was found on this machine, so there is no auto-detected one.");

    DebuggerAspects page;
    page.debuggers.setCurrentRow(row);
    QVERIFY(page.details.isVisible());
    QVERIFY(!page.displayName.isEnabled());
    QVERIFY(page.binary.isReadOnly());
    QVERIFY(page.workingDirectory.isReadOnly());
    QVERIFY(!page.debuggers.canRemove());

    // Only CDB has anything to say in the hint, and this one is not it.
    if (debuggerModel().item(row).engineType() != CdbEngineType)
        QVERIFY(!page.cdbHint.isVisible());
}

QObject *createDebuggersSettingsTest()
{
    return new DebuggersSettingsTest;
}

#endif // WITH_TESTS

// DebuggerSettingsPage

class DebuggerSettingsPage : public Core::IOptionsPage
{
public:
    DebuggerSettingsPage() {
        setId(ProjectExplorer::Constants::DEBUGGER_SETTINGS_PAGE_ID);
        setDisplayName(Tr::tr("Debuggers"));
        setCategory(ProjectExplorer::Constants::KITS_SETTINGS_CATEGORY);
        setSettingsProvider([] {
            static GuardedObject<DebuggerAspects> theDebuggerAspects;
            return theDebuggerAspects.get();
        });
    }
};

const DebuggerSettingsPage settingsPage;

} // namespace Debugger

#include "debuggeritemmanager.moc"
