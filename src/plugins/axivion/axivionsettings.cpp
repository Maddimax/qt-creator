// Copyright (C) 2022 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "axivionsettings.h"

#include "axivionplugin.h"
#include "axiviontr.h"

#ifdef WITH_TESTS
#include <QTest>
#endif

#include <coreplugin/dialogs/ioptionspage.h>
#include <coreplugin/credentialquery.h>
#include <coreplugin/icore.h>
#include <coreplugin/messagemanager.h>
#include <projectexplorer/project.h>
#include <projectexplorer/projectmanager.h>
#include <QtTaskTree/QTaskTree>

#include <utils/filedialogs.h>
#include <utils/algorithm.h>
#include <utils/fileutils.h>
#include <utils/guiutils.h>
#include <utils/id.h>
#include <utils/aspectlist.h>
#include <utils/layoutbuilder.h>
#include <utils/shutdownguard.h>
#include <utils/pathchooser.h>
#include <utils/qtcprocess.h>
#include <utils/stringutils.h>
#include <utils/utilsicons.h>

#include <QDialog>
#include <QDialogButtonBox>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QVBoxLayout>

using namespace Core;
using namespace Utils;
using namespace QtTaskTree;

namespace Axivion::Internal {

bool AxivionServer::operator==(const AxivionServer &other) const
{
    return id == other.id && dashboard == other.dashboard && username == other.username;
}

QJsonObject AxivionServer::toJson() const
{
    QJsonObject result;
    result.insert("id", id.toString());
    result.insert("dashboard", dashboard);
    result.insert("username", username);
    return result;
}

static QString fixUrl(const QString &url)
{
    const QString trimmed = Utils::trimBack(url, ' ');
    return trimmed.endsWith('/') ? trimmed : trimmed + '/';
}

AxivionServer AxivionServer::fromJson(const QJsonObject &json)
{
    const AxivionServer invalidServer;
    const QJsonValue id = json.value("id");
    if (id == QJsonValue::Undefined)
        return invalidServer;
    const QJsonValue dashboard = json.value("dashboard");
    if (dashboard == QJsonValue::Undefined)
        return invalidServer;
    const QJsonValue username = json.value("username");
    if (username == QJsonValue::Undefined)
        return invalidServer;
    return {Id::fromString(id.toString()), fixUrl(dashboard.toString()), username.toString()};
}

bool PathMapping::operator==(const PathMapping &other) const
{
    return projectName == other.projectName && analysisPath == other.analysisPath
            && localPath == other.localPath;
}

static Result<> analysisPathValid(const FilePath &analysisPath)
{
    if (analysisPath.isEmpty())
        return ResultOk;

    if (!analysisPath.isLocal())
        return ResultError(Tr::tr("Analysis path must be local."));

    static const QRegularExpression invalid("^(.*/)?\\.\\.?(/.*)?$");
    if (invalid.match(analysisPath.path()).hasMatch())
        return ResultError(Tr::tr("Invalid path elements (. or ..)."));

    return ResultOk;
}

bool PathMapping::isValid() const
 {
    return !projectName.isEmpty() && !localPath.isEmpty()
            && localPath.isLocal() && localPath.isAbsolutePath()
            && analysisPathValid(analysisPath);
}

static FilePath axivionJsonFilePath()
{
    return FilePath::fromString(ICore::settings()->fileName()).parentDir()
            .pathAppended("qtcreator/axivion.json");
}

static void writeAxivionJson(const FilePath &filePath, const QList<AxivionServer> &servers)
{
    QJsonDocument doc;
    QJsonArray serverArray;
    for (const AxivionServer &server : servers)
        serverArray.append(server.toJson());
    doc.setArray(serverArray);
    // FIXME error handling?
    filePath.writeFileContents(doc.toJson());
    filePath.setPermissions(QFile::ReadUser | QFile::WriteUser);
}

static QList<AxivionServer> readAxivionJson(const FilePath &filePath)
{
    if (!filePath.exists())
        return {};
    Result<QByteArray> contents = filePath.fileContents();
    if (!contents)
        return {};
    const QJsonDocument doc = QJsonDocument::fromJson(*contents);
    if (doc.isObject()) // old approach
        return { AxivionServer::fromJson(doc.object()) };
    if (!doc.isArray())
        return {};

    QList<AxivionServer> result;
    const QJsonArray serverArray = doc.array();
    for (const auto &serverValue : serverArray) {
        if (!serverValue.isObject())
            continue;
        result.append(AxivionServer::fromJson(serverValue.toObject()));
    }
    return result;
}

static QVariant pathMappingToVariant(const PathMapping &pm)
{
    QVariantMap m;
    m.insert("ProjectName", pm.projectName);
    m.insert("AnalysisPath", pm.analysisPath.toSettings());
    m.insert("LocalPath", pm.localPath.toSettings());
    return m;
}

static QVariant pathMappingsToSetting(const QList<PathMapping> &mappings)
{
    return Utils::transform(mappings,
                            [](const PathMapping &m) { return pathMappingToVariant(m); });
}

static PathMapping pathMappingFromVariant(const QVariant &m)
{
    const QVariantMap map = m.toMap();
    return map.isEmpty() ? PathMapping{}
                         : PathMapping{map.value("ProjectName").toString(),
                                       FilePath::fromSettings(map.value("AnalysisPath")),
                                       FilePath::fromSettings(map.value("LocalPath"))};
}

static QList<PathMapping> pathMappingsFromSetting(const QVariant &value)
{
    return Utils::transform(
                Utils::filtered(value.toList(), &QVariant::isValid), &pathMappingFromVariant);
}

// AxivionSettings

class PathMappingSettings final : public BaseAspect
{
public:
    PathMappingSettings()
    {
        setSettingsKey("Axivion/PathMappings");
    }

