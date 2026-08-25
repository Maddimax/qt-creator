// Copyright (C) 2024 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "clangdsettings.h"

#include "clangdiagnosticconfigidaspect.h"
#include "clangdiagnosticconfigsmodel.h"
#include "clangdiagnosticconfigswidget.h"
#include "cppeditorconstants.h"
#include "cppeditortr.h"
#include "cpptoolsreuse.h"

#include <coreplugin/dialogs/ioptionspage.h>
#include <coreplugin/editormanager/editormanager.h>
#include <coreplugin/icore.h>
#include <coreplugin/session.h>

#include <projectexplorer/buildconfiguration.h>
#include <projectexplorer/devicesupport/devicekitaspects.h>
#include <projectexplorer/devicesupport/idevice.h>
#include <projectexplorer/project.h>
#include <projectexplorer/projectexplorerconstants.h>
#include <projectexplorer/projectpanelfactory.h>
#include <projectexplorer/projectsettings.h>
#include <projectexplorer/useglobalaspect.h>

#include <utils/aspectwidgets.h>
#include <utils/algorithm.h>
#include <utils/shutdownguard.h>

#ifdef WITH_TESTS
#include <QTest>
#endif
#include <utils/clangutils.h>
#include <utils/guiutils.h>
#include <utils/infolabel.h>
#include <utils/itemviews.h>
#include <utils/layoutbuilder.h>
#include <utils/macroexpander.h>
#include <utils/pathchooser.h>
#include <utils/qtcprocess.h>

#include <QDesktopServices>
#include <QGroupBox>
#include <QGuiApplication>
#include <QInputDialog>
#include <QPushButton>
#include <QStandardPaths>
#include <QStringListModel>
#include <QVersionNumber>

#include <limits>

using namespace ProjectExplorer;
using namespace Utils;

