// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "systemsettings.h"

#include "coreconstants.h"
#include "coreplugintr.h"
#include "editormanager/editormanager_p.h"
#include "dialogs/ioptionspage.h"
#include "fileutils.h"
#include "icore.h"
#include "vcsmanager.h"

#include <utils/appinfo.h>
#include <utils/checkablemessagebox.h>
#include <utils/crashreporting.h>
#include <utils/environment.h>
#include <utils/environmentdialog.h>
#include <utils/fileutils.h>
#include <utils/globalfilechangeblocker.h>
#include <utils/guiutils.h>
#include <utils/hostosinfo.h>
#include <utils/layoutbuilder.h>
#include <utils/macroexpander.h>
#include <utils/pathvalidation.h>
#include <utils/terminalcommand.h>

#include <QApplication>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QGuiApplication>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QToolButton>

using namespace Utils;
using namespace Layouting;

namespace Core::Internal {

static constexpr bool hasDBusFileManager =
#ifdef QTC_SUPPORT_DBUSFILEMANAGER
    true;
#else
    false;
#endif

SystemSettings &systemSettings()
{
    static SystemSettings theSettings;
    return theSettings;
}

static NameValueDictionary defaultEnvVarSeparators()
{
    return NameValueDictionary(
        NameValuePairs{
            std::make_pair<QString, QString>("CFLAGS", " "),
            std::make_pair<QString, QString>("CXXFLAGS", " ")});
}

static QString fileBrowserHelpText()
{
    return Tr::tr(
        "<table border=1 cellspacing=0 cellpadding=3>"
        "<tr><th>Variable</th><th>Expands to</th></tr>"
        "<tr><td>%d</td><td>directory of current file</td></tr>"
        "<tr><td>%f</td><td>file name (with full path)</td></tr>"
        "<tr><td>%n</td><td>file name (without path)</td></tr>"
        "<tr><td>%%</td><td>%</td></tr>"
        "</table>");
}

SystemSettings::SystemSettings()
{
    setAutoApply(false);

    environmentChangesAspect.setLabelText("Environment:");
    environmentChangesAspect.setSettingsKey(kEnvironmentChanges);

    terminalCommand.setLabelText(Tr::tr("Terminal:"));

    const auto updateSystemEnv = [this, startupEnv = Environment::systemEnvironment()] {
        Environment systemEnv = startupEnv;
        environmentChangesAspect.value().modifyEnvironment(systemEnv, nullptr);

        Environment::setSystemEnvironment(systemEnv);

        if (ICore::instance())
            ICore::instance()->systemEnvironmentChanged();
    };

    connect(&environmentChangesAspect, &Utils::BaseAspect::changed, this, updateSystemEnv);

    Qt::Alignment lfa = Qt::Alignment(qApp->style()->styleHint(QStyle::SH_FormLayoutFormAlignment));

    BoolAspect::LabelPlacement boolAspectLabelPlacement
        = lfa & Qt::AlignHCenter ? BoolAspect::LabelPlacement::AtCheckBox
                                 : BoolAspect::LabelPlacement::Compact;

    envVarSeparatorAspect.setLabelText(Tr::tr("Variable separators:"));
    envVarSeparatorAspect.setSettingsKey(kEnvVarSeparators);
    envVarSeparatorAspect.setDefaultValue(defaultEnvVarSeparators().toStringList());

    useDbusFileManagers.setSettingsKey("General/SupportDbusFileManagers");
    useDbusFileManagers.setDefaultValue(true);
    useDbusFileManagers.setLabelPlacement(boolAspectLabelPlacement);
    useDbusFileManagers.setLabelText(Tr::tr("Use freedesktop.org file manager D-Bus interface"));
    useDbusFileManagers.setToolTip(
        Tr::tr(
            "Uses the <a href=\"%1\">freedesktop.org D-Bus interface</a> for <i>Open in File "
            "Manager</i>, if available. Otherwise falls back to the \"External file browser\" "
            "above.")
            .arg("https://freedesktop.org/wiki/Specifications/file-manager-interface"));

    externalFileBrowser.setSettingsKey("General/FileBrowser");
    externalFileBrowser.setDisplayStyle(StringAspect::DisplayStyle::LineEditDisplay);
    externalFileBrowser.setDefaultValue("xdg-open %d");
    externalFileBrowser.setLabelText(Tr::tr("External file browser:"));
    externalFileBrowser.setToolTip(
        Tr::tr(
            "Command used for <i>Open in File Manager</i> if the freedesktop.org D-Bus interface "
            "is not available. The command can contain the following variables:\n")
        + fileBrowserHelpText());

    patchCommand.setSettingsKey("General/PatchCommand");
    patchCommand.setDefaultValue("patch");
    patchCommand.setExpectedKind(PathChooserKind::ExistingCommand);
    patchCommand.setHistoryCompleter("General.PatchCommand.History");
    patchCommand.setLabelText(Tr::tr("Patch command:"));
    patchCommand.setToolTip(Tr::tr("Command used for reverting diff chunks."));

    autoSaveModifiedFiles.setSettingsKey("EditorManager/AutoSaveEnabled");
    autoSaveModifiedFiles.setDefaultValue(true);
    autoSaveModifiedFiles.setLabelText(Tr::tr("Auto-save modified files"));
    autoSaveModifiedFiles.setLabelPlacement(boolAspectLabelPlacement);
    autoSaveModifiedFiles.setToolTip(
        Tr::tr("Automatically creates temporary copies of modified files. "
               "If %1 is restarted after a crash or power failure, it asks whether to "
               "recover the auto-saved content.")
            .arg(QGuiApplication::applicationDisplayName()));

    autoSaveInterval.setSettingsKey("EditorManager/AutoSaveInterval");
    autoSaveInterval.setSuffix(Tr::tr("min"));
    autoSaveInterval.setRange(1, 1000000);
    autoSaveInterval.setDefaultValue(5);
    autoSaveInterval.setLabelText(Tr::tr("Interval:"));

    autoSaveAfterRefactoring.setSettingsKey("EditorManager/AutoSaveAfterRefactoring");
    autoSaveAfterRefactoring.setDefaultValue(true);
    autoSaveAfterRefactoring.setLabelPlacement(boolAspectLabelPlacement);
    autoSaveAfterRefactoring.setLabelText(Tr::tr("Auto-save files after refactoring"));
    autoSaveAfterRefactoring.setToolTip(
        Tr::tr("Automatically saves all open files affected by a refactoring operation,\n"
               "provided they were unmodified before the refactoring."));

    disableAtomicSave.setSettingsKey("EditorManager/DisableAtomicSave");
    disableAtomicSave.setDefaultValue(false);
    disableAtomicSave.setLabelPlacement(boolAspectLabelPlacement);
    disableAtomicSave.setLabelText(Tr::tr("Disable atomic saving of files"));
    disableAtomicSave.setToolTip(
        Tr::tr("Writes files directly instead of saving to a temporary file and renaming it "
               "over the original. The file keeps its inode, and with it any file-system "
               "metadata that a freshly created file would not inherit, but changes "
               "might be lost if the system crashes or power fails."));

    autoSuspendEnabled.setSettingsKey("EditorManager/AutoSuspendEnabled");
    autoSuspendEnabled.setDefaultValue(true);
    autoSuspendEnabled.setLabelText(Tr::tr("Auto-suspend unmodified files"));
    autoSuspendEnabled.setLabelPlacement(boolAspectLabelPlacement);
    autoSuspendEnabled.setToolTip(
        Tr::tr("Automatically free resources of old documents that are not visible and not "
               "modified. They stay visible in the list of open documents."));

    enableCrashReports.setSettingsKey(Utils::crashReportSettingsKey());
    enableCrashReports.setLabelPlacement(boolAspectLabelPlacement);
    enableCrashReports.setLabelText(Tr::tr("Enable crash reporting"));
    enableCrashReports.setToolTip(
        "<p>"
        + Tr::tr(
            "Allow crashes to be automatically reported. Collected reports are "
            "used for the sole purpose of fixing bugs.")
        + "</p><p>"
        + Tr::tr("Crash reports are saved in \"%1\".").arg(appInfo().crashReports.toUserOutput()));
    enableCrashReports.setDefaultValue(false);
    enableCrashReports.setLabelPlacement(boolAspectLabelPlacement);

    autoSuspendMinDocumentCount.setSettingsKey("EditorManager/AutoSuspendMinDocuments");
    autoSuspendMinDocumentCount.setRange(1, 500);
    autoSuspendMinDocumentCount.setDefaultValue(10);
    autoSuspendMinDocumentCount.setLabelText(Tr::tr("Files to keep open:"));
    autoSuspendMinDocumentCount.setToolTip(
        Tr::tr("Minimum number of open documents that should be kept in memory. Increasing this "
           "number will lead to greater resource usage when not manually closing documents."));

    warnBeforeOpeningBigFiles.setSettingsKey("EditorManager/WarnBeforeOpeningBigTextFiles");
    warnBeforeOpeningBigFiles.setDefaultValue(true);
    warnBeforeOpeningBigFiles.setLabelPlacement(boolAspectLabelPlacement);
    warnBeforeOpeningBigFiles.setLabelText(Tr::tr("Warn before opening text files greater than"));

    bigFileSizeLimitInMB.setSettingsKey("EditorManager/BigTextFileSizeLimitInMB");
    bigFileSizeLimitInMB.setSuffix(Tr::tr("MB"));
    bigFileSizeLimitInMB.setRange(1, 500);
    bigFileSizeLimitInMB.setDefaultValue(5);

    maxRecentFiles.setSettingsKey("EditorManager/MaxRecentFiles");
    maxRecentFiles.setRange(1, 99);
    maxRecentFiles.setDefaultValue(8);
    maxRecentFiles.setLabelText(Tr::tr("Number of \"Recent Files\":"));

    reloadSetting.setSettingsKey("EditorManager/ReloadBehavior");
    reloadSetting.setDisplayStyle(SelectionAspect::DisplayStyle::ComboBox);
    reloadSetting.addOption(
        {Tr::tr("Always Ask"),
         Tr::tr(
             "When %1 is or becomes active, ask what to do when open files are changed externally "
             "regardless of whether they have unsaved changes or not.")
             .arg(QGuiApplication::applicationDisplayName()),
         IDocument::AlwaysAsk});
    reloadSetting.addOption(
        {Tr::tr("Reload All Unchanged Editors when Active"),
         Tr::tr(
             "When %1 is or becomes active, ask what to do with editors with unsaved changes when "
             "their files are changed externally. Automatically reload all other files.")
             .arg(QGuiApplication::applicationDisplayName()),
         IDocument::ReloadUnmodified});
    reloadSetting.addOption(
        {Tr::tr("Reload All Unchanged Editors Immediately"),
         Tr::tr(
             "Ask what to do with editors with unsaved changes when their files are changed "
             "externally. Automatically reload all other files."),
         IDocument::ReloadUnmodifiedImmediately});
    reloadSetting.addOption(
        {Tr::tr("Ignore Modifications"),
         Tr::tr("Ignore external changes of files that are open in editors."),
         IDocument::IgnoreAll});
    reloadSetting.setDefaultValue(IDocument::ReloadUnmodified);
    reloadSetting.setLabelText(Tr::tr("When files are externally modified:"));
    const auto syncReloadSetting = [this] {
        GlobalFileChangeBlocker::instance()->setBlockingWhileAppIsInactive(
            reloadSetting.value() != IDocument::ReloadUnmodifiedImmediately);
    };
    reloadSetting.addOnChanged(this, syncReloadSetting);

    askBeforeExit.setSettingsKey("AskBeforeExit");
    askBeforeExit.setLabelText(Tr::tr("Ask for confirmation before exiting"));
    askBeforeExit.setLabelPlacement(boolAspectLabelPlacement);

    readSettings();
    syncReloadSetting();
    updateSystemEnv();

    const auto syncDisableAtomicSave = [this] {
        Utils::FileUtils::setAtomicSaveDisabled(disableAtomicSave());
    };
    syncDisableAtomicSave();
    disableAtomicSave.addOnChanged(this, syncDisableAtomicSave);

    autoSaveInterval.setEnabler(&autoSaveModifiedFiles);
    autoSuspendMinDocumentCount.setEnabler(&autoSuspendEnabled);
    bigFileSizeLimitInMB.setEnabler(&warnBeforeOpeningBigFiles);

    autoSaveModifiedFiles.addOnChanged(this, &EditorManagerPrivate::updateAutoSave);
    autoSaveInterval.addOnChanged(this, &EditorManagerPrivate::updateAutoSave);
    enableCrashReports.addOnChanged(this, [] {
        Utils::setCrashReportingEnabled(systemSettings().enableCrashReports());
    });

    crashNow.setActionText("CRASH!!!");
    crashNow.setQmlName("CrashNow");
    crashNow.setAction([] {
        // do a real crash
        volatile int *a = reinterpret_cast<volatile int *>(NULL);
        *a = 1;
    });
    crashNow.setVisible(qtcEnvironmentVariableIsSet("QTC_SHOW_CRASHBUTTON")
                        && isCrashReportingAvailable());

    // Which of these apply is a fact about the host and the build, not about
    // the layout, so it is settled here rather than every time a page is drawn.
    terminalCommand.setVisible(HostOsInfo::isAnyUnixHost());
    externalFileBrowser.setVisible(HostOsInfo::isAnyUnixHost() && !HostOsInfo::isMacHost());
    useDbusFileManagers.setVisible(hasDBusFileManager);
    enableCrashReports.setVisible(isCrashReportingAvailable());

    environmentChangesAspect.setQmlName("EnvironmentChanges");
    envVarSeparatorAspect.setQmlName("EnvVarSeparators");
    terminalCommand.setQmlName("Terminal");

    setQmlSource(QUrl("qrc:/qt/qml/QtCreator/Core/SystemSettingsPage.qml"));
}

// SystemSettingsPage
class SystemSettingsPage final : public IOptionsPage
{
public:
    SystemSettingsPage()
    {
        setId(Constants::SETTINGS_ID_SYSTEM);
        setDisplayName(Tr::tr("System"));
        setCategory(Constants::SETTINGS_CATEGORY_CORE);
        setSettingsProvider([] { return &systemSettings(); });
    }
};

static void appendVcsPathsToPatchCommandPath()
{
    Environment env;
    env.appendToPath(VcsManager::additionalToolsPath());
    systemSettings().patchCommand.setEnvironment(env);
}

void SystemSettings::delayedInitialize()
{
    // TODO: This is a bit of a hack to get the version control system's paths into the patch
    // command's environment, but it needs to be done after all plugins have been loaded to
    // ensure that the version control plugin has had a chance to add its additional tools path.
    // We should look into a cleaner way to handle this in the future.
    connect(
        VcsManager::instance(),
        &VcsManager::configurationChanged,
        this,
        appendVcsPathsToPatchCommandPath);
    connect(
        ICore::instance(), &ICore::systemEnvironmentChanged, this, appendVcsPathsToPatchCommandPath);
    appendVcsPathsToPatchCommandPath();
}

const SystemSettingsPage settingsPage;

} // namespace Core::Internal