    void setVariantValue(const QVariant &value, Announcement howToAnnounce = DoEmit) final
    {
        m_pathMapping = pathMappingsFromSetting(value);
        if (howToAnnounce == DoEmit)
            emit changed();
    }

    QVariant variantValue() const final { return pathMappingsToSetting(m_pathMapping); }

    const QList<PathMapping> validPathMappings() const
    {
        return Utils::filtered(m_pathMapping, &PathMapping::isValid);
    }

    FilePath mappedFilePath(const FilePath &filePath, const QString &projectName) const
    {
        QTC_ASSERT(!projectName.isEmpty(), return {});
        QTC_ASSERT(filePath.exists(), return {});
        FilePath fallback;

        auto handleRelativeAnalysisPath = [](const PathMapping &pm, const FilePath &filePath) {
            const FilePath sub = filePath.relativeChildPath(pm.localPath);
            if (pm.analysisPath.isEmpty())
                return sub;
            else
                return pm.analysisPath.pathAppended(sub.path());
        };

        for (const PathMapping &pm : m_pathMapping) {
            if (pm.isValid() && projectName == pm.projectName) {
                if (pm.analysisPath.isAbsolutePath()) {
                    if (auto childPath = filePath.prefixRemoved(pm.localPath.path())) {
                        return pm.analysisPath.pathAppended(childPath->path());
                    }
                } else {
                    if (filePath.isChildOf(pm.localPath))
                        return handleRelativeAnalysisPath(pm, filePath);
                }
            } else if (fallback.isEmpty()) {
                if (filePath.isChildOf(pm.localPath))
                    fallback = handleRelativeAnalysisPath(pm, filePath);
            }
        }
        return fallback;
    }

    FilePath localProjectForProjectName(const QString &projectName) const
    {
        QTC_ASSERT(!projectName.isEmpty(), return {});
        return Utils::findOrDefault(m_pathMapping, [projectName](const PathMapping &pm) {
            return pm.isValid() && projectName == pm.projectName;
        }).localPath;
    }

