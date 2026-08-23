// Copyright (C) 2020 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "webassemblysettings.h"

#include "webassemblyconstants.h"
#include "webassemblyemsdk.h"
#include "webassemblyqtversion.h"
#include "webassemblytoolchain.h"
#include "webassemblytr.h"

#include <coreplugin/icore.h>
#include <coreplugin/dialogs/ioptionspage.h>

#include <projectexplorer/projectexplorerconstants.h>

#include <utils/aspects.h>
#include <utils/environment.h>
#include <utils/pathvalidation.h>

#include <QDesktopServices>
#include <QDir>
#include <QGroupBox>
#include <QGuiApplication>
#include <QTimer>

using namespace Utils;

namespace WebAssembly::Internal {

WebAssemblySettings &settings()
{
    static WebAssemblySettings theSettings;
    return theSettings;
}

static QString environmentDisplay(const FilePath &sdkRoot)
{
    Environment env;
    WebAssemblyEmSdk::addToEnvironment(sdkRoot, env);
    QString result;
    auto h4 = [](const QString &text) { return QString("<h4>" + text + "</h4>"); };
    result.append(h4(Tr::tr("Adding directories to PATH:")));
    result.append(env.value("PATH").replace(OsSpecificAspects::pathListSeparator(sdkRoot.osType()), "<br/>"));
    result.append(h4(Tr::tr("Setting environment variables:")));
    for (const QString &envVar : env.toStringList()) {
        if (!envVar.startsWith("PATH")) // Path was already printed out above
            result.append(envVar + "<br/>");
    }
    return result;
}

WebAssemblySettings::WebAssemblySettings()
{
    setSettingsGroup("WebAssembly");
    setAutoApply(false);

    emSdk.setSettingsKey("EmSdk");
    emSdk.setExpectedKind(Utils::PathChooserKind::ExistingDirectory);
    emSdk.setDefaultValue(QDir::homePath());

    connect(this, &Utils::AspectContainer::applied, &registerToolChains);

    instruction.setText(
        Tr::tr("Select the root directory of an installed %1. "
               "Ensure that the activated SDK version is compatible with the %2 "
               "or %3 version that you plan to develop against.")
            .arg(R"(<a href="https://emscripten.org/docs/getting_started/downloads.html">Emscripten SDK</a>)")
            .arg(R"(<a href="https://doc.qt.io/qt-5/wasm.html#install-emscripten">Qt 5</a>)")
            .arg(R"(<a href="https://doc.qt.io/qt-6/wasm.html#installing-emscripten">Qt 6</a>)"));
    instruction.setWordWrap(true);
    instruction.setQmlName("Instruction");
    connect(&instruction, &Utils::TextDisplay::linkActivated, this, [](const QString &link) {
        QDesktopServices::openUrl(QUrl(link));
    });

    statusIsEmsdkDir.setText(Tr::tr("The chosen directory is an emsdk location."));
    statusIsEmsdkDir.setQmlName("StatusIsEmsdkDir");
    statusSdkInstalled.setText(Tr::tr("An SDK is installed."));
    statusSdkInstalled.setQmlName("StatusSdkInstalled");
    statusSdkActivated.setText(Tr::tr("An SDK is activated."));
    statusSdkActivated.setQmlName("StatusSdkActivated");
    statusSdkInvalid.setIconType(Utils::InfoType::Error);
    statusSdkInvalid.setWordWrap(true);
    statusSdkInvalid.setQmlName("StatusSdkInvalid");

    emSdkVersionDisplay.setWordWrap(true);
    emSdkVersionDisplay.setQmlName("EmSdkVersionDisplay");

    emSdkEnvDisplay.setDisplayStyle(Utils::StringAspect::TextEditDisplay);
    emSdkEnvDisplay.setReadOnly(true);
    emSdkEnvDisplay.setQmlName("EmSdkEnvDisplay");

    qtVersionDisplay.setText(
        Tr::tr("Note: %1 supports Qt %2 for WebAssembly and higher. "
               "Your installed lower Qt version(s) are not supported.")
            .arg(Core::ICore::versionString(),
                 WebAssemblyQtVersion::minimumSupportedQtVersion().toString()));
    qtVersionDisplay.setIconType(Utils::InfoType::Warning);
    qtVersionDisplay.setWordWrap(true);
    qtVersionDisplay.setQmlName("QtVersionDisplay");

    // The status of the chosen directory follows the directory, and is not
    // something a layout should be computing on its way past.
    connect(&emSdk, &Utils::BaseAspect::volatileValueChanged,
            this, &WebAssemblySettings::updateStatus);
    updateStatus();

    setQmlSource(QUrl("qrc:/qt/qml/QtCreator/WebAssembly/WebAssemblySettingsPage.qml"));

    readSettings();
}

enum EmsdkError {
    EmsdkErrorUnknown,
    EmsdkErrorNoDir,
    EmsdkErrorNoEmsdkDir,
    EmsdkErrorNoSdkInstalled,
    EmsdkErrorNoSdkActivated,
};

static EmsdkError emsdkError(const Utils::FilePath &sdkRoot)
{
    if (!sdkRoot.exists())
        return EmsdkErrorNoDir;
    if (!(sdkRoot / "emsdk").refersToExecutableFile(FilePath::WithBatSuffix))
        return EmsdkErrorNoEmsdkDir;
    if (!(sdkRoot / "upstream/.emsdk_version").isReadableFile())
        return EmsdkErrorNoSdkInstalled;
    if (!(sdkRoot / Constants::WEBASSEMBLY_EMSDK_CONFIG_FILE).isReadableFile())
        return EmsdkErrorNoSdkActivated;
    return EmsdkErrorUnknown;
}

void WebAssemblySettings::updateStatus()
{
    WebAssemblyEmSdk::clearCaches();

    const Utils::FilePath newEmSdk = emSdk.resolvedVolatileValue();
    const auto version = WebAssemblyEmSdk::version(newEmSdk);
    const bool sdkValid = newEmSdk.exists() && version;

    statusIsEmsdkDir.setVisible(!sdkValid);
    statusSdkInstalled.setVisible(!sdkValid);
    statusSdkActivated.setVisible(!sdkValid);
    statusSdkInvalid.setVisible(!sdkValid);
    statusSdkInvalid.setText(version.has_value() ? QString() : version.error());
    emSdkVersionDisplay.setVisible(sdkValid);
    emSdkEnvDisplay.setEnabled(sdkValid);

    if (sdkValid && version) {
        const QVersionNumber sdkVersion = *version;
        const QVersionNumber minVersion = minimumSupportedEmSdkVersion();
        const bool versionTooLow = sdkVersion < minVersion;
        emSdkVersionDisplay.setIconType(versionTooLow ? Utils::InfoType::NotOk
                                                      : Utils::InfoType::Ok);
        auto bold = [](const QString &text) { return QString("<b>" + text + "</b>"); };
        emSdkVersionDisplay.setText(
            versionTooLow ? Tr::tr("The activated version %1 is not supported by %2. "
                                   "Activate version %3 or higher.")
                                .arg(bold(sdkVersion.toString()))
                                .arg(bold(Core::ICore::versionString()))
                                .arg(bold(minVersion.toString()))
                          : Tr::tr("Activated version: %1")
                                .arg(bold(sdkVersion.toString())));
        emSdkEnvDisplay.setValue(environmentDisplay(newEmSdk));
    } else {
        const EmsdkError error = emsdkError(newEmSdk);
        const bool isEmsdkDir = error != EmsdkErrorNoDir && error != EmsdkErrorNoEmsdkDir;
        statusIsEmsdkDir.setIconType(isEmsdkDir ? Utils::InfoType::Ok : Utils::InfoType::NotOk);
        const bool sdkInstalled = isEmsdkDir && error != EmsdkErrorNoSdkInstalled;
        statusSdkInstalled.setIconType(sdkInstalled ? Utils::InfoType::Ok
                                                    : Utils::InfoType::NotOk);
        const bool sdkActivated = sdkInstalled && error != EmsdkErrorNoSdkActivated;
        statusSdkActivated.setIconType(sdkActivated ? Utils::InfoType::Ok
                                                    : Utils::InfoType::NotOk);
        emSdkEnvDisplay.setValue({});
    }

    qtVersionDisplay.setVisible(WebAssemblyQtVersion::isUnsupportedQtVersionInstalled());
}

// WebAssemblySettingsPage

class WebAssemblySettingsPage final : public Core::IOptionsPage
{
public:
    WebAssemblySettingsPage()
    {
        setId(Id(Constants::SETTINGS_ID));
        setDisplayName(Tr::tr("WebAssembly"));
        setCategory(ProjectExplorer::Constants::SDK_SETTINGS_CATEGORY);
        setSettingsProvider([] { return &settings(); });
    }
};

const WebAssemblySettingsPage settingsPage;

} // WebAssembly::Internal
