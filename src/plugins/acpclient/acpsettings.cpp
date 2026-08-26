// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "acpsettings.h"
#include "acpclienttr.h"

#include <acp/acpregistry.h>

#include <coreplugin/coreconstants.h>
#include <coreplugin/dialogs/ioptionspage.h>
#include <coreplugin/icore.h>

#include <utils/aspectwidgets.h>
#include <utils/fileutils.h>
#include <utils/appinfo.h>
#include <utils/aspectlist.h>
#include <utils/aspects.h>
#include <utils/co_result.h>
#include <utils/environmentchangesaspect.h>
#include <utils/async.h>
#include <utils/environmentdialog.h>
#include <utils/filestreamer.h>
#include <utils/globaltasktree.h>
#include <utils/layoutbuilder.h>
#include <utils/networkaccessmanager.h>
#include <utils/pathvalidation.h>
#include <utils/temporarydirectory.h>
#include <utils/temporaryfile.h>
#include <utils/infolabel.h>
#include <utils/theme/theme.h>

#include <QComboBox>
#include <QCryptographicHash>
#include <QDir>
#include <QPromise>
#include <QPushButton>
#include <QStandardItemModel>
#include <QUuid>
#include <QtTaskTree/QNetworkReplyWrapper>
#include <QtTaskTree/QParallelTaskTreeRunner>

#ifdef WITH_TESTS
#include <QTest>
#endif

using namespace Utils;

namespace AcpClient::Internal {

using namespace QtTaskTree;

using SharedTempFile = std::shared_ptr<TemporaryFilePath>;

// Persistent icon cache: stores themed SVGs under userResourcePath("acpclient/icons")
// keyed by a hash of the URL. Survives restarts.

static Result<> createSvgFile(
    QByteArray data,
    const FilePath &destPath,
    const QString themeColor,
    const std::shared_ptr<QPromise<QIcon>> &promise)
{
    data.replace("currentColor", themeColor.toUtf8());
    co_await destPath.parentDir().ensureWritableDir();
    co_await destPath.writeFileContents(data);

    promise->addResult(QIcon(destPath.toFSPathString()));
    co_return ResultOk;
};

static void fetchIconToPersistentCache(const QString &url, const std::shared_ptr<QPromise<QIcon>> &promise)
{
    if (url.isEmpty()) {
        promise->finish();
        return;
    }

    const QString themeColor = creatorColor(Theme::PanelTextColorDark).toRgb().name();

    const QByteArray hash = QCryptographicHash::hash((url + themeColor).toUtf8(), QCryptographicHash::Sha1);
    const FilePath destPath = Core::ICore::userResourcePath("acpclient/icons")
        / QString::fromLatin1(hash.toHex() + ".svg");

    if (destPath.isFile()) {
        promise->addResult(QIcon(destPath.toFSPathString()));
        promise->finish();
        return;
    }

    const auto setupFetch = [url](FileStreamer &task) {
        task.setSource(FilePath::fromUserInput(url));
        task.setStreamMode(StreamMode::Reader);
    };

    const auto fetchDone = [url, promise, destPath, themeColor](const FileStreamer &task,
                                                                DoneWith doneWith) {
        if (doneWith == DoneWith::Success) {
            Result<> res = createSvgFile(task.readData(), destPath, themeColor, promise);
            QTC_CHECK_RESULT(res);
        }
        promise->finish();
    };

    GlobalTaskTree::start({FileStreamerTask(setupFetch, fetchDone)});
}

class AcpRegistryBrowser : public StringSelectionAspect
{
public:
    AcpRegistryBrowser(AspectContainer *parent = nullptr)
        : StringSelectionAspect(parent)
    {
        setComboBoxEditable(false);

        auto fillCallback = [this](ResultCallback resultCb) {
            if (!s_registry) {
                // Not yet fetched — return just the custom item for now
                auto customItem = new QStandardItem(Tr::tr("<Custom>"));
                customItem->setData(QString());
                customItem->setToolTip(
                    Tr::tr("Manually specify an agent not listed in the registry."));
                resultCb({customItem});
                return;
            }
            resultCb(registryItems());
        };
        setFillCallback(fillCallback);
        setSizeAdjustPolicy(Utils::AspectControls::SizeAdjustPolicy::ToContents);
    }