    bool projectHasAnyPathMapping(const QString &projectName) const
    {
        return Utils::indexOf(m_pathMapping, [projectName](const PathMapping &pm) {
            return pm.projectName == projectName;
        }) != -1;
    }

private:
    QList<PathMapping> m_pathMapping;
};

static PathMappingSettings &pathMappingSettings()
{
    static PathMappingSettings thePathMapping;
    return thePathMapping;
}

AxivionSettings &settings()
{
    static AxivionSettings theSettings;
    return theSettings;
}

AxivionSettings::AxivionSettings()
{
    setSettingsGroup("Axivion");
    setAutoApply(false);

    highlightMarks.setSettingsKey("HighlightMarks");
    highlightMarks.setLabelText(Tr::tr("Highlight marks"));
    highlightMarks.setToolTip(Tr::tr("Marks issues on the scroll bar."));
    highlightMarks.setDefaultValue(false);

    axivionSuitePath.setSettingsKey("SuitePath");
    axivionSuitePath.setExpectedKind(PathChooserKind::ExistingDirectory);
    axivionSuitePath.setAllowPathFromDevice(false);
    axivionSuitePath.setLabelText(Tr::tr("Axivion Suite path:"));

    saveOpenFiles.setSettingsKey("SaveOpenFiles");
    saveOpenFiles.setLabelText(Tr::tr("Save all open files before starting an analysis"));

    bauhausPython.setSettingsKey("BauhausPython");
    bauhausPython.setExpectedKind(PathChooserKind::ExistingCommand);
    bauhausPython.setAllowPathFromDevice(false);
    bauhausPython.setLabelText("BAUHAUS_PYTHON:");
    bauhausPython.setToolTip(Tr::tr("Path to python executable.\nSet it to overwrite global "
                                    "environment or if Axivion fails to find python in PATH."));

    javaHome.setSettingsKey("JavaHome");
    javaHome.setExpectedKind(PathChooserKind::ExistingDirectory);
    javaHome.setAllowPathFromDevice(false);
    javaHome.setLabelText("JAVA_HOME:");
    javaHome.setToolTip(Tr::tr("Set it to overwrite global environment or if Axivion fails to "
                               "find java in PATH."));

    // Drawn by the local build dialog, not by this page. No settings group,
    // so LastLocalBuildCmd stays where it was saved.
    localBuild.setQmlSource(QUrl("qrc:/qt/qml/QtCreator/Axivion/LocalBuildDialog.qml"));

    localBuildModifyWarning.setQmlName("ModifyWarning");
    localBuildModifyWarning.setIconType(AspectControls::InfoType::Warning);
    localBuildModifyWarning.setText(
        Tr::tr("Modifying source files during the local build may produce unexpected "
               "warnings, errors, or wrong results."));

    localBuildConfigWarning.setQmlName("ConfigWarning");
    localBuildConfigWarning.setIconType(AspectControls::InfoType::Warning);
    localBuildConfigWarning.setText(
        Tr::tr("If your build is not configured for local build, you may overwrite output "
               "files of your native compiler when starting a local build."));

    localBuildVersionHint.setQmlName("VersionHint");
    localBuildVersionHint.setText(
        Tr::tr("Choose the same Axivion Suite version as your CI build uses "
               "or the results may differ."));

    localBuildSuite.setQmlName("Suite");
    localBuildSuite.setLabelText(Tr::tr("Axivion Suite installation directory:"));
    localBuildSuite.setExpectedKind(PathChooserKind::ExistingDirectory);
    localBuildSuite.setAllowPathFromDevice(false);

    lastLocalBuildCommand.setQmlName("Command");
    lastLocalBuildCommand.setSettingsKey("LastLocalBuildCmd"); // used outside settings
    lastLocalBuildCommand.setExpectedKind(PathChooserKind::Any);
    lastLocalBuildCommand.setAllowPathFromDevice(false);
    lastLocalBuildCommand.setHistoryCompleter("LocalBuildHistory");

    localBuildType.setQmlName("BuildType");
    localBuildType.setLabelText(Tr::tr("Build type:"));
    localBuildType.setDisplayStyle(SelectionAspect::DisplayStyle::ComboBox);
    localBuildType.setToolTip(
        Tr::tr("Clean Build: Set environment variable AXIVION_CLEAN_BUILD=1\n"
               "Incremental Build: Set environment variable AXIVION_INCREMENTAL_BUILD=1"));
    localBuildType.addOption("");
    localBuildType.addOption(Tr::tr("Clean Build"));
    localBuildType.addOption(Tr::tr("Incremental Build"));

    // Drawn by the single file analysis dialog, not by this page.
    singleFileAnalysis.setQmlSource(
        QUrl("qrc:/qt/qml/QtCreator/Axivion/SingleFileAnalysisDialog.qml"));

    lastBauhausConfig.setQmlName("BauhausConfig");
    lastBauhausConfig.setSettingsKey("LastBauhausConfig"); // used outside settings
    lastBauhausConfig.setLabelText(Tr::tr("BAUHAUS_CONFIG Directory"));
    lastBauhausConfig.setExpectedKind(PathChooserKind::ExistingDirectory);
    lastBauhausConfig.setAllowPathFromDevice(false);
    lastBauhausConfig.setHistoryCompleter("Axivion.SFABauhausConfig");

    bauhausConfigHint.setQmlName("BauhausConfigHint");
    bauhausConfigHint.setText(
        Tr::tr("Usually the directory containing the file \"axivion_config.json\"."));

    lastSfaCommand.setQmlName("SfaCommand");
    lastSfaCommand.setSettingsKey("LastSfaCmd"); // used outside settings
    lastSfaCommand.setLabelText(Tr::tr("Analysis Command"));
    lastSfaCommand.setExpectedKind(PathChooserKind::Any);
    lastSfaCommand.setAllowPathFromDevice(false);
    lastSfaCommand.setHistoryCompleter("Axivion.SFACommand");

    sfaCommandHint.setQmlName("SfaCommandHint");
    sfaCommandHint.setText(
        "build_compile_commands --single_file %{CurrentDocument:FilePath} "
        "%{ActiveProject:BuildConfig:Path}/compile_commands.json\n"
        //: the text is preceded by a command to execute
        + Tr::tr("or some shell/batch script holding cafeCC / axivion_analysis commands"
                 " to execute.")
              .append("\n\n")
              .append(Tr::tr("Leave empty to derive from active project. File to analyze "
                             "must be part of the active project.")));

    noProjectWarning.setQmlName("NoProjectWarning");
    noProjectWarning.setIconType(AspectControls::InfoType::Warning);
    //: %1 is a Qt Creator variable string
    noProjectWarning.setText(Tr::tr("No active project. Referring to %1 will fail.")
                                 .arg("<code>%{ActiveProject:...}</code>"));

    defaultIssueKind.setSettingsKey("DefaultIssueKind"); // used without UI
    defaultIssueKind.setDefaultValue("SV"); // style violations

    m_defaultServerId.setSettingsKey("DefaultDashboardId");
    pathMappingSettings().readSettings();
    AspectContainer::readSettings();
    if (!axivionSuitePath().isEmpty())
        validatePath();

    m_allServers = readAxivionJson(axivionJsonFilePath());

    if (m_allServers.size() == 1 && m_defaultServerId().isEmpty()) // handle settings transition
        m_defaultServerId.setValue(m_allServers.first().id.toString());

    connect(&axivionSuitePath, &BaseAspect::changed, this, [this] { m_versionInfo.reset(); });

    axivionSuitePath.addOnVolatileValueChanged(this, markSettingsDirty);
    bauhausPython.addOnVolatileValueChanged(this, markSettingsDirty);
    javaHome.addOnVolatileValueChanged(this, markSettingsDirty);
    highlightMarks.addOnVolatileValueChanged(this, markSettingsDirty);
    saveOpenFiles.addOnVolatileValueChanged(this, markSettingsDirty);
    // m_defaultServerId.addOnVolatileValueChanged(this, markSettingsDirty); // TODO
}

void AxivionSettings::toSettings() const
{
    writeAxivionJson(axivionJsonFilePath(), m_allServers);
    AspectContainer::writeSettings();
}

Id AxivionSettings::defaultDashboardId() const
{
    return Id::fromString(m_defaultServerId());
}

const AxivionServer AxivionSettings::defaultServer() const
{
    return serverForId(defaultDashboardId());
}

const AxivionServer AxivionSettings::serverForId(const Utils::Id &id) const
{
    return Utils::findOrDefault(m_allServers, [&id](const AxivionServer &server) {
        return id == server.id;
    });
}

void AxivionSettings::disableCertificateValidation(const Utils::Id &id)
{
    const int index = Utils::indexOf(m_allServers, [&id](const AxivionServer &server) {
        return id == server.id;
    });
    if (index == -1)
        return;

    m_allServers[index].validateCert = false;
}

bool AxivionSettings::updateDashboardServers(const QList<AxivionServer> &other,
                                             const Utils::Id &selected)
{
    const Id oldDefault = defaultDashboardId();
    if (selected == oldDefault && m_allServers == other)
        return false;


    // collect dashserver items that have been removed,
    // so we can delete the api tokens from the credentials store
    const QStringList previousKeys = Utils::transform(m_allServers, &credentialKey);
    const QStringList updatedKeys = Utils::transform(other, &credentialKey);
    const QStringList keysToRemove = Utils::filtered(previousKeys, [updatedKeys](const QString &key) {
        return !updatedKeys.contains(key);
    });

    m_defaultServerId.setValue(selected.toString(), BeQuiet);
    m_allServers = other;
    emit serversChanged(); // should we be more detailed? (id)

    const ListIterator iterator(keysToRemove);

    const auto onDeleteKeySetup = [iterator](CredentialQuery &query) {
        MessageManager::writeSilently(Tr::tr("Axivion: Deleting API token for %1 as respective "
                                             "dashboard server was removed.")
                                          .arg(*iterator));
        query.setOperation(CredentialOperation::Delete);
        query.setService(s_axivionKeychainService);
        query.setKey(*iterator);
    };

    const Group recipe = For (iterator) >> Do {
        CredentialQueryTask(onDeleteKeySetup)
    };
    m_taskTreeRunner.start(recipe);
    return true;
}

const QList<PathMapping> AxivionSettings::validPathMappings() const
{
    return pathMappingSettings().validPathMappings();
}

FilePath AxivionSettings::mappedFilePath(const FilePath &filePath,
                                         const QString &projectName) const
{
    return pathMappingSettings().mappedFilePath(filePath, projectName);
}

Utils::FilePath AxivionSettings::localProjectForProjectName(const QString &projectName) const
{
    return pathMappingSettings().localProjectForProjectName(projectName);
}


void AxivionSettings::validatePath()
{
    if (m_versionInfo) {
        emit suitePathValidated();
        return;
    }

    const FilePath &suitePath = axivionSuitePath();
    const FilePath info = (suitePath.isEmpty() ? FilePath{"axivion_suite_info"}
                                               : suitePath.pathAppended("bin/axivion_suite_info"))
            .withExecutableSuffix();

    const auto onSetup = [info](Process &process) {
        process.setCommand({info, {"--json"}});
    };
    const auto onDone = [this](const Process &process) {
        const auto onFinish = qScopeGuard([this] { emit suitePathValidated(); });
        m_versionInfo.reset();
        if (process.result() != ProcessResult::FinishedWithSuccess)
            return;

        const QString output = process.allOutput();
        QJsonParseError error;
        const QJsonDocument doc = QJsonDocument::fromJson(output.toUtf8(), &error);
        if (error.error != QJsonParseError::NoError)
            return;
        if (!doc.isObject())
            return;
        const QJsonObject obj = doc.object();
        const QString version = obj.value("versionNumber").toString();
        const QString date = obj.value("dateTime").toString();
        m_versionInfo.emplace(AxivionVersionInfo{version, date});
    };

    m_taskTreeRunner.start({ProcessTask(onSetup, onDone)});
}

static QString escapeKey(const QString &string)
{
    QString escaped = string;
    return escaped.replace('\\', "\\\\").replace('@', "\\@");
}

QString credentialKey(const AxivionServer &server)
{
    return escapeKey(server.username) + '@' + escapeKey(server.dashboard);
}

// AxivionSettingsPage

// may allow some invalid, but does some minimal check for legality
static bool hostValid(const QString &host)
{
    static const QRegularExpression ip(R"(^(\d+).(\d+).(\d+).(\d+)$)");
    static const QRegularExpression dn(R"(^([a-zA-Z0-9][a-zA-Z0-9-]+\.)*[a-zA-Z0-9][a-zA-Z0-9-]+$)");
    const QRegularExpressionMatch match = ip.match(host);
    if (match.hasMatch()) {
        for (int i = 1; i < 5; ++i) {
            int val = match.captured(i).toInt();
            if (val < 0 || val > 255)
                return false;
        }
        return true;
    }
    return dn.match(host).hasMatch();
}

static bool isUrlValid(const QString &in)
{
    const QUrl url(in);
    return hostValid(url.host()) && (url.scheme() == "https" || url.scheme() == "http");
}

class DashboardSettingsWidget : public QWidget
{
public:
    explicit DashboardSettingsWidget(QWidget *parent, QPushButton *ok = nullptr);