namespace CppEditor {

static FilePath g_defaultClangdFilePath;
static FilePath fallbackClangdFilePath()
{
    if (g_defaultClangdFilePath.exists())
        return g_defaultClangdFilePath;
    return Environment::systemEnvironment().searchInPath("clangd");
}

static Id initialClangDiagnosticConfigId() { return Constants::CPP_CLANG_DIAG_CONFIG_BUILDSYSTEM; }

static Key clangdSettingsKey() { return "ClangdSettings"; }
static Key useClangdKey() { return "UseClangdV7"; }
static Key clangdPathKey() { return "ClangdPath"; }
static Key clangdIndexingKey() { return "ClangdIndexing"; }
static Key clangdProjectIndexPathKey() { return "ClangdProjectIndexPath"; }
static Key clangdSessionIndexPathKey() { return "ClangdSessionIndexPath"; }
static Key clangdIndexingPriorityKey() { return "ClangdIndexingPriority"; }
static Key clangdHeaderSourceSwitchModeKey() { return "ClangdHeaderSourceSwitchMode"; }
static Key clangdCompletionRankingModelKey() { return "ClangdCompletionRankingModel"; }
static Key clangdCompletionStyleKey() { return "ClangdCompletionStyle"; }
static Key clangdHeaderInsertionKey() { return "ClangdHeaderInsertion"; }
static Key clangdThreadLimitKey() { return "ClangdThreadLimit"; }
static Key clangdDocumentThresholdKey() { return "ClangdDocumentThreshold"; }
static Key clangdSizeThresholdEnabledKey() { return "ClangdSizeThresholdEnabled"; }
static Key clangdSizeThresholdKey() { return "ClangdSizeThreshold"; }
static Key useGlobalSettingsKey() { return "useGlobalSettings"; }
static Key sessionsWithOneClangdKey() { return "SessionsWithOneClangd"; }
static Key diagnosticConfigIdKey() { return "diagnosticConfigId"; }
static Key checkedHardwareKey() { return "checkedHardware"; }
static Key completionResultsKey() { return "completionResults"; }
static Key updateDependentSourcesKey() { return "updateDependentSources"; }
static Key useExternalCompilationDbKey() { return "ClangdUseExternalCompilationDb"; }

const char blockProjectIndexingProperty[] = "ClangBlockProjectIndexing";

QString ClangdSettings::priorityToString(const IndexingPriority &priority)
{
    switch (priority) {
    case IndexingPriority::Background: return "background";
    case IndexingPriority::Normal: return "normal";
    case IndexingPriority::Low: return "low";
    case IndexingPriority::Off: return {};
    }
    return {};
}

QString ClangdSettings::priorityToDisplayString(const IndexingPriority &priority)
{
    switch (priority) {
    case IndexingPriority::Background: return Tr::tr("Background Priority");
    case IndexingPriority::Normal: return Tr::tr("Normal Priority");
    case IndexingPriority::Low: return Tr::tr("Low Priority");
    case IndexingPriority::Off: return Tr::tr("Off");
    }
    return {};
}

QString ClangdSettings::headerSourceSwitchModeToDisplayString(HeaderSourceSwitchMode mode)
{
    switch (mode) {
    case HeaderSourceSwitchMode::BuiltinOnly: return Tr::tr("Use Built-in Only");
    case HeaderSourceSwitchMode::ClangdOnly: return Tr::tr("Use Clangd Only");
    case HeaderSourceSwitchMode::Both: return Tr::tr("Try Both");
    }
    return {};
}

QString ClangdSettings::rankingModelToCmdLineString(CompletionRankingModel model)
{
    switch (model) {
    case CompletionRankingModel::Default: break;
    case CompletionRankingModel::DecisionForest: return "decision_forest";
    case CompletionRankingModel::Heuristics: return "heuristics";
    }
    QTC_ASSERT(false, return {});
}

QString ClangdSettings::rankingModelToDisplayString(CompletionRankingModel model)
{
    switch (model) {
    case CompletionRankingModel::Default: return Tr::tr("Default");
    case CompletionRankingModel::DecisionForest: return Tr::tr("Decision Forest");
    case CompletionRankingModel::Heuristics: return Tr::tr("Heuristics");
    }
    QTC_ASSERT(false, return {});
}

QString ClangdSettings::completionStyleToCmdLineString(CompletionStyle style)
{
    switch (style) {
    case CompletionStyle::Default: break;
    case CompletionStyle::Detailed: return "detailed";
    case CompletionStyle::Bundled: return "bundled";
    }
    QTC_ASSERT(false, return {});
}

QString ClangdSettings::completionStyleToDisplayString(CompletionStyle style)
{
    switch (style) {
    case CompletionStyle::Default: return Tr::tr("Default");
    case CompletionStyle::Detailed: return Tr::tr("Detailed");
    case CompletionStyle::Bundled: return Tr::tr("Bundled");
    }
    QTC_ASSERT(false, return {});
}

QString ClangdSettings::defaultProjectIndexPathTemplate()
{
    return QDir::toNativeSeparators("%{BuildConfig:BuildDirectory:FilePath}/.qtc_clangd");
}

QString ClangdSettings::defaultSessionIndexPathTemplate()
{
    return QDir::toNativeSeparators("%{IDE:UserResourcePath}/.qtc_clangd/%{Session:FileBaseName}");
}

ClangdSettings &ClangdSettings::instance()
{
    static ClangdSettings settings;
    return settings;
}

static Layouting::Layout clangdSettingsLayout(ClangdSettings *s)
{
    auto *versionWarning = new InfoLabel;
    versionWarning->setType(InfoLabelType::Warning);
    versionWarning->setVisible(false);
    const auto updateWarning = [s, versionWarning] {
        const FilePath path = s->clangdPath();
        if (path.isEmpty()) {
            versionWarning->setVisible(false);
            return;
        }
        const Result<> res = checkClangdVersion(path);
        versionWarning->setVisible(!res);
        if (!res)
            versionWarning->setText(res.error());
    };
    s->clangdPath.addOnChanged(versionWarning, updateWarning);
    // avoid popping up before getting parented
    QMetaObject::invokeMethod(s, [updateWarning]{ updateWarning(); }, Qt::QueuedConnection);

    using namespace Layouting;
    // clang-format off
    return Column {
        s->useClangd,
        Form {
            s->clangdPath, br,
            empty, versionWarning, br,
            s->indexingPriority, br,
            s->projectIndexPathTemplate, br,
            s->sessionIndexPathTemplate, br,
            s->headerSourceSwitchMode, br,
            s->workerThreadLimit, br,
            empty, s->autoIncludeHeaders, br,
            empty, s->updateDependentSources, br,
            empty, s->useExternalCompilationDb, br,
            s->completionResults, br,
            s->completionRankingModel, br,
            s->completionStyle, br,
            s->documentUpdateThreshold, br,
            s->sizeThresholdEnabled,
                Row { s->sizeThresholdInKb, st }, br,
        },
        s->diagnosticConfigId,
        noMargin,
    };
    // clang-format on
}

ClangdSettings::ClangdSettings()
{
    setSettingsGroup("ClangdSettings");

    useClangd.setSettingsKey(useClangdKey());
    useClangd.setQmlName("UseClangd");
    useClangd.setDefaultValue(true);
    useClangd.setLabelText(Tr::tr("Use clangd"));

    clangdPath.setSettingsKey(clangdPathKey());
    clangdPath.setQmlName("ClangdPath");
    clangdPath.setLabelText(Tr::tr("Path to executable:"));
    clangdPath.setExpectedKind(PathChooserKind::ExistingCommand);
    clangdPath.setAllowPathFromDevice(true);
    clangdPath.setCommandVersionArguments({"--version"});

    autoIncludeHeaders.setSettingsKey(clangdHeaderInsertionKey());
    autoIncludeHeaders.setQmlName("AutoIncludeHeaders");
    autoIncludeHeaders.setDefaultValue(false);
    autoIncludeHeaders.setLabelText(Tr::tr("Insert header files on completion"));
    autoIncludeHeaders.setToolTip(
        Tr::tr("Controls whether clangd may insert header files as part of symbol completion."));

    sizeThresholdEnabled.setSettingsKey(clangdSizeThresholdEnabledKey());
    sizeThresholdEnabled.setQmlName("SizeThresholdEnabled");
    sizeThresholdEnabled.setDefaultValue(false);
    sizeThresholdEnabled.setLabelText(Tr::tr("Ignore files greater than"));

    updateDependentSources.setSettingsKey(updateDependentSourcesKey());
    updateDependentSources.setQmlName("UpdateDependentSources");
    updateDependentSources.setDefaultValue(false);
    updateDependentSources.setLabelText(Tr::tr("Update dependent sources"));
    updateDependentSources.setToolTip(Tr::tr(
        "<p>Controls whether when editing a header file, clangd should re-parse all source files "
        "including that header.</p>"
        "<p>Note that enabling this option can cause considerable CPU load when editing widely "
        "included headers.</p>"
        "<p>If this option is disabled, the dependent source files are only re-parsed when the "
        "header file is saved.</p>"));

    useExternalCompilationDb.setSettingsKey(useExternalCompilationDbKey());
    useExternalCompilationDb.setQmlName("UseExternalCompilationDb");
    useExternalCompilationDb.setDefaultValue(false);
    useExternalCompilationDb.setLabelText(
        Tr::tr("Use externally provided compilation database"));
    useExternalCompilationDb.setToolTip(Tr::tr(
        "<p>Controls whether clangd will use an existing compile_commands.json file, rather than "
        "one set up by Qt Creator, which is the default.</p>"
        "<p>When enabling this option, the user is responsible for providing a suitable file at "
        "the index location specified above, as well as for keeping that file in sync with the "
        "project state.</p>"));

    workerThreadLimit.setSettingsKey(clangdThreadLimitKey());
    workerThreadLimit.setQmlName("WorkerThreadLimit");
    workerThreadLimit.setLabelText(Tr::tr("Worker thread count:"));
    workerThreadLimit.setDefaultValue(0);
    workerThreadLimit.setSpecialValueText(Tr::tr("Automatic"));
    workerThreadLimit.setToolTip(Tr::tr(
        "Number of worker threads used by clangd. Background indexing also uses this many "
        "worker threads."));

    documentUpdateThreshold.setSettingsKey(clangdDocumentThresholdKey());
    documentUpdateThreshold.setQmlName("DocumentUpdateThreshold");
    documentUpdateThreshold.setLabelText(Tr::tr("Document update threshold:"));
    documentUpdateThreshold.setDefaultValue(500);
    documentUpdateThreshold.setRange(50, 10000);
    documentUpdateThreshold.setSingleStep(100);
    documentUpdateThreshold.setSuffix(" ms");
    documentUpdateThreshold.setToolTip(
        //: %1 is the application name (Qt Creator)
        Tr::tr("Defines the amount of time %1 waits before sending document changes to the "
               "server.\n"
               "If the document changes again while waiting, this timeout resets.")
            .arg(QGuiApplication::applicationDisplayName()));

    sizeThresholdInKb.setSettingsKey(clangdSizeThresholdKey());
    sizeThresholdInKb.setQmlName("SizeThresholdInKb");
    sizeThresholdInKb.setDefaultValue(1024);
    sizeThresholdInKb.setRange(1, std::numeric_limits<int>::max());
    sizeThresholdInKb.setSuffix(" KB");

    const QString sizeThresholdToolTip = Tr::tr(
        "Files greater than this will not be opened as documents in clangd.\n"
        "The built-in code model will handle highlighting, completion and so on.");
    sizeThresholdEnabled.setToolTip(sizeThresholdToolTip);
    sizeThresholdInKb.setToolTip(sizeThresholdToolTip);

    completionResults.setSettingsKey(completionResultsKey());
    completionResults.setQmlName("CompletionResults");
    completionResults.setLabelText(Tr::tr("Completion results:"));
    completionResults.setDefaultValue(Data::defaultCompletionResults());
    completionResults.setRange(0, std::numeric_limits<int>::max());
    completionResults.setSpecialValueText(Tr::tr("No limit"));
    completionResults.setToolTip(
        Tr::tr("The maximum number of completion results returned by clangd."));

    projectIndexPathTemplate.setSettingsKey(clangdProjectIndexPathKey());
    projectIndexPathTemplate.setQmlName("ProjectIndexPathTemplate");
    projectIndexPathTemplate.setLabelText(Tr::tr("Per-project index location:"));
    projectIndexPathTemplate.setDefaultValue(defaultProjectIndexPathTemplate());
    projectIndexPathTemplate.setDisplayStyle(StringAspect::LineEditDisplay);
    projectIndexPathTemplate.setUseResetButton();
    projectIndexPathTemplate.setToolTip(
        Tr::tr("The location of the per-project clangd index.<p>"
               "This is also where the compile_commands.json file will go."));

    sessionIndexPathTemplate.setSettingsKey(clangdSessionIndexPathKey());
    sessionIndexPathTemplate.setQmlName("SessionIndexPathTemplate");
    sessionIndexPathTemplate.setLabelText(Tr::tr("Per-session index location:"));
    sessionIndexPathTemplate.setDefaultValue(defaultSessionIndexPathTemplate());
    sessionIndexPathTemplate.setDisplayStyle(StringAspect::LineEditDisplay);
    sessionIndexPathTemplate.setUseResetButton();
    sessionIndexPathTemplate.setToolTip(
        Tr::tr("The location of the per-session clangd index.<p>"
               "This is also where the compile_commands.json file will go."));

    // IndexingPriority: options added in enum-value order so stored int == index
    using Priority = IndexingPriority;
    indexingPriority.setSettingsKey(clangdIndexingPriorityKey());
    indexingPriority.setQmlName("IndexingPriority");
    indexingPriority.setLabelText(Tr::tr("Background indexing:"));
    indexingPriority.setDefaultValue(IndexingPriority::Low);
    indexingPriority.setDisplayStyle(SelectionAspect::DisplayStyle::ComboBox);
    for (Priority p : {Priority::Off, Priority::Background, Priority::Normal, Priority::Low})
        indexingPriority.addOption(priorityToDisplayString(p));
    indexingPriority.setToolTip(Tr::tr(
        "<p>If background indexing is enabled, global symbol searches will yield more accurate "
        "results, at the cost of additional CPU load when the project is first opened. The "
        "indexing result is persisted in the project's build directory. If you disable background "
        "indexing, a faster, but less accurate, built-in indexer is used instead. The thread "
        "priority for building the background index can be adjusted since clangd 15.</p>"
        "<p>Background Priority: Minimum priority, runs on idle CPUs. May leave 'performance' "
        "cores unused.</p>"
        "<p>Normal Priority: Reduced priority compared to interactive work.</p>"
        "<p>Low Priority: Same priority as other clangd work.</p>"));

    using SwitchMode = HeaderSourceSwitchMode;
    headerSourceSwitchMode.setSettingsKey(clangdHeaderSourceSwitchModeKey());
    headerSourceSwitchMode.setQmlName("HeaderSourceSwitchMode");
    headerSourceSwitchMode.setLabelText(Tr::tr("Header/source switch mode:"));
    headerSourceSwitchMode.setDefaultValue(HeaderSourceSwitchMode::Both);
    headerSourceSwitchMode.setDisplayStyle(SelectionAspect::DisplayStyle::ComboBox);
    for (SwitchMode m : {SwitchMode::BuiltinOnly, SwitchMode::ClangdOnly, SwitchMode::Both})
        headerSourceSwitchMode.addOption(headerSourceSwitchModeToDisplayString(m));
    headerSourceSwitchMode.setToolTip(Tr::tr(
        "<p>The C/C++ backend to use for switching between header and source files.</p>"
        "<p>While the clangd implementation has more capabilities than the built-in "
        "code model, it tends to find false positives.</p>"
        "<p>When \"Try Both\" is selected, clangd is used only if the built-in variant "
        "does not find anything.</p>"));

    using RankingModel = CompletionRankingModel;
    completionRankingModel.setSettingsKey(clangdCompletionRankingModelKey());
    completionRankingModel.setQmlName("CompletionRankingModel");
    completionRankingModel.setLabelText(Tr::tr("Completion ranking model:"));
    completionRankingModel.setDefaultValue(CompletionRankingModel::Default);
    completionRankingModel.setDisplayStyle(SelectionAspect::DisplayStyle::ComboBox);
    for (RankingModel m : {RankingModel::Default, RankingModel::DecisionForest,
                           RankingModel::Heuristics})
        completionRankingModel.addOption(rankingModelToDisplayString(m));
    completionRankingModel.setToolTip(
        Tr::tr("<p>Which model clangd should use to rank possible completions.</p>"
               "<p>This determines the order of candidates in the combo box when doing code "
               "completion.</p>"
               "<p>The \"%1\" model used by default results from (pre-trained) machine learning "
               "and provides superior results on average.</p>"
               "<p>If you feel that its suggestions stray too much from your expectations for "
               "your code base, you can try switching to the hand-crafted \"%2\" model.</p>")
            .arg(rankingModelToDisplayString(RankingModel::DecisionForest),
                 rankingModelToDisplayString(RankingModel::Heuristics)));

    using Style = CompletionStyle;
    completionStyle.setSettingsKey(clangdCompletionStyleKey());
    completionStyle.setQmlName("CompletionStyle");
    completionStyle.setLabelText(Tr::tr("Completion style:"));
    completionStyle.setDefaultValue(CompletionStyle::Default);
    completionStyle.setDisplayStyle(SelectionAspect::DisplayStyle::ComboBox);
    for (Style s : {Style::Default, Style::Detailed, Style::Bundled})
        completionStyle.addOption(completionStyleToDisplayString(s));
    completionStyle.setToolTip(
        Tr::tr("<p>Which granularity to use for completion items.</p>"
               "<p>Determines whether to use one item per overload or bundle them "
               "together.</p>"));

    diagnosticConfigId.setSettingsKey(diagnosticConfigIdKey());
    diagnosticConfigId.setQmlName("DiagnosticConfigId");
    diagnosticConfigId.setDefaultValue(initialClangDiagnosticConfigId());
    diagnosticConfigId.setModelFactory([] { return diagnosticConfigsModel(); });
    diagnosticConfigId.setEditWidgetFactory(
        [](const ClangDiagnosticConfigs &configs, const Id &id) {
            return new ClangDiagnosticConfigsWidget(configs, id);
        });

    for (BaseAspect *aspect : aspects()) {
        if (aspect != &useClangd)
            aspect->setEnabler(&useClangd);
    }
    sizeThresholdInKb.setEnabler(&sizeThresholdEnabled);

    Utils::AspectWidgets::setLayouter(this, [this] { return clangdSettingsLayout(this); });

    loadSettings();

    const auto sessionMgr = Core::SessionManager::instance();
    connect(sessionMgr, &Core::SessionManager::sessionRemoved, this, [this](const QString &name) {
        m_data.sessionsWithOneClangd.removeOne(name);
    });
    connect(sessionMgr,
            &Core::SessionManager::sessionRenamed,
            this,
            [this](const QString &oldName, const QString &newName) {
                const auto index = m_data.sessionsWithOneClangd.indexOf(oldName);
                if (index != -1)
                    m_data.sessionsWithOneClangd[index] = newName;
            });
}

bool ClangdSettings::Data::useGoodClangd(const Kit *kit) const
{
    return useClangd && clangdVersion(clangdFilePath(kit)) >= minimumClangdVersion();
}

void ClangdSettings::setUseClangd(bool use)
{
    instance().useClangd.setValue(use, BaseAspect::BeQuiet);
}

void ClangdSettings::setUseClangdAndSave(bool use)
{
    setUseClangd(use);
    instance().saveSettings();
    emit instance().changed();
}

bool ClangdSettings::hardwareFulfillsRequirements()
{
    instance().m_data.haveCheckedHardwareReqirements = true;
    instance().saveSettings();
    const quint64 minRam = quint64(12) * 1024 * 1024 * 1024;
    const std::optional<quint64> totalRam = HostOsInfo::totalMemoryInstalledInBytes();
    return !totalRam || *totalRam >= minRam;
}

bool ClangdSettings::haveCheckedHardwareRequirements()
{
    return instance().data().haveCheckedHardwareReqirements;
}

void ClangdSettings::setDefaultClangdPath(const FilePath &filePath)
{
    g_defaultClangdFilePath = filePath;
}

void ClangdSettings::setCustomDiagnosticConfigs(const ClangDiagnosticConfigs &configs)
{
    if (instance().m_data.customDiagnosticConfigs == configs)
        return;
    instance().m_data.customDiagnosticConfigs = configs;
    instance().saveSettings();
}

ClangDiagnosticConfigsModel ClangdSettings::diagnosticConfigsModel()
{
    const ClangDiagnosticConfigs &customConfigs = instance().m_data.customDiagnosticConfigs;
    ClangDiagnosticConfigsModel model;
    model.addBuiltinConfigs();
    for (const ClangDiagnosticConfig &config : customConfigs)
        model.appendOrUpdate(config);
    return model;
}

FilePath ClangdSettings::Data::clangdFilePath(const Kit *kit) const
{
    if (kit && BuildDeviceTypeKitAspect::deviceTypeId(kit)
                   != ProjectExplorer::Constants::DESKTOP_DEVICE_TYPE) {
        if (const IDeviceConstPtr buildDevice = BuildDeviceKitAspect::device(kit)) {
            FilePath clangd = buildDevice->deviceToolPath(CppEditor::Constants::CLANGD_TOOL_ID);
            if (!clangd.isEmpty())
                return clangd;
        }
    }

    if (!executableFilePath.isEmpty())
        return executableFilePath;
    return fallbackClangdFilePath();
}

FilePath ClangdSettings::Data::projectIndexPath(const MacroExpander &expander) const
{
    return FilePath::fromUserInput(expander.expand(projectIndexPathTemplate));
}

FilePath ClangdSettings::Data::sessionIndexPath(const MacroExpander &expander) const
{
    return FilePath::fromUserInput(expander.expand(sessionIndexPathTemplate));
}

bool ClangdSettings::Data::sizeIsOkay(const FilePath &fp) const
{
    return !sizeThresholdEnabled || sizeThresholdInKb * 1024 >= fp.fileSize();
}

Id ClangdSettings::Data::diagnosticConfigIdOrDefault() const
{
    if (diagnosticConfigsModel().hasConfigWithId(diagnosticConfigId))
        return diagnosticConfigId;
    return initialClangDiagnosticConfigId();
}

ClangDiagnosticConfig ClangdSettings::Data::diagnosticConfig() const
{
    return diagnosticConfigsModel().configWithId(diagnosticConfigIdOrDefault());
}

bool ClangdSettings::Data::isSessionMode() const
{
    return granularity() == Granularity::Session;
}

ClangdSettings::Data::Granularity ClangdSettings::Data::granularity() const
{
    if (sessionsWithOneClangd.contains(Core::SessionManager::activeSession()))
        return Granularity::Session;
    return Granularity::Project;
}

ClangdSettings::Data ClangdSettings::data() const
{
    Data d;
    d.useClangd = useClangd();
    d.executableFilePath = clangdPath();
    d.autoIncludeHeaders = autoIncludeHeaders();
    d.sizeThresholdEnabled = sizeThresholdEnabled();
    d.updateDependentSources = updateDependentSources();
    d.useExternalCompilationDb = useExternalCompilationDb();
    d.workerThreadLimit = int(workerThreadLimit());
    d.documentUpdateThreshold = int(documentUpdateThreshold());
    d.sizeThresholdInKb = sizeThresholdInKb();
    d.completionResults = int(completionResults());
    d.projectIndexPathTemplate = projectIndexPathTemplate();
    d.sessionIndexPathTemplate = sessionIndexPathTemplate();
    d.indexingPriority = indexingPriority();
    d.headerSourceSwitchMode = headerSourceSwitchMode();
    d.completionRankingModel = completionRankingModel();
    d.completionStyle = completionStyle();
    d.sessionsWithOneClangd = m_data.sessionsWithOneClangd;
    d.customDiagnosticConfigs = m_data.customDiagnosticConfigs;
    d.diagnosticConfigId = diagnosticConfigId();
    d.haveCheckedHardwareReqirements = m_data.haveCheckedHardwareReqirements;
    return d;
}

void ClangdSettings::setData(const Data &data)
{
    if (this == &instance() && data != this->data()) {
        const BaseAspect::Announcement beQuiet = BaseAspect::BeQuiet;
        useClangd.setValue(data.useClangd, beQuiet);
        clangdPath.setValue(data.executableFilePath, beQuiet);
        autoIncludeHeaders.setValue(data.autoIncludeHeaders, beQuiet);
        sizeThresholdEnabled.setValue(data.sizeThresholdEnabled, beQuiet);
        updateDependentSources.setValue(data.updateDependentSources, beQuiet);
        useExternalCompilationDb.setValue(data.useExternalCompilationDb, beQuiet);
        workerThreadLimit.setValue(data.workerThreadLimit, beQuiet);
        documentUpdateThreshold.setValue(data.documentUpdateThreshold, beQuiet);
        sizeThresholdInKb.setValue(data.sizeThresholdInKb, beQuiet);
        completionResults.setValue(data.completionResults, beQuiet);
        projectIndexPathTemplate.setValue(data.projectIndexPathTemplate, beQuiet);
        sessionIndexPathTemplate.setValue(data.sessionIndexPathTemplate, beQuiet);
        indexingPriority.setValue(data.indexingPriority, beQuiet);
        headerSourceSwitchMode.setValue(data.headerSourceSwitchMode, beQuiet);
        completionRankingModel.setValue(data.completionRankingModel, beQuiet);
        completionStyle.setValue(data.completionStyle, beQuiet);
        diagnosticConfigId.setValue(data.diagnosticConfigId, beQuiet);
        m_data.sessionsWithOneClangd = data.sessionsWithOneClangd;
        m_data.customDiagnosticConfigs = data.customDiagnosticConfigs;
        m_data.haveCheckedHardwareReqirements = data.haveCheckedHardwareReqirements;
    }
}

static FilePath getClangHeadersPathFromClang(const FilePath &clangdFilePath)
{
    const FilePath clangFilePath = clangdFilePath.absolutePath().pathAppended("clang")
                                       .withExecutableSuffix();
    if (!clangFilePath.exists())
        return {};
    Process clang;
    clang.setCommand({clangFilePath, {"-print-resource-dir"}});
    clang.start();
    if (!clang.waitForFinished())
        return {};
    const FilePath resourceDir = FilePath::fromUserInput(QString::fromLocal8Bit(
        clang.rawStdOut().trimmed()));
    if (resourceDir.isEmpty() || !resourceDir.exists())
        return {};
    const FilePath includeDir = resourceDir.pathAppended("include");
    if (!includeDir.exists())
        return {};
    return includeDir;
}

static FilePath getClangHeadersPath(const FilePath &clangdFilePath)
{
    const FilePath headersPath = getClangHeadersPathFromClang(clangdFilePath);
    if (!headersPath.isEmpty())
        return headersPath;

    const QVersionNumber version = Utils::clangdVersion(clangdFilePath);
    QTC_ASSERT(!version.isNull(), return {});
    static const QStringList libDirs{"lib", "lib64"};
    const QStringList versionStrings{QString::number(version.majorVersion()), version.toString()};
    for (const QString &libDir : libDirs) {
        for (const QString &versionString : versionStrings) {
            const FilePath includePath = clangdFilePath.absolutePath().parentDir()
                                             .pathAppended(libDir).pathAppended("clang")
                                             .pathAppended(versionString).pathAppended("include");
            if (includePath.exists())
                return includePath;
        }
    }
    QTC_CHECK(false);
    return {};
}

FilePath ClangdSettings::Data::clangdIncludePath(const Kit *kit) const
{
    QTC_ASSERT(useGoodClangd(kit), return {});
    FilePath clangdPath = clangdFilePath(kit);
    QTC_ASSERT(!clangdPath.isEmpty() && clangdPath.exists(), return {});
    static QHash<FilePath, FilePath> headersPathCache;
    const auto it = headersPathCache.constFind(clangdPath);
    if (it != headersPathCache.constEnd())
        return *it;
    const FilePath headersPath = getClangHeadersPath(clangdPath);
    if (!headersPath.isEmpty())
        headersPathCache.insert(clangdPath, headersPath);
    return headersPath;
}

FilePath ClangdSettings::clangdUserConfigFilePath()
{
    return FilePath::fromString(
               QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation))
           / "clangd/config.yaml";
}