    std::optional<Acp::Registry::ACPAgentRegistry> registry() const { return s_registry; }

    QList<QStandardItem *> registryItems()
    {
        QList<QStandardItem *> items;
        auto customItem = new QStandardItem(Tr::tr("<Custom>"));
        customItem->setData(QString());
        customItem->setToolTip(
            Tr::tr("Manually specify an agent not listed in the registry."));
        items.append(customItem);

        if (s_registry) {
            for (const auto &agent : s_registry->agents()) {
                auto item = new QStandardItem(agent.name() + " (" + agent.version() + ")");
                item->setToolTip(agent.description());
                item->setData(agent.id());
                Utils::onResultReady(
                    AcpSettings::iconForUrl(agent.icon().value_or(QString())),
                    this,
                    [id = agent.id(), this](const QIcon &icon) {
                        if (auto item = itemById(id))
                            item->setData(icon, Qt::DecorationRole);
                    });
                items.append(item);
            }
        }
        return items;
    }

    static void prefetch(std::function<void()> onDone = {})
    {
        if (s_registry) {
            if (onDone)
                onDone();
            return;
        }

        const auto setupFetch = [](QNetworkReplyWrapper &wrapper) {
            wrapper.setNetworkAccessManager(Utils::NetworkAccessManager::instance());
            wrapper.setOperation(QNetworkAccessManager::Operation::GetOperation);
            QNetworkRequest request(
                QUrl("https://cdn.agentclientprotocol.com/registry/v1/latest/registry.json"));
            wrapper.setRequest(request);
        };
        const auto fetchDone = [onDone](const QNetworkReplyWrapper &wrapper, DoneWith doneWith) {
            if (doneWith != DoneWith::Success)
                return;

            const QByteArray data = wrapper.reply()->readAll();
            QJsonParseError error;
            const QJsonDocument doc = QJsonDocument::fromJson(data, &error);
            if (error.error != QJsonParseError::NoError) {
                qWarning() << "Failed to parse registry JSON:" << error.errorString();
                return;
            }

            auto registry = Acp::Registry::fromJson<Acp::Registry::ACPAgentRegistry>(
                doc.object());
            if (!registry) {
                qWarning() << "Failed to parse registry:" << registry.error();
                return;
            }
            s_registry = std::move(*registry);
            if (onDone)
                onDone();
        };

        GlobalTaskTree::start({QNetworkReplyWrapperTask(setupFetch, fetchDone)});
    }

private:
    static std::optional<Acp::Registry::ACPAgentRegistry> s_registry;
};

std::optional<Acp::Registry::ACPAgentRegistry> AcpRegistryBrowser::s_registry;

class AcpServerAspect : public AspectContainer
{
public:
    AcpServerAspect()
    {
        setAutoApply(false);

        id.setSettingsKey("id");
        id.setValue(QUuid::createUuid().toString());

        iconUrl.setSettingsKey("iconUrl");
        iconUrl.setVisible(false);

        registryBrowser.setLabelText(Tr::tr("Template:"));
        registryBrowser.setSettingsKey("registryTemplate");
        registryBrowser.setToolTip(
            Tr::tr("Select a template from the registry to pre-fill the settings."));

        name.setLabelText(Tr::tr("Name:"));
        name.setSettingsKey("name");
        name.setToolTip(Tr::tr("The display name."));
        name.setDisplayStyle(StringAspect::DisplayStyle::LineEditDisplay);
        name.setDefaultValue(Tr::tr("<New Server>"));

        launchCommand.setLabelText(Tr::tr("Executable:"));
        launchCommand.setSettingsKey("launchCommand");
        launchCommand.setToolTip(Tr::tr("The executable to launch the ACP server process."));
        launchCommand.setExpectedKind(PathChooserKind::ExistingCommand);

        launchArguments.setLabelText(Tr::tr("Arguments:"));
        launchArguments.setSettingsKey("launchArguments");
        launchArguments.setToolTip(
            Tr::tr("The arguments to launch the ACP server process."));
        launchArguments.setDisplayStyle(StringAspect::DisplayStyle::LineEditDisplay);

        environment.setSettingsKey("environment");
        environment.setLabelText(Tr::tr("Environment changes:"));
        connect(&registryBrowser, &AcpRegistryBrowser::volatileValueChanged, this, [this]() {
            applyRegistryTemplate();
        });

        templateCmdInfo.setQmlName("TemplateCommand");
        templateCmdInfo.setIconType(InfoType::Information);
        templateCmdInfo.setWordWrap(true);
        templateCmdInfo.setToolTip(Tr::tr("The command that will spawn the ACP server."));

        cmdNotFound.setQmlName("CommandNotFound");
        cmdNotFound.setIconType(InfoType::Error);
        cmdNotFound.setWordWrap(true);
        cmdNotFound.setTextFormat(AspectControls::TextFormat::RichText);

        const auto updateCmdInfo = [this] {
            const bool isCustom = registryBrowser.volatileValue().isEmpty();
            const FilePath executable = launchCommand.expandedValue();
            templateCmdInfo.setText(
                QString("%1 %2").arg(executable.toUserOutput()).arg(launchArguments()));
            templateCmdInfo.setVisible(!isCustom);

            const bool executableCanBeFound
                = executable.searchInPath(FileUtils::usefulExtraSearchPaths()).exists();
            cmdNotFound.setVisible(!isCustom && !executableCanBeFound);
            QString warningText = Tr::tr("\"%1\" is needed for this Agent, but was not found.")
                                      .arg(executable.toUserOutput().toHtmlEscaped());
            if (executable == "npx") {
                const QString npmDocsUrl
                    = "https://docs.npmjs.com/downloading-and-installing-node-js-and-npm";
                warningText += " "
                    + Tr::tr("Make sure Node.js is installed and npx is available in PATH. "
                             "See <a href=\"%1\">%1</a> for installation instructions.")
                          .arg(npmDocsUrl);
            } else if (executable == "uvx") {
                const QString uvDocsUrl = "https://docs.astral.sh/uv/getting-started/installation/";
                warningText += " "
                    + Tr::tr("Make sure UV is installed and uvx is available in PATH. "
                             "See <a href=\"%1\">%1</a> for installation instructions.")
                          .arg(uvDocsUrl);
            }
            cmdNotFound.setText(warningText);
        };

        const auto updateVisible = [this] {
            const bool isCustom = registryBrowser.volatileValue().isEmpty();
            name.setVisible(isCustom);
            launchCommand.setVisible(isCustom);
            launchArguments.setVisible(isCustom);
            environment.setVisible(isCustom);
        };

        // Not before this server has a parent to be shown in.
        QMetaObject::invokeMethod(this, [updateVisible, updateCmdInfo] {
            updateVisible();
            updateCmdInfo();
        }, Qt::QueuedConnection);

        connect(&registryBrowser, &AcpRegistryBrowser::volatileValueChanged, this, updateVisible);
        connect(&registryBrowser, &AcpRegistryBrowser::volatileValueChanged, this, updateCmdInfo);
        connect(&environment, &EnvironmentChangesAspect::volatileValueChanged, this, updateCmdInfo);
    }