    AxivionServer dashboardServer() const;
    void setDashboardServer(const AxivionServer &server);

    bool isValid() const;

private:
    Id m_id;
    StringAspect m_dashboardUrl;
    StringAspect m_username;
    BoolAspect m_isAnonAuth;
    BoolAspect m_valid;
};

DashboardSettingsWidget::DashboardSettingsWidget(QWidget *parent, QPushButton *ok)
    : QWidget(parent)
{
    m_dashboardUrl.setLabelText(Tr::tr("Dashboard URL:"));
    m_dashboardUrl.setDisplayStyle(StringAspect::LineEditDisplay);
    m_dashboardUrl.setValidationFunction([](const QString &text) -> Result<> {
        if (isUrlValid(text))
            return ResultOk;
        return ResultError(QString());
    });

    m_username.setLabelText(Tr::tr("Username:"));
    m_username.setDisplayStyle(StringAspect::LineEditDisplay);
    m_username.setPlaceHolderText(Tr::tr("User name"));

    m_isAnonAuth.setLabelText(Tr::tr("Anonymous authentication"));
    m_isAnonAuth.setDefaultValue(true);

    using namespace Layouting;

    Form {
        m_dashboardUrl, br,
        m_isAnonAuth, br,
        m_username, br,
        noMargin
    }.attachTo(this);

    QTC_ASSERT(ok, return);
    auto checkValidity = [this, ok] {
        m_valid.setValue(isValid());
        ok->setEnabled(m_valid());
    };
    auto anonAuthChanged = [this] {
        const bool anonymous = m_isAnonAuth();
        if (anonymous)
            m_username.setValue("anon_auth");
        m_username.setEnabled(!anonymous);
    };
    m_dashboardUrl.addOnChanged(this, checkValidity);
    m_isAnonAuth.addOnChanged(this, anonAuthChanged);
    m_username.addOnChanged(this, checkValidity);
}

AxivionServer DashboardSettingsWidget::dashboardServer() const
{
    AxivionServer result;
    if (m_id.isValid())
        result.id = m_id;
    else
        result.id = Id::generate();
    result.dashboard = fixUrl(m_dashboardUrl());
    result.username = m_username();
    return result;
}

void DashboardSettingsWidget::setDashboardServer(const AxivionServer &server)
{
    m_id = server.id;
    m_dashboardUrl.setValue(server.dashboard);
    m_username.setValue(server.username);
    m_isAnonAuth.setValue(server.username == "anon_auth");
    m_username.setEnabled(!m_isAnonAuth());
}

bool DashboardSettingsWidget::isValid() const
{
    return isUrlValid(m_dashboardUrl());
}

// PathMappingSettingsWidget

class PathMappingDetails : public AspectContainer
{
public:
    PathMappingDetails()
    {
        m_projectName.setQmlName("ProjectName");
        m_analysisPath.setQmlName("AnalysisPath");
        m_localPath.setQmlName("LocalPath");

        m_projectName.setLabelText(Tr::tr("Project name:"));
        m_projectName.setDisplayStyle(StringAspect::LineEditDisplay);
        m_projectName.setValidationFunction([](const QString &text) -> Result<> {
            if (text.isEmpty())
                return ResultError(Tr::tr("Project name must be non-empty."));
            return ResultOk;
        });
        m_projectName.setToolTip(Tr::tr("Project name as it appears in the global dashboard."));
        m_projectName.setShowToolTipOnLabel(true);

        m_analysisPath.setLabelText(Tr::tr("Analysis path:"));
        m_analysisPath.setDisplayStyle(StringAspect::LineEditDisplay);
        m_analysisPath.setValidationFunction([](const QString &text) -> Result<> {
            QString input = text;
            // do NOT use fromUserInput() as this also cleans the path
            const FilePath fp = FilePath::fromString(input.replace('\\', '/'));
            return analysisPathValid(fp);
        });
        m_analysisPath.setToolTip(Tr::tr("Root path of the analyzed project relative to the used "
                                         "project path inside the dashboard.\nLeave empty if the "
                                         "analyzed project refers to the basepath of the analyzed "
                                         "project."));
        m_analysisPath.setShowToolTipOnLabel(true);

        m_localPath.setLabelText(Tr::tr("Local path:"));
        m_localPath.setExpectedKind(PathChooserKind::ExistingDirectory);
        m_localPath.setAllowPathFromDevice(false);
        m_localPath.setToolTip(Tr::tr("Local directory path corresponding to the analysis path."));
        m_localPath.setShowToolTipOnLabel(true);

        setQmlSource(QUrl("qrc:/qt/qml/QtCreator/Axivion/PathMappingForm.qml"));

        m_projectName.addOnVolatileValueChanged(this, markSettingsDirty);
        m_analysisPath.addOnVolatileValueChanged(this, markSettingsDirty);
        m_localPath.addOnVolatileValueChanged(this, markSettingsDirty);
    }