void ClangdSettings::loadSettings()
{
    const auto settings = Core::ICore::settings();

    // Read aspects from QtcSettings (respects the "ClangdSettings" group)
    AspectContainer::readSettings();

    // Pre-8.0 compat: old boolean indexing key
    settings->beginGroup(clangdSettingsKey());
    if (settings->contains(clangdIndexingKey())
            && !settings->value(clangdIndexingKey()).toBool()) {
        indexingPriority.setValue(IndexingPriority::Off);
    }
    settings->endGroup();

    // Complex fields not covered by aspects
    const Store store = storeFromSettings(clangdSettingsKey(), settings);
    m_data.sessionsWithOneClangd =
        store.value(sessionsWithOneClangdKey()).toStringList();
    m_data.haveCheckedHardwareReqirements =
        store.value(checkedHardwareKey(), false).toBool();

    settings->beginGroup(Constants::CPPEDITOR_SETTINGSGROUP);
    m_data.customDiagnosticConfigs = diagnosticConfigsFromSettings(settings);

    // Pre-8.0 compat
    static const Key oldKey("ClangDiagnosticConfig");
    const QVariant configId = settings->value(oldKey);
    if (configId.isValid()) {
        diagnosticConfigId.setValue(Id::fromSetting(configId));
        settings->setValue(oldKey, {});
    }

    settings->endGroup();
}