    void applyRegistryTemplate()
    {
        const QString selectedId = registryBrowser.volatileValue();
        if (selectedId.isEmpty())
            return;

        const auto &registry = registryBrowser.registry();
        if (!registry) {
            qWarning() << "No registry data available";
            return;
        }

        const auto agent = std::find_if(
            registry->agents().begin(),
            registry->agents().end(),
            [&selectedId](const Acp::Registry::ACPAgent &a) {
                return a.id() == selectedId;
            });
        if (agent == registry->agents().end()) {
            qWarning() << "Selected agent not found in registry:" << selectedId;
            return;
        }
        const Acp::Registry::ACPAgent &selectedAgent = *agent;
        name.setValue(selectedAgent.name());
        iconUrl.setValue(selectedAgent.icon().value_or(QString()));

        if (selectedAgent.distribution().binary()) {
            const FilePath stubPath = (appInfo().libexec / "dlwrapper").withExecutableSuffix();
            launchCommand.setValue(stubPath);

            const QMap<QPair<OsType, OsArch>, std::optional<Acp::Registry::binaryTarget>>
                platformToBinary{
                    {qMakePair(OsTypeMac, OsArchArm64),
                     selectedAgent.distribution().binary()->darwinminusaarch64()},
                    {qMakePair(OsTypeMac, OsArchAMD64),
                     selectedAgent.distribution().binary()->darwinminusx86_64()},
                    {qMakePair(OsTypeLinux, OsArchArm64),
                     selectedAgent.distribution().binary()->linuxminusaarch64()},
                    {qMakePair(OsTypeLinux, OsArchAMD64),
                     selectedAgent.distribution().binary()->linuxminusx86_64()},
                    {qMakePair(OsTypeWindows, OsArchArm64),
                     selectedAgent.distribution().binary()->windowsminusaarch64()},
                    {qMakePair(OsTypeWindows, OsArchAMD64),
                     selectedAgent.distribution().binary()->windowsminusx86_64()},
                };
            const auto it = platformToBinary.find(
                qMakePair(HostOsInfo::hostOs(), HostOsInfo::hostArchitecture()));
            if (it == platformToBinary.end() || !it.value().has_value()) {
                qWarning() << "No suitable binary found for current platform";
                return;
            }
            const auto binary = it.value().value();

            QStringList envChanges;
            const QMap<QString, QString> env = binary.env().value_or(QMap<QString, QString>{});
            for (const auto &[key, value] : env.asKeyValueRange())
                envChanges.append("--env " + key + "=" + value);

            const QString cmdLine
                = (QStringList{binary.cmd()} + binary.args().value_or(QStringList{})).join(" ");

            launchArguments.setValue(QString("--download %1 --version %2 %3 %4")
                                         .arg(binary.archive())
                                         .arg(selectedAgent.version())
                                         .arg(envChanges.join(" "))
                                         .arg(cmdLine));
        } else if (selectedAgent.distribution().npx()) {
            launchCommand.setValue(FilePath("npx"));
            launchArguments.setValue(
                selectedAgent.distribution().npx()->package() + " "
                + selectedAgent.distribution().npx()->args().value_or(QStringList{}).join(" "));
        } else if (selectedAgent.distribution().uvx()) {
            launchCommand.setValue(FilePath("uvx"));
            launchArguments.setValue(
                selectedAgent.distribution().uvx()->package() + " "
                + selectedAgent.distribution().uvx()->args().value_or(QStringList{}).join(" "));
        }
    }

