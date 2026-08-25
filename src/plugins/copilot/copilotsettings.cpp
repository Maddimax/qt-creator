// Copyright (C) 2023 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0+ OR GPL-3.0 WITH Qt-GPL-exception-1.0

#include "copilotsettings.h"

#include "copilotclient.h"
#include "copilotconstants.h"
#include "copilottr.h"

#include <coreplugin/coreconstants.h>
#include <coreplugin/dialogs/ioptionspage.h>

#include <languageclient/languageclientmanager.h>

#include <projectexplorer/project.h>
#include <projectexplorer/projectmanager.h>
#include <projectexplorer/projectpanelfactory.h>
#include <projectexplorer/projectsettings.h>
#include <projectexplorer/useglobalaspect.h>

#include <utils/algorithm.h>
#include <utils/guardedcallback.h>
#include <utils/pathvalidation.h>
#include <utils/qtcassert.h>
#include <utils/stringutils.h>
#include <utils/layoutbuilder.h>

#include <QDesktopServices>

using namespace ProjectExplorer;
using namespace Utils;

namespace Copilot::Internal {

static void initEnableAspect(BoolAspect &enableCopilot)
{
    enableCopilot.setSettingsKey(Constants::ENABLE_COPILOT);
    enableCopilot.setDisplayName(Tr::tr("Enable Copilot"));
    enableCopilot.setLabelText(Tr::tr("Enable Copilot"));
    enableCopilot.setToolTip(Tr::tr("Enables the Copilot integration."));
    enableCopilot.setDefaultValue(false);
}

void CopilotSettings::setAuthState(const QString &buttonText, const QString &message, bool working)
{
    signIn.setActionText(buttonText);
    signIn.setEnabled(!working);
    authStatus.setText(message);
    authStatus.setVisible(!message.isEmpty());
}

void CopilotSettings::restartClient()
{
    LanguageClient::LanguageClientManager::shutdownClient(m_client);
    m_client = nullptr;

    const FilePath nodeJs = nodeJsPath.expandedVolatileValue();
    const FilePath agent = distPath.expandedVolatileValue();
    const bool enabled = enableCopilot.volatileValue() || isCopilotEnabledByProject();
    if (!enabled || !nodeJs.isExecutableFile() || !agent.exists()) {
        setAuthState(Tr::tr("Sign In"), {}, false);
        signIn.setEnabled(false);
        return;
    }

    setAuthState(Tr::tr("Sign In"), {}, true);

    m_client = new CopilotClient(nodeJs, agent);
    connect(m_client, &LanguageClient::Client::initialized,
            this, &CopilotSettings::checkAuthStatus);
    connect(m_client, &QObject::destroyed, this, [destroyed = m_client, this] {
        if (destroyed == m_client)
            m_client = nullptr;
    });
}

void CopilotSettings::checkAuthStatus()
{
    QTC_ASSERT(m_client && m_client->reachable(), return);

    setAuthState(Tr::tr("Checking status..."), {}, true);

    m_client->requestCheckStatus(
        false, guardedCallback(this, [this](const CheckStatusRequest::Response &response) {
            if (response.error()) {
                setAuthState(Tr::tr("Failed to authenticate"),
                             response.error()->message(),
                             false);
                return;
            }
            const CheckStatusResponse result = *response.result();
            if (result.user().isEmpty()) {
                setAuthState(Tr::tr("Sign In"), {}, false);
                m_authState = AuthState::SignedOut;
                return;
            }
            setAuthState(Tr::tr("Sign Out %1").arg(result.user()), {}, false);
            m_authState = AuthState::SignedIn;
        }));
}

void CopilotSettings::requestSignIn()
{
    QTC_ASSERT(m_client && m_client->reachable(), return);

    setAuthState(Tr::tr("Signing in..."), {}, true);

    m_client->requestSignInInitiate(
        guardedCallback(this, [this](const SignInInitiateRequest::Response &response) {
            if (response.error()) {
                setAuthState(Tr::tr("Sign In"),
                             Tr::tr("The sign-in request failed: %1")
                                 .arg(response.error()->message()),
                             false);
                return;
            }

            Utils::setClipboardAndSelection(response.result()->userCode());
            QDesktopServices::openUrl(QUrl(response.result()->verificationUri()));

            authStatus.setText(
                Tr::tr("A browser window will open. Enter the code %1 when asked.\n"
                       "The code has been copied to your clipboard.")
                    .arg(response.result()->userCode()));
            authStatus.setVisible(true);

            m_client->requestSignInConfirm(
                response.result()->userCode(),
                guardedCallback(this, [this](const SignInConfirmRequest::Response &response) {
                    if (response.error()) {
                        setAuthState(Tr::tr("Sign In"), response.error()->message(), false);
                        return;
                    }
                    setAuthState(Tr::tr("Sign Out %1").arg(response.result()->user()), {}, false);
                    m_authState = AuthState::SignedIn;
                }));
        }));
}

void CopilotSettings::requestSignOut()
{
    QTC_ASSERT(m_client && m_client->reachable(), return);

    setAuthState(Tr::tr("Signing out..."), {}, true);

    m_client->requestSignOut(guardedCallback(this, [this](const SignOutRequest::Response &response) {
        QTC_ASSERT(!response.error(), return);
        QTC_ASSERT(response.result()->status() == "NotSignedIn", return);
        checkAuthStatus();
    }));
}

CopilotSettings &settings()
{
    static CopilotSettings settings;
    return settings;
}

static const QString entryPointFileName = QStringLiteral("language-server.js");

CopilotSettings::CopilotSettings()
{
    setAutoApply(false);

    const FilePath nodeFromPath = FilePath("node").searchInPath();

    // From: https://github.com/github/copilot.vim/blob/release/README.md#getting-started
    const QStringList subLocations
        = {"dist/agent.js",
           "copilot/dist/agent.js",
           "dist/language-server.js",
           "copilot-language-server/dist/language-server.js"};

    const QString baseDir = "pack/github/start/copilot.vim";

    const FilePaths locations = {
        // Vim, Linux/macOS:
        FilePath::fromUserInput("~/.vim") / baseDir,

        // Neovim, Linux/macOS:
        FilePath::fromUserInput("~/.config/nvim") / baseDir,

        // Vim, Windows (PowerShell command):
        FilePath::fromUserInput("~/vimfiles") / baseDir,

        // Neovim, Windows (PowerShell command):
        FilePath::fromUserInput("~/AppData/Local/nvim") / baseDir,
    };

    FilePaths searchDirs;
    for (const auto &loc : locations) {
        for (const auto &subLocation : subLocations)
            searchDirs.append(loc / subLocation);
    }

    const FilePath distFromVim = findOrDefault(searchDirs, &FilePath::exists);

    nodeJsPath.setExpectedKind(PathChooserKind::ExistingCommand);
    nodeJsPath.setDefaultPathValue(nodeFromPath);
    nodeJsPath.setSettingsKey("Copilot.NodeJsPath");
    nodeJsPath.setLabelText(Tr::tr("Node.js path:"));
    nodeJsPath.setHistoryCompleter("Copilot.NodePath.History");
    nodeJsPath.setDisplayName(Tr::tr("Node.js Path"));
    //: %1 is the URL to nodejs
    nodeJsPath.setToolTip(Tr::tr("Select path to node.js executable. See %1 "
                                 "for installation instructions.")
                              .arg("https://nodejs.org/en/download/"));

    distPath.setExpectedKind(PathChooserKind::File);
    distPath.setDefaultPathValue(distFromVim);
    distPath.setSettingsKey("Copilot.DistPath");
    //: %1 is the filename of the copilot language server
    distPath.setLabelText(Tr::tr("Path to %1:").arg(entryPointFileName));
    distPath.setHistoryCompleter("Copilot.DistPath.History");
    //: %1 is the filename of the copilot language server
    distPath.setDisplayName(Tr::tr("Path to %1").arg(entryPointFileName));
    //: %1 is the URL to copilot.vim getting started, %2 is the filename of the copilot language server
    distPath.setToolTip(Tr::tr("Select path to %2 in Copilot Neovim plugin. See "
                               "%1 for installation instructions.")
                            .arg("https://github.com/github/copilot.vim#getting-started")
                            .arg(entryPointFileName));

    autoComplete.setDisplayName(Tr::tr("Auto Request"));
    autoComplete.setSettingsKey("Copilot.Autocomplete");
    autoComplete.setLabelText(Tr::tr("Auto request"));
    autoComplete.setDefaultValue(true);
    autoComplete.setToolTip(Tr::tr("Automatically request suggestions for the current text cursor "
                                   "position after changes to the document."));

    proxy.setDisplayName(Tr::tr("Proxy"));
    proxy.setDisplayStyle(StringAspect::DisplayStyle::LineEditDisplay);
    proxy.setSettingsKey("Copilot.Proxy");
    proxy.setLabelText(Tr::tr("Proxy:"));
    proxy.setDefaultValue("");
    proxy.setPlaceHolderText("http://localhost:3128");
    proxy.setToolTip(Tr::tr("The proxy server to use for connections."));

    proxyRejectUnauthorized.setDisplayName(Tr::tr("Reject Unauthorized"));
    proxyRejectUnauthorized.setSettingsKey("Copilot.ProxyRejectUnauthorized");
    proxyRejectUnauthorized.setLabelText(Tr::tr("Reject unauthorized"));
    proxyRejectUnauthorized.setDefaultValue(true);
    proxyRejectUnauthorized.setToolTip(Tr::tr("Reject unauthorized certificates from the proxy "
                                              "server. Turning this off is a security risk."));

    githubEnterpriseUrl.setDisplayName(Tr::tr("GitHub Enterprise URL"));
    githubEnterpriseUrl.setDisplayStyle(StringAspect::DisplayStyle::LineEditDisplay);
    githubEnterpriseUrl.setSettingsKey("Copilot.GithubEnterpriseUrl");
    githubEnterpriseUrl.setLabelText(Tr::tr("GitHub Enterprise URL:"));
    githubEnterpriseUrl.setDefaultValue("");
    githubEnterpriseUrl.setToolTip(Tr::tr("The URL of your GitHub Enterprise server."));

    initEnableAspect(enableCopilot);

    readSettings();

    // TODO: As a workaround we set the enabler after reading the settings, as that does not signal
    // a change.
    nodeJsPath.setEnabler(&enableCopilot);
    distPath.setEnabler(&enableCopilot);
    autoComplete.setEnabler(&enableCopilot);
    githubEnterpriseUrl.setEnabler(&enableCopilot);

    proxy.setEnabler(&enableCopilot);
    proxyRejectUnauthorized.setEnabler(&enableCopilot);

    warning.setText(
        Tr::tr("Enabling %1 is subject to your agreement and abidance with your applicable "
               "%1 terms. It is your responsibility to know and accept the requirements and "
               "parameters of using tools like %1. This may include, but is not limited to, "
               "ensuring you have the rights to allow %1 access to your code, as well as "
               "understanding any implications of your use of %1 and suggestions produced "
               "(like copyright, accuracy, etc.).")
            .arg("Copilot"));
    warning.setWordWrap(true);
    warning.setQmlName("Warning");

    help.setText(
        Tr::tr("The Copilot plugin requires node.js and the Copilot neovim plugin. "
               "If you install the neovim plugin as described in %1, "
               "the plugin will find the %3 file automatically.\n\n"
               "Otherwise you need to specify the path to the %2 "
               "file from the Copilot neovim plugin.",
               "Markdown text for the copilot instruction label")
            .arg("[README.md](https://github.com/github/copilot.vim)")
            .arg("[language-server.js](https://github.com/github/copilot.vim/tree/release/"
                 "copilot-language-server/dist)")
            .arg(entryPointFileName));
    help.setWordWrap(true);
    help.setQmlName("Help");
    connect(&help, &Utils::TextDisplay::linkActivated, this, [](const QString &link) {
        QDesktopServices::openUrl(QUrl(link));
    });

    authStatus.setQmlName("AuthStatus");
    authStatus.setVisible(false);

    signIn.setQmlName("SignIn");
    signIn.setAction([this] {
        if (m_authState == AuthState::SignedIn)
            requestSignOut();
        else if (m_authState == AuthState::SignedOut)
            requestSignIn();
        else
            restartClient();
    });

    // The client belongs to the settings, not to whatever is drawing them: it
    // used to be created and shut down with the widget, so it churned every
    // time the page was opened.
    connect(this, &Utils::AspectContainer::applied, this, &CopilotSettings::restartClient);
    connect(&nodeJsPath, &Utils::FilePathAspect::volatileValueChanged,
            this, &CopilotSettings::restartClient);
    connect(&distPath, &Utils::FilePathAspect::volatileValueChanged,
            this, &CopilotSettings::restartClient);
    connect(&enableCopilot, &Utils::BoolAspect::volatileValueChanged,
            this, &CopilotSettings::restartClient);
    // Not here: starting the client reads these settings back, and they are a
    // function-local static still under construction. It starts when the page
    // is first drawn, which is when the widget editor used to start it.
    signIn.setOnShown([this] { restartClient(); });
    setAuthState(Tr::tr("Sign In"), {}, false);
    signIn.setEnabled(false);

    setQmlSource(QUrl("qrc:/qt/qml/QtCreator/Copilot/CopilotSettingsPage.qml"));

}

// Global settings page

class CopilotSettingsPage final : public Core::IOptionsPage
{
public:
    CopilotSettingsPage()
    {
        setId(Constants::COPILOT_GENERAL_OPTIONS_ID);
        setDisplayName("Copilot");
        setCategory(Core::Constants::SETTINGS_CATEGORY_AI);
        setSettingsProvider([] { return &settings(); });
    }
};

// Project settings

class CopilotProjectSettings final : public AspectContainer
{
public:
    explicit CopilotProjectSettings(Project *project)
    {
        setAutoApply(true);

        useGlobalSettings.setSettingsKey(Constants::COPILOT_USE_GLOBAL_SETTINGS);

        initEnableAspect(enableCopilot);

        Store map = storeFromVariant(project->namedSettings(Constants::COPILOT_PROJECT_SETTINGS_ID));
        fromMap(map);

        enableCopilot.setEnabled(!useGlobalSettings());
        enableCopilot.addOnChanged(this, [this, project] { save(project); });

        useGlobalSettings.addOnChanged(this, [this, project] {
            enableCopilot.setEnabled(!useGlobalSettings());
            save(project);
        });
    }