    void updateContent(const PathMapping &mapping)
    {
        m_projectName.setValue(mapping.projectName, BaseAspect::BeQuiet);
        m_analysisPath.setValue(mapping.analysisPath.path(), BaseAspect::BeQuiet);
        m_localPath.setValue(mapping.localPath, BaseAspect::BeQuiet);
    }

    PathMapping toPathMapping() const
    {
        return PathMapping{
            m_projectName(), FilePath::fromUserInput(m_analysisPath()), m_localPath()
        };
    }

private:
    StringAspect m_projectName{this};
    StringAspect m_analysisPath{this};
    FilePathAspect m_localPath{this};
};

// What the settings page edits. The dashboard servers and the path mappings
// are lists the page keeps until Apply, which is where they are handed to the
// settings; the settings themselves are shown as they are, through the
// container the page holds rather than owns.
class AxivionSettingsAspects final : public AspectContainer
{
public:
    AxivionSettingsAspects();

    void apply() override;
    void cancel() override;

private:
    void refreshServers();
    void setServerOptions();
    void showServerDialog(bool add);
    void removeCurrentServer();
    void loadMappings();
    void updateVersionInfo();
    PathMappingDetails *currentMapping() const;

    // The servers as the page has them, which is what Apply writes.
    QList<AxivionServer> m_servers;