    AcpSettings::ServerInfo toServerInfo() const
    {
        AcpSettings::ServerInfo info;
        info.id = id.value();
        info.name = name.value();
        info.iconUrl = iconUrl.value();
        info.launchCommand = CommandLine(
            FilePath::fromUserInput(launchCommand.value()),
            launchArguments.value(),
            CommandLine::Raw);
        info.envChanges = environment.value();
        return info;
    }

    AcpRegistryBrowser registryBrowser{this};

    StringAspect id{this};
    StringAspect name{this};
    StringAspect iconUrl{this};

    FilePathAspect launchCommand{this};
    StringAspect launchArguments{this};
    EnvironmentChangesAspect environment{this};
    // What the closure used to build as two InfoLabels. As aspects they are
    // drawn by whichever renderer has the item, and their text and visibility
    // are set where the rest of this server's behaviour is - in the
    // constructor, not in a layout.
    TextDisplay templateCmdInfo{this};
    TextDisplay cmdNotFound{this};
};

class AcpManagerSettings : public AspectContainer
{
public:
    AcpManagerSettings()
    {
        setSettingsGroup("AcpClient");

        setAutoApply(false);
        acpServers.setSettingsKey("AcpServers");
        acpServers.setDisplayStyle(AspectList::DisplayStyle::ListViewWithDetails);
        acpServers.setCreateItemFunction([] { return std::make_shared<AcpServerAspect>(); });

        acpServers.listViewDataCallback = [](AcpServerAspect *aspect, int role) -> QVariant {
            if (role == Qt::DisplayRole)
                return aspect->name.volatileValue();

            if (role == Qt::DecorationRole) {
                const QString iconUrl = aspect->iconUrl.volatileValue();
                if (iconUrl.isEmpty())
                    return QVariant();

                return QVariant::fromValue(
                    AcpSettings::iconForUrl(iconUrl).then(
                        [](const QIcon &icon) -> QVariant { return icon; }));
            }
            return {};
        };

        setQmlSource(QUrl("qrc:/qt/qml/QtCreator/AcpClient/AcpSettingsPage.qml"));

        readSettings();
    }