void ClangdSettings::saveSettings()
{
    const auto settings = Core::ICore::settings();

    // Write aspects (respects the "ClangdSettings" group)
    AspectContainer::writeSettings();

    // Complex fields alongside aspect data
    settings->beginGroup(clangdSettingsKey());
    settings->setValue(sessionsWithOneClangdKey(), m_data.sessionsWithOneClangd);
    settings->setValue(checkedHardwareKey(), m_data.haveCheckedHardwareReqirements);
    settings->endGroup();

    settings->beginGroup(Constants::CPPEDITOR_SETTINGSGROUP);
    diagnosticConfigsToSettings(settings, m_data.customDiagnosticConfigs);
    settings->endGroup();
}

#ifdef WITH_TESTS
void ClangdSettings::setClangdFilePath(const FilePath &filePath)
{
    instance().clangdPath.setValue(filePath, BaseAspect::BeQuiet);
}
#endif

Store ClangdSettings::Data::toMap() const
{
    Store map;

    map.insert(useClangdKey(), useClangd);

    map.insert(clangdPathKey(),
               executableFilePath != fallbackClangdFilePath() ? executableFilePath.toSettings()
                                                              : QVariant());

    map.insert(clangdIndexingKey(), indexingPriority != IndexingPriority::Off);
    map.insert(clangdIndexingPriorityKey(), int(indexingPriority));
    map.insert(clangdProjectIndexPathKey(), projectIndexPathTemplate);
    map.insert(clangdSessionIndexPathKey(), sessionIndexPathTemplate);
    map.insert(clangdHeaderSourceSwitchModeKey(), int(headerSourceSwitchMode));
    map.insert(clangdCompletionRankingModelKey(), int(completionRankingModel));
    map.insert(clangdCompletionStyleKey(), int(completionStyle));
    map.insert(clangdHeaderInsertionKey(), autoIncludeHeaders);
    map.insert(clangdThreadLimitKey(), workerThreadLimit);
    map.insert(clangdDocumentThresholdKey(), documentUpdateThreshold);
    map.insert(clangdSizeThresholdEnabledKey(), sizeThresholdEnabled);
    map.insert(clangdSizeThresholdKey(), sizeThresholdInKb);
    map.insert(sessionsWithOneClangdKey(), sessionsWithOneClangd);
    map.insert(diagnosticConfigIdKey(), diagnosticConfigId.toSetting());
    map.insert(checkedHardwareKey(), haveCheckedHardwareReqirements);
    map.insert(completionResultsKey(), completionResults);
    map.insert(updateDependentSourcesKey(), updateDependentSources);
    map.insert(useExternalCompilationDbKey(), useExternalCompilationDb);
    return map;
}