    ContainerAspect m_suite{this};
    SelectionAspect m_server{this};
    ActionAspect m_addServer{this};
    ActionAspect m_editServer{this};
    ActionAspect m_removeServer{this};
    AspectList m_mappings{this};
    TextDisplay m_version{this};
    TextDisplay m_buildDate{this};
    TextDisplay m_supportNote{this};
};

AxivionSettingsAspects::AxivionSettingsAspects()
{
    setAutoApply(false);
    setQmlSource(QUrl("qrc:/qt/qml/QtCreator/Axivion/AxivionSettingsPage.qml"));

    // The settings themselves are the plugin's own object, shown here rather
    // than copied: naming them as sub-aspects would re-home them, and an
    // aspect's container is what its settings key is resolved against.
    m_suite.setQmlName("Suite");
    m_suite.setContainer(&settings());

    m_server.setQmlName("Server");
    m_server.setLabelText(Tr::tr("Default dashboard server:"));
    m_server.setDisplayStyle(SelectionAspect::DisplayStyle::ComboBox);

    m_addServer.setQmlName("AddServer");
    m_addServer.setActionText(Tr::tr("Add..."));
    m_addServer.setAction([this] {
        // An empty item, defaulting to anonymous authentication, so that the
        // dialog has something to edit and Cancel has something to take away.
        m_servers.append(AxivionServer{{}, {}, "anon_auth", true});
        m_server.setValue(int(m_servers.size()) - 1);
        showServerDialog(true);
    });

    m_editServer.setQmlName("EditServer");
    m_editServer.setActionText(Tr::tr("Edit..."));
    m_editServer.setAction([this] { showServerDialog(false); });

    m_removeServer.setQmlName("RemoveServer");
    m_removeServer.setActionText(Tr::tr("Remove"));
    m_removeServer.setAction([this] { removeCurrentServer(); });

    m_mappings.setQmlName("Mappings");
    m_mappings.setDisplayStyle(AspectList::DisplayStyle::ListViewWithDetails);
    // A file is looked up in the mappings in order, so the order is the user's.
    m_mappings.setOrdered(true);
    m_mappings.setCreateItemFunction([] { return std::make_shared<PathMappingDetails>(); });
    m_mappings.listViewDataCallback = [](PathMappingDetails *item, int role) -> QVariant {
        const PathMapping mapping = item->toPathMapping();
        if (role == Qt::DisplayRole) {
            return QString("%1: %2 \342\206\222 %3").arg(mapping.projectName,
                                                         mapping.analysisPath.path(),
                                                         mapping.localPath.toUserOutput());
        }
        // A mapping that would map nothing says so on its row, as the tree did.
        if (role == Qt::DecorationRole && !mapping.isValid())
            return Icons::CRITICAL.icon();
        return {};
    };

    m_version.setQmlName("Version");
    m_version.setLabelText(Tr::tr("Version:"));

    m_buildDate.setQmlName("BuildDate");
    m_buildDate.setLabelText(Tr::tr("Build date:"));

    m_supportNote.setQmlName("Support");
    m_supportNote.setText(Tr::tr("Contact support@axivion.com if you need assistance."));

    // Behaviour, not layout.
    connect(&settings().axivionSuitePath, &BaseAspect::changed,
            &settings(), &AxivionSettings::validatePath);
    connect(&settings(), &AxivionSettings::suitePathValidated,
            this, [this] { updateVersionInfo(); });
    connect(&m_server, &BaseAspect::volatileValueChanged, this, [this] {
        const bool any = !m_servers.isEmpty();
        m_editServer.setEnabled(any);
        m_removeServer.setEnabled(any);
    });

    refreshServers();
    loadMappings();
    updateVersionInfo();
    settings().validatePath();
}

void AxivionSettingsAspects::setServerOptions()
{
    m_server.clearOptions();
    for (const AxivionServer &server : std::as_const(m_servers))
        m_server.addOption(server.displayString());
}

void AxivionSettingsAspects::refreshServers()
{
    m_servers = settings().allAvailableServers();
    setServerOptions();
    const int index = Utils::indexOf(m_servers,
                                     [id = settings().defaultDashboardId()](const AxivionServer &s) {
                                         return id == s.id;
                                     });
    m_server.setValue(m_servers.isEmpty() ? -1 : qMax(0, index));

    const bool any = !m_servers.isEmpty();
    m_editServer.setEnabled(any);
    m_removeServer.setEnabled(any);
}

void AxivionSettingsAspects::removeCurrentServer()
{
    const int row = m_server.volatileValue();
    QTC_ASSERT(row >= 0 && row < m_servers.size(), return);
    const QString config = m_servers.at(row).displayString();
    if (QMessageBox::question(
            ICore::dialogParent(),
            Tr::tr("Remove Server Configuration"),
            Tr::tr("Remove the server configuration \"%1\"?").arg(config))
        != QMessageBox::Yes) {
        return;
    }
    m_servers.removeAt(row);
    setServerOptions();
    m_server.setValue(m_servers.isEmpty() ? -1 : qMin(row, int(m_servers.size()) - 1));
    markSettingsDirty();
}

void AxivionSettingsAspects::showServerDialog(bool add)
{
    const int row = m_server.volatileValue();
    QTC_ASSERT(row >= 0 && row < m_servers.size(), return);
    const AxivionServer old = m_servers.at(row);

    QDialog dialog(ICore::dialogParent());
    dialog.setWindowTitle(add ? Tr::tr("Add Dashboard Configuration")
                              : Tr::tr("Edit Dashboard Configuration"));
    auto buttons = new QDialogButtonBox(QDialogButtonBox::Cancel | QDialogButtonBox::Ok, &dialog);
    auto ok = buttons->button(QDialogButtonBox::Ok);
    auto dashboardWidget = new DashboardSettingsWidget(&dialog, ok);
    dashboardWidget->setDashboardServer(old);
    ok->setEnabled(dashboardWidget->isValid());
    connect(buttons->button(QDialogButtonBox::Cancel), &QPushButton::clicked,
            &dialog, &QDialog::reject);
    connect(ok, &QPushButton::clicked, &dialog, &QDialog::accept);

    Layouting::Column {
        dashboardWidget,
        buttons
    }.attachTo(&dialog);
    dialog.resize(500, 200);

    if (dialog.exec() != QDialog::Accepted) {
        if (add) { // if we canceled an add, remove the canceled item
            m_servers.removeAt(row);
            refreshServers();
        }
        return;
    }
    if (dashboardWidget->isValid()) {
        const AxivionServer server = dashboardWidget->dashboardServer();
        if (server != old) {
            m_servers[row] = server;
            setServerOptions();
            m_server.setValue(row);
            markSettingsDirty();
        }
    }
}

void AxivionSettingsAspects::loadMappings()
{
    m_mappings.clear();
    for (const PathMapping &mapping : settings().validPathMappings()) {
        auto item = std::make_shared<PathMappingDetails>();
        item->updateContent(mapping);
        m_mappings.addItem(item);
    }
    // What was loaded is what Cancel goes back to; an AspectList keeps the
    // items that were added apart from the ones that were applied.
    m_mappings.apply();
}

void AxivionSettingsAspects::updateVersionInfo()
{
    const std::optional<AxivionVersionInfo> info = settings().versionInfo();
    m_version.setText(info ? info->versionNumber : QString{});
    m_buildDate.setText(info ? info->dateTime : QString{});
}

void AxivionSettingsAspects::apply()
{
    const int row = m_server.volatileValue();
    const Id selected = row >= 0 && row < m_servers.size() ? m_servers.at(row).id : Id{};
    const bool dirty = settings().isDirty();
    const bool serversChanged = settings().updateDashboardServers(m_servers, selected);

    AspectContainer::apply();
    settings().apply();
    if (dirty || serversChanged)
        settings().toSettings();

    const QList<PathMapping> oldMappings = settings().validPathMappings();
    QList<PathMapping> newMappings;
    m_mappings.forEachItem([&newMappings](const std::shared_ptr<PathMappingDetails> &item) {
        newMappings.append(item->toPathMapping());
    });
    if (oldMappings != newMappings) {
        pathMappingSettings().setVariantValue(pathMappingsToSetting(newMappings));
        pathMappingSettings().writeSettings();
    }
    refreshServers();
}

void AxivionSettingsAspects::cancel()
{
    AspectContainer::cancel();
    settings().cancel();
    pathMappingSettings().cancel();
    refreshServers();
    loadMappings();
}


static PathMapping showPathMappingsDialog(const PathMapping &suggested)
{
    QDialog dialog(ICore::dialogParent());
    dialog.setWindowTitle(Tr::tr("Missing Path Mapping"));
    auto label = new QLabel(Tr::tr("Configure a valid path mapping for \"%1\" to open "
                                   "files for this project.").arg(suggested.projectName), &dialog);
    auto buttons = new QDialogButtonBox(QDialogButtonBox::Cancel | QDialogButtonBox::Ok, &dialog);
    auto ok = buttons->button(QDialogButtonBox::Ok);
    auto mappingWidget = new QWidget(&dialog);
    PathMappingDetails details;
    details.updateContent(suggested);
    Layouting::Column{Core::createAspectForm(&details), Layouting::noMargin}
        .attachTo(mappingWidget);

    ok->setEnabled(suggested.isValid()
                   && suggested.localPath.resolvePath(suggested.analysisPath).exists());
    QObject::connect(buttons->button(QDialogButtonBox::Cancel),
                     &QPushButton::clicked, &dialog, &QDialog::reject);
    QObject::connect(ok, &QPushButton::clicked, &dialog, &QDialog::accept);
    QObject::connect(&details, &BaseAspect::changed, &dialog, [&details, ok] {
        const PathMapping pm = details.toPathMapping();
        ok->setEnabled(pm.isValid() && pm.localPath.resolvePath(pm.analysisPath).exists());
    });

    Layouting::Column {
        label,
        mappingWidget,
        buttons,
    }.attachTo(&dialog);
    dialog.resize(500, 200);

    if (dialog.exec() != QDialog::Accepted)
        return {};

    return details.toPathMapping();
}

static PathMapping showPathMappingFileOpenDialog(const FilePath &missingPath,
                                                 const QString &projectName)
{
    FilePath result = FileUtils::getOpenFilePath(
        Tr::tr("Select local file for \"%1\"").arg(missingPath.path()),
        {}, missingPath.fileName());
    if (result.isEmpty() || missingPath.fileName() != result.fileName())
        return {};
    // create mapping for the selected file and return it
    std::optional<FilePath> analysisPath = std::nullopt;
    std::optional<FilePath> local = result.tailRemoved(missingPath.path());
    if (!local) {
        local = result.tailRemoved(missingPath.fileName());
        analysisPath = missingPath.tailRemoved(missingPath.fileName());
    }
    QTC_ASSERT(local, return {});
    return PathMapping{projectName, analysisPath ? *analysisPath : FilePath{}, *local};
}

bool handleMissingPathMapping(const FilePath &missingPath, const QString &projectName)
{
    const bool hasAnyMapping = pathMappingSettings().projectHasAnyPathMapping(projectName);

    QMessageBox mbox(ICore::dialogParent());
    mbox.setWindowTitle(Tr::tr("Missing Path Mapping"));
    if (hasAnyMapping) {
        mbox.setText(Tr::tr("No matching path mapping for \"%1\" configured.").arg(missingPath.path()));
        mbox.setInformativeText(Tr::tr("To open this file, you need to change the existing or add "
                                       "another valid path mapping.\n"
                                       "This may include changing the order of mappings."));
    } else {
        mbox.setText(Tr::tr("No path mapping for \"%1\" configured.").arg(missingPath.path()));
        mbox.setInformativeText(Tr::tr("To open files for this project, specify a valid "
                                       "path mapping or select a matching local file."));
    }
    QPushButton *filechooser = nullptr;
    if (!hasAnyMapping) {
        filechooser = new QPushButton(Tr::tr("Select Matching File..."));
        mbox.addButton(filechooser, QMessageBox::AcceptRole);
    }
    QPushButton *manual = new QPushButton(hasAnyMapping ? Tr::tr("Change Existing...")
                                                        : Tr::tr("Set up Manually..."), &mbox);
    mbox.addButton(manual, QMessageBox::ActionRole);
    QPushButton *cancel = mbox.addButton(QMessageBox::Cancel);

    mbox.exec();
    QAbstractButton *clicked = mbox.clickedButton();
    if (!clicked || clicked == cancel)
        return false;

    PathMapping userInput;
    if (clicked == filechooser) {
        userInput = showPathMappingFileOpenDialog(missingPath, projectName);
    } else {
        // else manually set up / change existing has been clicked
        if (hasAnyMapping) {
            // present axivion options to modify existing
            // FIXME? we have no way to give a hint regarding the missing file path, should we
            // put the path into the clipboard at least?
            ICore::showSettings("Analyzer.Axivion.Settings");
            return false;
        } else {
            ProjectExplorer::Project *startupProj = ProjectExplorer::ProjectManager::startupProject();
            const FilePath computedPath = startupProj ? findFileForIssuePath(missingPath)
                                                      : FilePath{};
            PathMapping suggested;
            suggested.projectName = projectName;
            if (computedPath.exists()) {
                suggested.localPath = computedPath.chopped(
                            missingPath.pathView().size() + 1);
            }
            userInput = showPathMappingsDialog(suggested);
        }
    }
    if (!userInput.isValid())
        return false;

    QList<PathMapping> mappings = settings().validPathMappings();
    if (mappings.contains(userInput)) // do not store an already existing mapping
        return false;
    // add and store the new mapping
    mappings.append(userInput);
    pathMappingSettings().setVariantValue(pathMappingsToSetting(mappings));
    pathMappingSettings().writeSettings();

    return true;
}

// settings pages

class AxivionSettingsPage : public IOptionsPage
{
public:
    AxivionSettingsPage()
    {
        setId("Analyzer.Axivion.Settings");
        setDisplayName(Tr::tr("Axivion"));
        setCategory("T.Analyzer");
        setSettingsProvider([] {
            static GuardedObject<AxivionSettingsAspects> theAspects;
            return theAspects.get();
        });
    }
};

const AxivionSettingsPage generalSettingsPage;

#ifdef WITH_TESTS

// The dialog that asks for a missing path mapping draws PathMappingDetails
// through its form. Nothing else renders that form - the settings page draws
// list items generically - so this is what says the form's names still match
// the aspects'.
class AxivionPathMappingFormTest final : public QObject
{
    Q_OBJECT

private slots:
    void testTheFormDrawsEveryFieldOfAMapping()
    {
        PathMappingDetails details;
        details.updateContent(
            PathMapping{"a-project", FilePath::fromUserInput("/analysis"), FilePath("/local")});

        // A name the form gets wrong is not a load error - the page still
        // builds - so the evidence is the diagnostic. QML reports an
        // unresolved aspects.Foo as an assignment of undefined.
        static QStringList messages;
        messages.clear();
        QtMessageHandler previous = qInstallMessageHandler(
            [](QtMsgType, const QMessageLogContext &, const QString &text) {
                messages.append(text);
            });
        const std::unique_ptr<QWidget> form(Core::createAspectForm(&details));
        qInstallMessageHandler(previous);

        QVERIFY(form);
        const QStringList unresolved = Utils::filtered(messages, [](const QString &text) {
            return text.contains("Unable to assign");
        });
        QVERIFY2(unresolved.isEmpty(), qPrintable(unresolved.join("; ")));

        // And it was Qt Quick that drew it, asked through the metaobject
        // rather than by making Axivion link Qt Quick for a test.
        QWidget *quick = nullptr;
        for (QWidget *child : form->findChildren<QWidget *>()) {
            if (qstrcmp(child->metaObject()->className(), "QQuickWidget") == 0)
                quick = child;
        }
        QVERIFY2(quick, "the mapping was not drawn with Qt Quick");
        constexpr int quickWidgetReady = 1; // QQuickWidget::Ready
        QCOMPARE(quick->property("status").toInt(), quickWidgetReady);
    }
};

QObject *createAxivionPathMappingFormTest()
{
    return new AxivionPathMappingFormTest;
}

#endif // WITH_TESTS

} // Axivion::Internal

#ifdef WITH_TESTS
#include "axivionsettings.moc"
#endif