    static AcpManagerSettings &instance()
    {
        static AcpManagerSettings settings;
        return settings;
    }

    AspectList acpServers{this};
};

static const char ACP_SETTINGS_PAGE_ID[] = "AI.ACPSERVERS";

class AcpSettingsPage final : public Core::IOptionsPage
{
public:
    AcpSettingsPage()
    {
        setId(ACP_SETTINGS_PAGE_ID);
        setDisplayName(Tr::tr("ACP Servers"));
        setCategory(Core::Constants::SETTINGS_CATEGORY_AI);
        setSettingsProvider([] { return &AcpManagerSettings::instance(); });
    }
};

static AcpSettingsPage &settingsPage()
{
    static AcpSettingsPage page;
    return page;
}

// --- AcpSettings implementation ---

AcpSettings::AcpSettings()
{
    QObject::connect(
        &AcpManagerSettings::instance().acpServers,
        &BaseAspect::changed,
        this,
        &AcpSettings::serversChanged);
}

AcpSettings::~AcpSettings() = default;

AcpSettings &AcpSettings::instance()
{
    static AcpSettings settings;
    return settings;
}

QList<AcpSettings::ServerInfo> AcpSettings::servers()
{
    QList<ServerInfo> result;
    AcpManagerSettings::instance().acpServers.forEachItem(
        [&](const std::shared_ptr<AcpServerAspect> &server) {
            result.push_back(server->toServerInfo());
        });
    return result;
}

QFuture<QIcon> AcpSettings::iconForUrl(const QString &url)
{
    auto promise = std::make_shared<QPromise<QIcon>>();
    promise->start();
    fetchIconToPersistentCache(url, promise);
    return promise->future();
}

void setupAcpSettings()
{
    (void) settingsPage();
    (void) AcpSettings::instance();
}

void prefetchAcpRegistry()
{
    AcpRegistryBrowser::prefetch(
        [] {
            AcpManagerSettings::instance().acpServers.forEachItem(
                [](const std::shared_ptr<AcpServerAspect> &server) {
                    server->applyRegistryTemplate();
                });
            AcpManagerSettings::instance().acpServers.writeSettings();
            emit AcpSettings::instance().serversChanged();
        });
}

#ifdef WITH_TESTS


// The visibility rules used to live in the layout closure, so only a widget
// being built ever ran them. They are the server's own behaviour: a server
// taken from the registry is described by the template, and the fields that
// spell one out by hand have nothing to say.
class AcpServerAspectTest final : public QObject
{
    Q_OBJECT

private slots:
    void testATemplateHidesTheFieldsThatSpellOneOutByHand()
    {
        AcpServerAspect server;

        // Custom to begin with: no template chosen, so every field is the
        // user's to fill in.
        QTRY_VERIFY(server.name.isVisible());
        QVERIFY(server.launchCommand.isVisible());
        QVERIFY(server.launchArguments.isVisible());
        QVERIFY(server.environment.isVisible());

        // Choosing a template describes the command instead, so the fields go.
        server.registryBrowser.setVolatileValue(QString("some-registry-id"));
        QTRY_VERIFY(!server.name.isVisible());
        QVERIFY(!server.launchCommand.isVisible());
        QVERIFY(!server.launchArguments.isVisible());
        QVERIFY(!server.environment.isVisible());

        // And clearing it brings them back.
        server.registryBrowser.setVolatileValue(QString());
        QTRY_VERIFY(server.name.isVisible());
        QVERIFY(server.launchCommand.isVisible());
    }
};

QObject *createAcpServerAspectTest()
{
    return new AcpServerAspectTest;
}

#endif // WITH_TESTS

} // namespace AcpClient::Internal

#include "acpsettings.moc"