void ClangdSettings::Data::fromMap(const Store &map)
{
    useClangd = map.value(useClangdKey(), true).toBool();
    executableFilePath = FilePath::fromSettings(map.value(clangdPathKey()));
    indexingPriority = IndexingPriority(
        map.value(clangdIndexingPriorityKey(), int(this->indexingPriority)).toInt());
    const auto it = map.find(clangdIndexingKey());
    if (it != map.end() && !it->toBool())
        indexingPriority = IndexingPriority::Off;
    projectIndexPathTemplate
        = map.value(clangdProjectIndexPathKey(), defaultProjectIndexPathTemplate()).toString();
    sessionIndexPathTemplate
        = map.value(clangdSessionIndexPathKey(), defaultSessionIndexPathTemplate()).toString();
    headerSourceSwitchMode = HeaderSourceSwitchMode(map.value(clangdHeaderSourceSwitchModeKey(),
                                                              int(headerSourceSwitchMode)).toInt());
    completionRankingModel = CompletionRankingModel(map.value(clangdCompletionRankingModelKey(),
                                                              int(completionRankingModel)).toInt());
    completionStyle = CompletionStyle(
        map.value(clangdCompletionStyleKey(), int(completionStyle)).toInt());
    autoIncludeHeaders = map.value(clangdHeaderInsertionKey(), false).toBool();
    useExternalCompilationDb = map.value(useExternalCompilationDbKey(), false).toBool();
    workerThreadLimit = map.value(clangdThreadLimitKey(), 0).toInt();
    documentUpdateThreshold = map.value(clangdDocumentThresholdKey(), 500).toInt();
    sizeThresholdEnabled = map.value(clangdSizeThresholdEnabledKey(), false).toBool();
    sizeThresholdInKb = map.value(clangdSizeThresholdKey(), 1024).toLongLong();
    sessionsWithOneClangd = map.value(sessionsWithOneClangdKey()).toStringList();
    diagnosticConfigId = Id::fromSetting(map.value(diagnosticConfigIdKey(),
                                                   initialClangDiagnosticConfigId().toSetting()));
    haveCheckedHardwareReqirements = map.value(checkedHardwareKey(), false).toBool();
    updateDependentSources = map.value(updateDependentSourcesKey(), false).toBool();
    completionResults = map.value(completionResultsKey(), defaultCompletionResults()).toInt();
}