    void save(Project *project)
    {
        Store map;
        toMap(map);
        project->setNamedSettings(Constants::COPILOT_PROJECT_SETTINGS_ID, variantFromStore(map));

        // This triggers a restart of the Copilot language server.
        settings().apply();
    }

    static Utils::Key extraDataKey() { return "CopilotProjectSettings"; }

    BoolAspect enableCopilot{this};
    UseGlobalAspect useGlobalSettings{Constants::COPILOT_GENERAL_OPTIONS_ID, this};
};

static CopilotProjectSettings *copilotProjectSettings(Project *project)
{
    return projectSettings<CopilotProjectSettings>(project);
}

class CopilotProjectWidget final : public QWidget
{
public:
    CopilotProjectWidget(Project *project)
    {
        CopilotProjectSettings * const ps = copilotProjectSettings(project);
        // clang-format off
        using namespace Layouting;
        Column {
            ps->useGlobalSettings,
            hr,
            ps->enableCopilot,
            st,
        }.attachTo(this);
        // clang-format on
    }
};

class CopilotProjectPanelFactory final : public ProjectPanelFactory
{
public:
    CopilotProjectPanelFactory()
    {
        setPriority(1000);
        setDisplayName(Tr::tr("Copilot"));
        setCreateWidgetFunction([](Project *project) {
            return new CopilotProjectWidget(project);
        });
    }
};

bool isCopilotEnabled(Project *project)
{
    if (!project)
        return settings().enableCopilot();

    CopilotProjectSettings * const ps = copilotProjectSettings(project);
    if (ps->useGlobalSettings())
        return settings().enableCopilot();

    return ps->enableCopilot();
}

bool isCopilotEnabledByProject()
{
    return Utils::anyOf(ProjectManager::projects(), [](Project *project) {
        CopilotProjectSettings * const ps = copilotProjectSettings(project);
        return !ps->useGlobalSettings() && ps->enableCopilot();
    });
}

void setupCopilotSettings()
{
    static CopilotSettingsPage theCopilotSettingsPage;
    static CopilotProjectPanelFactory theCopilotProjectPanelFactory;
}

} // namespace Copilot::Internal