int ClangdSettings::Data::defaultCompletionResults()
{
    // Default clangd --limit-results value is 100
    bool ok = false;
    const int userValue = qtcEnvironmentVariableIntValue("QTC_CLANGD_COMPLETION_RESULTS", &ok);
    return ok ? userValue : 100;
}

class ClangdProjectSettings : public ClangdSettings
{
public:
    explicit ClangdProjectSettings(Project *project)
        : m_project(project)
    {
        // Base constructor loaded global settings into aspects; now override
        // with project-specific values if applicable.
        setAutoApply(true);
        Utils::AspectWidgets::setLayouter(this, [this] { return clangdSettingsLayout(this); });

        const Store store =
            storeFromVariant(project->namedSettings(clangdSettingsKey()));
        const bool global =
            store.value(useGlobalSettingsKey(), true).toBool();
        useGlobalSettings.setValue(global);
        if (!global) {
            Data d;
            d.fromMap(store);
            useClangd.setValue(d.useClangd);
            clangdPath.setValue(d.executableFilePath);
            autoIncludeHeaders.setValue(d.autoIncludeHeaders);
            sizeThresholdEnabled.setValue(d.sizeThresholdEnabled);
            updateDependentSources.setValue(d.updateDependentSources);
            useExternalCompilationDb.setValue(d.useExternalCompilationDb);
            workerThreadLimit.setValue(d.workerThreadLimit);
            documentUpdateThreshold.setValue(d.documentUpdateThreshold);
            sizeThresholdInKb.setValue(d.sizeThresholdInKb);
            completionResults.setValue(d.completionResults);
            projectIndexPathTemplate.setValue(d.projectIndexPathTemplate);
            indexingPriority.setValue(d.indexingPriority);
            headerSourceSwitchMode.setValue(d.headerSourceSwitchMode);
            completionRankingModel.setValue(d.completionRankingModel);
            completionStyle.setValue(d.completionStyle);
            diagnosticConfigId.setValue(d.diagnosticConfigId);
            m_data.customDiagnosticConfigs = d.customDiagnosticConfigs;
        }

        setEnabled(!useGlobalSettings());

        useGlobalSettings.addOnChanged(this, [this] {
            setEnabled(!useGlobalSettings());
            save();
            emit ClangdSettings::instance().changed();
        });
        addOnChanged(this, [this] {
            if (!useGlobalSettings())
                save();
        });

        connect(&diagnosticConfigId, &BaseAspect::changed, this, [this] {
            m_ownDiagConfigChange = true;
            emit ClangdSettings::instance().changed();
            m_ownDiagConfigChange = false;
        });
        connect(&ClangdSettings::instance(), &ClangdSettings::changed, this, [this] {
            if (data().isSessionMode())
                useGlobalSettings.setValue(true);
            if (!m_ownDiagConfigChange) {
                if (useGlobalSettings())
                    diagnosticConfigId.setValue(ClangdSettings::instance().diagnosticConfigId(),
                                                BaseAspect::BeQuiet);
                diagnosticConfigId.refresh();
            }
        });
    }

    void save()
    {
        // Only sync customConfigs from the widget when it has been shown;
        // otherwise keep whatever was loaded from settings.
        if (diagnosticConfigId.hasWidget())
            m_data.customDiagnosticConfigs = diagnosticConfigId.customConfigs();
        Store store;
        store.insert(useGlobalSettingsKey(), useGlobalSettings());
        if (!useGlobalSettings()) {
            store = data().toMap();
            store.insert(useGlobalSettingsKey(), false);
        }
        m_project->setNamedSettings(clangdSettingsKey(), variantFromStore(store));
    }

    static Key extraDataKey() { return "ClangdProjectSettings"; }

    UseGlobalAspect useGlobalSettings{Constants::CPP_CLANGD_SETTINGS_ID};

private:
    Project * const m_project;
    bool m_ownDiagConfigChange = false;
};

static ClangdProjectSettings *clangdProjectSettings(Project *project)
{
    return projectSettings<ClangdProjectSettings>(project);
}

ClangdSettings::Data clangdSettingsForProject(Project *project)
{
    if (!project)
        return ClangdSettings::instance().data();
    const auto *ps = clangdProjectSettings(project);
    ClangdSettings::Data d = ps->useGlobalSettings()
        ? ClangdSettings::instance().data() : ps->data();
    if (project->property(blockProjectIndexingProperty).toBool())
        d.indexingPriority = ClangdSettings::IndexingPriority::Off;
    return d;
}

ClangdSettings::Data clangdSettingsForProject(BuildConfiguration *bc)
{
    return clangdSettingsForProject(bc ? bc->project() : nullptr);
}

void clangdBlockIndexingForProject(Project *project)
{
    QTC_ASSERT(project, return);
    project->setProperty(blockProjectIndexingProperty, true);

    emit ClangdSettings::instance().changed();
}

void clangdUnblockIndexingForProject(Project *project)
{
    QTC_ASSERT(project, return);
    project->setProperty(blockProjectIndexingProperty, false);
}

void clangdSetDiagnosticConfigId(Project *project, Id id)
{
    QTC_ASSERT(project, return);
    ClangdProjectSettings *ps = clangdProjectSettings(project);
    ps->diagnosticConfigId.setValue(id, BaseAspect::BeQuiet);
    ps->useGlobalSettings.setValue(false, BaseAspect::BeQuiet);
    ps->save();
    emit ClangdSettings::instance().changed();
}

namespace Internal {

// ClangdSessionsAspect

// Which sessions share one clangd. A session is one of the ones there are, so
// a row offers them as a choice - the widget page asked for it in a modal
// combo box dialog, and offered nothing when they were all taken.
class ClangdSessionsModel final : public QAbstractTableModel
{
public:
    using QAbstractTableModel::QAbstractTableModel;

    int rowCount(const QModelIndex &parent = {}) const override
    {
        return parent.isValid() ? 0 : int(m_sessions.size());
    }

    int columnCount(const QModelIndex &parent = {}) const override
    {
        Q_UNUSED(parent)
        return 1;
    }

    QVariant headerData(int section, Qt::Orientation orientation, int role) const override
    {
        if (role != Qt::DisplayRole || orientation != Qt::Horizontal || section != 0)
            return {};
        return Tr::tr("Session");
    }

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override
    {
        if (!index.isValid() || index.row() >= m_sessions.size())
            return {};
        if (role == Qt::DisplayRole || role == Qt::EditRole)
            return m_sessions.at(index.row());
        if (role == AspectTable::EditableRole)
            return true;
        if (role == AspectTable::ChoicesRole) {
            QVariantList choices;
            for (const QString &name : available(index.row()))
                choices.append(QVariantMap{{"display", name}, {"id", name}});
            return choices;
        }
        return {};
    }

    bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override
    {
        if (role != Qt::EditRole || !index.isValid() || index.row() >= m_sessions.size())
            return false;
        const QString name = value.toString();
        // The same session twice would mean nothing, and it is not on offer.
        if (!available(index.row()).contains(name))
            return false;
        m_sessions[index.row()] = name;
        emit dataChanged(index, index);
        return true;
    }

    bool insertRows(int row, int count, const QModelIndex &parent = {}) override
    {
        if (parent.isValid() || count != 1)
            return false;
        const QStringList unused = available(-1);
        if (unused.isEmpty())
            return false;
        beginInsertRows({}, row, row);
        m_sessions.insert(row, unused.first());
        endInsertRows();
        return true;
    }

    bool removeRows(int row, int count, const QModelIndex &parent = {}) override
    {
        if (parent.isValid() || row < 0 || row + count > m_sessions.size())
            return false;
        beginRemoveRows({}, row, row + count - 1);
        m_sessions.remove(row, count);
        endRemoveRows();
        return true;
    }

    QHash<int, QByteArray> roleNames() const override
    {
        return AspectTable::withRoleNames(QAbstractTableModel::roleNames());
    }

    void setSessions(const QStringList &sessions)
    {
        beginResetModel();
        m_sessions = sessions;
        m_sessions.sort();
        endResetModel();
    }

    QStringList sessions() const { return m_sessions; }

private:
    // The sessions this row may name: the ones nobody has taken, and the one
    // it holds already.
    QStringList available(int row) const
    {
        QStringList names = Core::SessionManager::sessions();
        for (int i = 0; i < m_sessions.size(); ++i) {
            if (i != row)
                names.removeOne(m_sessions.at(i));
        }
        names.sort();
        return names;
    }

    QStringList m_sessions;
};

class ClangdSessionsAspect final : public BaseAspect
{
public:
    using BaseAspect::BaseAspect;

    AspectPresentation presentation() const override
    {
        AspectPresentation p = BaseAspect::presentation();
        p.control = AspectControls::Table;
        return p;
    }

    QAbstractItemModel *tableModel() override { return &m_model; }

    void reload()
    {
        m_model.setSessions(ClangdSettings::instance().m_data.sessionsWithOneClangd);
    }

    QStringList sessions() const { return m_model.sessions(); }

private:
    // Parented: a model handed to QML with no parent belongs to the engine.
    ClangdSessionsModel m_model{this};
};

// ClangdPageAspects

// What the Clangd page shows besides the settings themselves: whether the
// clangd that was named is one Qt Creator can use, which sessions share a
// clangd, and where else clangd can be configured.
//
// The settings are ClangdSettings, which is already a container - the page
// takes it in rather than copying it, because that singleton is what the rest
// of the plugin reads.
class ClangdPageAspects final : public AspectContainer
{
public:
    ClangdPageAspects()
    {
        setAutoApply(false);
        setQmlSource(QUrl("qrc:/qt/qml/QtCreator/CppEditor/ClangdSettingsPage.qml"));

        ClangdSettings &s = ClangdSettings::instance();
        s.setQmlName("Settings");
        registerAspect(&s, /*takeOwnership=*/false);

        versionWarning.setQmlName("VersionWarning");
        versionWarning.setIconType(InfoType::Warning);
        versionWarning.setVisible(false);

        sessions.setQmlName("Sessions");
        sessions.setLabelText(Tr::tr("Sessions:"));
        sessions.setToolTip(Tr::tr(
            "By default, Qt Creator runs one clangd process per project.\n"
            "If you have sessions with tightly coupled projects that should be\n"
            "managed by the same clangd process, add them here."));

        configFilesHelp.setQmlName("ConfigFilesHelp");
        configFilesHelp.setTextFormat(AspectControls::TextFormat::RichText);
        configFilesHelp.setWordWrap(true);
        configFilesHelp.setText(
            Tr::tr("Additional settings are available via "
                   "<a href=\"https://clangd.llvm.org/config\">"
                   " clangd configuration files</a>.<br>"
                   "User-specific settings go <a href=\"%1\">here</a>, "
                   "project-specific settings can be configured by putting"
                   " a .clangd file into the project source tree.")
                .arg(ClangdSettings::clangdUserConfigFilePath().toUserOutput()));
        connect(&configFilesHelp, &TextDisplay::linkActivated, this, [](const QString &link) {
            if (link.startsWith("https"))
                QDesktopServices::openUrl(link);
            else
                Core::EditorManager::openEditor(FilePath::fromString(link));
        });

        // Behaviour, not layout: whether the clangd that was named is one that
        // can be used is decided by running it.
        s.clangdPath.addOnChanged(this, [this] { updateVersionWarning(); });
        updateVersionWarning();
        sessions.reload();
    }

    void apply() override
    {
        AspectContainer::apply();
        ClangdSettings &s = ClangdSettings::instance();
        s.m_data.customDiagnosticConfigs = s.diagnosticConfigId.customConfigs();
        s.m_data.sessionsWithOneClangd = sessions.sessions();
        s.saveSettings();
        emit s.changed();
    }

    void cancel() override
    {
        AspectContainer::cancel();
        sessions.reload();
    }

    bool isDirty() const override
    {
        if (AspectContainer::isDirty())
            return true;
        QStringList stored = ClangdSettings::instance().m_data.sessionsWithOneClangd;
        stored.sort();
        return sessions.sessions() != stored;
    }

    TextDisplay versionWarning{this};
    ClangdSessionsAspect sessions{this};
    TextDisplay configFilesHelp{this};

private:
    void updateVersionWarning()
    {
        const FilePath path = ClangdSettings::instance().clangdPath();
        if (path.isEmpty()) {
            versionWarning.setVisible(false);
            return;
        }
        const Result<> res = checkClangdVersion(path);
        versionWarning.setVisible(!res);
        if (!res)
            versionWarning.setText(res.error());
    }
};

class ClangdSettingsPage final : public Core::IOptionsPage
{
public:
    ClangdSettingsPage()
    {
        setId(Constants::CPP_CLANGD_SETTINGS_ID);
        setDisplayName(Tr::tr("Clangd"));
        setCategory(Constants::CPP_SETTINGS_CATEGORY);
        setSettingsProvider([] {
            static GuardedObject<ClangdPageAspects> theAspects;
            return theAspects.get();
        });
    }
};

#ifdef WITH_TESTS

// The page's sessions lived in a QStringListModel behind a QListView, and were
// added through a modal combo box dialog that offered nothing once they were
// all taken. Whether the clangd that was named is usable was an InfoLabel the
// layout built, so neither could be read back without opening the page.

class ClangdSettingsTest : public QObject
{
    Q_OBJECT

private slots:
    // Two sessions of its own, so the tests do not depend on how many this
    // machine happens to have - with one, every claim about choosing between
    // them is true for want of anything to choose.
    void initTestCase()
    {
        for (const QString &name : m_ownSessions)
            QVERIFY(Core::SessionManager::createSession(name));
    }

    void cleanupTestCase() { Core::SessionManager::deleteSessions(m_ownSessions); }

    void cleanup()
    {
        if (ClangdPageAspects *p = page())
            static_cast<BaseAspect *>(p)->cancel();
    }

    void testTheSessionsAreTheOnesThatAreStored();
    void testASessionIsPickedFromTheOnesThatAreLeft();
    void testTheSameSessionCannotBeChosenTwice();
    void testTheWarningIsSilentUntilThereIsSomethingToWarnAbout();
    void testChangingTheSessionsIsSomethingToApply();

private:
    const QStringList m_ownSessions{"clangd-test-alpha", "clangd-test-beta"};

    // The one the page hands over, rather than one of its own: the settings
    // inside it are a singleton the rest of the plugin reads.
    static ClangdPageAspects *page()
    {
        Core::IOptionsPage *found = Utils::findOrDefault(
            Core::IOptionsPage::allOptionsPages(), [](Core::IOptionsPage *p) {
                return p->id() == Constants::CPP_CLANGD_SETTINGS_ID;
            });
        if (!found)
            return nullptr;
        const std::optional<AspectContainer *> aspects = found->aspects();
        return aspects ? static_cast<ClangdPageAspects *>(*aspects) : nullptr;
    }
};

void ClangdSettingsTest::testTheSessionsAreTheOnesThatAreStored()
{
    ClangdPageAspects *p = page();
    QVERIFY(p);
    QStringList stored = ClangdSettings::instance().m_data.sessionsWithOneClangd;
    stored.sort();
    QCOMPARE(p->sessions.sessions(), stored);
    QCOMPARE(p->sessions.tableModel()->rowCount({}), stored.size());
}

void ClangdSettingsTest::testASessionIsPickedFromTheOnesThatAreLeft()
{
    ClangdPageAspects *p = page();
    QVERIFY(p);
    QAbstractItemModel *model = p->sessions.tableModel();
    const int before = model->rowCount({});

    QStringList unused = Core::SessionManager::sessions();
    for (const QString &taken : p->sessions.sessions())
        unused.removeOne(taken);
    QVERIFY(!unused.isEmpty());
    unused.sort();

    // Adding takes one that is free, rather than leaving a blank row for the
    // user to fill in with something that is not a session at all.
    QVERIFY(model->insertRows(before, 1));
    QCOMPARE(model->rowCount({}), before + 1);
    QCOMPARE(model->index(before, 0).data().toString(), unused.first());

    // A second row takes the next one, not the same one again.
    QVERIFY(model->insertRows(before + 1, 1));
    QCOMPARE(model->index(before + 1, 0).data().toString(), unused.at(1));

    // And a row offers the free ones plus the one it holds - never one that
    // another row has taken.
    QStringList offered;
    for (const QVariant &choice :
         model->index(before, 0).data(AspectTable::ChoicesRole).toList()) {
        offered << choice.toMap().value("id").toString();
    }
    QVERIFY2(offered.contains(unused.first()), qPrintable(offered.join(", ")));
    QVERIFY2(!offered.contains(unused.at(1)), qPrintable(offered.join(", ")));

    QVERIFY(model->removeRows(before, 2));
    QCOMPARE(model->rowCount({}), before);
}

void ClangdSettingsTest::testTheSameSessionCannotBeChosenTwice()
{
    ClangdPageAspects *p = page();
    QVERIFY(p);
    QAbstractItemModel *model = p->sessions.tableModel();
    const int first = model->rowCount({});
    QVERIFY(model->insertRows(first, 1));
    QVERIFY(model->insertRows(first + 1, 1));
    const QString taken = model->index(first, 0).data().toString();
    const QString other = model->index(first + 1, 0).data().toString();
    QVERIFY(taken != other);

    // The dialog only ever offered what was free; a cell has to refuse the
    // rest itself.
    QVERIFY(!model->setData(model->index(first + 1, 0), taken));
    QCOMPARE(model->index(first + 1, 0).data().toString(), other);
}

void ClangdSettingsTest::testTheWarningIsSilentUntilThereIsSomethingToWarnAbout()
{
    ClangdPageAspects *p = page();
    QVERIFY(p);
    ClangdSettings &s = ClangdSettings::instance();

    s.clangdPath.setValue(FilePath());
    QVERIFY(!p->versionWarning.isVisible());

    // Something that is there and is not clangd: the page says so rather than
    // letting the language server fail later.
    s.clangdPath.setValue(FilePath::fromString(QCoreApplication::applicationFilePath()));
    QVERIFY(p->versionWarning.isVisible());
    QVERIFY(!p->versionWarning.text().isEmpty());

    s.clangdPath.setValue(FilePath());
    QVERIFY(!p->versionWarning.isVisible());
}

void ClangdSettingsTest::testChangingTheSessionsIsSomethingToApply()
{
    ClangdPageAspects *p = page();
    QVERIFY(p);
    QAbstractItemModel *model = p->sessions.tableModel();
    QVERIFY(!static_cast<BaseAspect *>(p)->isDirty());
    const int before = model->rowCount({});
    QVERIFY(model->insertRows(before, 1));
    // The sessions are not an aspect's value, so nothing else notices them.
    QVERIFY(static_cast<BaseAspect *>(p)->isDirty());

    QVERIFY(model->removeRows(before, 1));
    QVERIFY(!static_cast<BaseAspect *>(p)->isDirty());
}

QObject *createClangdSettingsTest()
{
    return new ClangdSettingsTest;
}

#endif // WITH_TESTS

void setupClangdSettingsPage()
{
    static ClangdSettingsPage theClangdSettingsPage;
    ClangdSettings::instance().setAutoApply(false);
}

class ClangdProjectSettingsWidget : public QWidget
{
public:
    ClangdProjectSettingsWidget(Project *project)
    {
        ClangdProjectSettings *ps = clangdProjectSettings(project);

        using namespace Layouting;
        auto settingsWidget = Column {
            *ps,
            noMargin,
        }.emerge();
        settingsWidget->setEnabled(!ps->useGlobalSettings());
        ps->useGlobalSettings.addOnChanged(settingsWidget, [ps, settingsWidget] {
            settingsWidget->setEnabled(!ps->useGlobalSettings());
        });

        Column {
            ps->useGlobalSettings,
            settingsWidget,
            noMargin,
            st,
        }.attachTo(this);
    }
};

class ClangdProjectSettingsPanelFactory final : public ProjectPanelFactory
{
public:
    ClangdProjectSettingsPanelFactory()
    {
        setPriority(100);
        setDisplayName(Tr::tr("Clangd"));
        setCreateWidgetFunction([](Project *project) {
            return new ClangdProjectSettingsWidget(project);
        });
    }
};

void setupClangdProjectSettingsPanel()
{
    static ClangdProjectSettingsPanelFactory theClangdProjectSettingsPanelFactory;
}

} // namespace Internal
} // namespace CppEditor

#include "clangdsettings.moc"
