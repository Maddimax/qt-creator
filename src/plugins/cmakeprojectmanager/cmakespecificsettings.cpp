// Copyright (C) 2018 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "cmakeproject.h"
#include "cmakespecificsettings.h"

#include "cmakeprojectconstants.h"
#include "cmakeprojectmanagertr.h"

#include <coreplugin/dialogs/ioptionspage.h>
#include <coreplugin/icore.h>

#include <projectexplorer/project.h>
#include <projectexplorer/projectimporter.h>
#include <projectexplorer/projectpanelfactory.h>

#include <utils/hostosinfo.h>
#include <utils/layoutbuilder.h>
#include <utils/qtcassert.h>

#include <QGuiApplication>

#ifdef WITH_TESTS
#include <coreplugin/dialogs/ioptionspage.h>
#include <utils/algorithm.h>

#include <QTest>
#endif

using namespace ProjectExplorer;
using namespace Utils;

namespace CMakeProjectManager::Internal {

CMakeSpecificSettings &cmakeSettingsForProject(Project *project)
{
    static CMakeSpecificSettings theSettings(nullptr, false);
    if (!project)
        return theSettings;

    CMakeProject *cmakeProject = qobject_cast<CMakeProject *>(project);
    if (!cmakeProject || cmakeProject->settings().useGlobalSettings())
        return theSettings;

    return cmakeProject->settings();
}

QVariant NinjaPathAspect::fromSettingsValue(const QVariant &savedValue) const
{
   // Sometimes the installer appends the same ninja path to the qtcreator.ini file
   const QString path = savedValue.canConvert<QStringList>()
           ? savedValue.toStringList().last() : savedValue.toString();
   return FilePath::fromUserInput(path).toVariant();
}

QVariant NinjaPathAspect::toSettingsValue(const QVariant &valueToSave) const
{
    // never save this to the settings:
    Q_UNUSED(valueToSave);
    return QVariant::fromValue(QString());
}

CMakeSpecificSettings::CMakeSpecificSettings(Project *p, bool autoApply)
    : project(p)
{
    useGlobalSettings.setSettingsPageId(Constants::Settings::GENERAL_ID);

    setQmlSource(QUrl("qrc:/qt/qml/QtCreator/CMakeProjectManager/CMakeSpecificSettingsPage.qml"));

    // TODO: fixup of QTCREATORBUG-26289 , remove in Qt Creator 7 or so
    Core::ICore::settings()->remove("CMakeSpecificSettings/NinjaPath");

    setSettingsGroup(Constants::Settings::GENERAL_ID);
    setAutoApply(autoApply);

    autorunCMake.setSettingsKey("AutorunCMake");
    autorunCMake.setDefaultValue(true);
    autorunCMake.setLabelText(::CMakeProjectManager::Tr::tr("Autorun CMake"));
    autorunCMake.setToolTip(::CMakeProjectManager::Tr::tr(
        "Automatically run CMake after changes to CMake project files."));

    ninjaPath.setSettingsKey("NinjaPath");

    configureDetailsExpanded.setSettingsKey("ConfigureDetailsExpanded");
    configureDetailsExpanded.setDefaultValue(true);

    packageManagerAutoSetup.setSettingsKey("PackageManagerAutoSetup");
    packageManagerAutoSetup.setDefaultValue(true);
    packageManagerAutoSetup.setLabelText(::CMakeProjectManager::Tr::tr("Package manager auto setup"));
    packageManagerAutoSetup.setToolTip(
        //: %1 = applicationDisplayName
        ::CMakeProjectManager::Tr::tr(
            "Enables %1 to install dependencies from the conanfile.txt, "
            "conanfile.py, or vcpkg.json file from the project source directory.")
            .arg(QGuiApplication::applicationDisplayName()));

    maintenanceToolDependencyProvider.setSettingsKey("MaintenanceToolDependencyProvider");
    maintenanceToolDependencyProvider.setDefaultValue(true);
    maintenanceToolDependencyProvider.setLabelText(
        ::CMakeProjectManager::Tr::tr("Qt Online Installer dependency provider"));
    maintenanceToolDependencyProvider.setToolTip(
        ::CMakeProjectManager::Tr::tr("Use Qt Online Installer to install missing Qt components."));

    askBeforeReConfigureInitialParams.setSettingsKey("AskReConfigureInitialParams");
    askBeforeReConfigureInitialParams.setDefaultValue(true);
    askBeforeReConfigureInitialParams.setLabelText(::CMakeProjectManager::Tr::tr("Ask before re-configuring with "
        "initial parameters"));

    askBeforePresetsReload.setSettingsKey("AskBeforePresetsReload");
    askBeforePresetsReload.setDefaultValue(true);
    askBeforePresetsReload.setLabelText(::CMakeProjectManager::Tr::tr("Ask before reloading CMake Presets"));

    askBeforeApplyingConfigurationChanges.setSettingsKey("AskApplyConfigurationChanges");
    askBeforeApplyingConfigurationChanges.setDefaultValue(true);

    showSourceSubFolders.setSettingsKey("ShowSourceSubFolders");
    showSourceSubFolders.setDefaultValue(true);
    showSourceSubFolders.setLabelText(
                ::CMakeProjectManager::Tr::tr("Show subfolders inside source group folders"));

    showAdvancedOptionsByDefault.setSettingsKey("ShowAdvancedOptionsByDefault");
    showAdvancedOptionsByDefault.setDefaultValue(false);
    showAdvancedOptionsByDefault.setLabelText(
                ::CMakeProjectManager::Tr::tr("Show advanced options by default"));

    useJunctionsForSourceAndBuildDirectories.setSettingsKey(
        "UseJunctionsForSourceAndBuildDirectories");
    useJunctionsForSourceAndBuildDirectories.setDefaultValue(false);
    useJunctionsForSourceAndBuildDirectories.setLabelText(::CMakeProjectManager::Tr::tr(
        "Use junctions for CMake configuration and build operations"));
    useJunctionsForSourceAndBuildDirectories.setVisible(HostOsInfo::isWindowsHost());
    useJunctionsForSourceAndBuildDirectories.setToolTip(::CMakeProjectManager::Tr::tr(
        "Create and use junctions for the source and build directories to overcome "
        "issues with long paths on Windows.<br><br>"
        "Junctions are stored under <tt>C:\\ProgramData\\QtCreator\\Links</tt> (overridable via "
        "the <tt>QTC_CMAKE_JUNCTIONS_DIR</tt> environment variable).<br><br>"
        "With <tt>QTC_CMAKE_JUNCTIONS_HASH_LENGTH</tt>, you can shorten the MD5 hash key length "
        "to a value smaller than the default length value of 32.<br><br>"
        "Junctions are used for CMake configure, build and install operations."));

    cleanOldOutput.setSettingsKey("CleanOldOutput");
    cleanOldOutput.setDefaultValue(true);
    cleanOldOutput.setLabelText(
        ::CMakeProjectManager::Tr::tr("Clear old CMake output on a new run"));

    readSettings();

    if (project) {
        setEnabled(!useGlobalSettings());

        useGlobalSettings.addOnChanged(this, [this] {
            setEnabled(!useGlobalSettings());
            writeSettings();
        });
        addOnChanged(this, [this] {
            if (!useGlobalSettings())
                writeSettings();
        });

        // Re-read the settings. Reading in constructor is too early
        connect(project, &Project::settingsLoaded, this, [this] { readSettings(); });
    }
}

void CMakeSpecificSettings::readSettings()
{
    if (!project) {
        AspectContainer::readSettings();
    } else {
        Store data = storeFromVariant(project->namedSettings(Constants::Settings::GENERAL_ID));
        if (data.isEmpty()) {
            CMakeProject *cmakeProject = qobject_cast<CMakeProject *>(project);
            if (cmakeProject && cmakeProject->presetsData().havePresets
                && cmakeProject->presetsData().vendor) {
                useGlobalSettings.setValue(false);
                data = storeFromMap(*cmakeProject->presetsData().vendor);
                fromMap(data);

                // Write the new loaded CMakePresets settings into .user file
                writeSettings();
            } else {
                useGlobalSettings.setValue(true);
                AspectContainer::readSettings();
            }
        } else {
            useGlobalSettings.setValue(data.value(Constants::Settings::USE_GLOBAL_SETTINGS, true).toBool());
            fromMap(data);
        }
    }
}

void CMakeSpecificSettings::writeSettings() const
{
    if (!project) {
        AspectContainer::writeSettings();
    } else {
        Store data;
        toMap(data);
        data.insert(Constants::Settings::USE_GLOBAL_SETTINGS, useGlobalSettings());
        project->setNamedSettings(Constants::Settings::GENERAL_ID, variantFromStore(data));
    }
}

class CMakeSpecificSettingsPage final : public Core::IOptionsPage
{
public:
    CMakeSpecificSettingsPage()
    {
        setId(Constants::Settings::GENERAL_ID);
        setDisplayName(::CMakeProjectManager::Tr::tr("General"));
        setCategory(Constants::Settings::CATEGORY);
        setSettingsProvider([] { return &cmakeSettingsForProject(nullptr); });
    }
};

const CMakeSpecificSettingsPage settingsPage;

// What the panel shows. The flag is not one of the settings - see the comment
// on its declaration - so the panel is a container of its own holding both.
// It has no settings key, and neither does the flag, so what is stored stays
// the settings container's business.
class CMakeProjectPanel final : public AspectContainer
{
public:
    explicit CMakeProjectPanel(CMakeSpecificSettings *settings)
    {
        // Before registering anything: insertAspect() forces the container's
        // own auto-apply onto what it takes in.
        setAutoApply(true);
        setQmlSource(QUrl("qrc:/qt/qml/QtCreator/CMakeProjectManager/CMakeProjectPanel.qml"));

        settings->useGlobalSettings.setQmlName("UseGlobalSettings");
        registerAspect(&settings->useGlobalSettings);

        settings->setQmlName("Settings");
        registerAspect(settings);
    }
};

static CMakeProjectPanel *cmakeProjectPanel(Project *project)
{
    const Key key = "CMakeProjectPanel";
    QVariant v = project->extraData(key);
    if (v.isNull()) {
        v = QVariant::fromValue(new CMakeProjectPanel(&cmakeSettingsForProject(project)));
        project->setExtraData(key, v);
    }
    return v.value<CMakeProjectPanel *>();
}

class CMakeProjectSettingsPanelFactory final : public ProjectPanelFactory
{
public:
    CMakeProjectSettingsPanelFactory()
    {
        setPriority(120);
        setDisplayName("CMake");
        setSupportsFunction([](Project *project) {
            return qobject_cast<CMakeProject *>(project) != nullptr;
        });
        setSettingsProvider([](Project *project) { return cmakeProjectPanel(project); });
    }
};

const CMakeProjectSettingsPanelFactory projectSettingsPane;

#ifdef WITH_TESTS

// Every QML warning raised while the panel is built. A wrong aspect name is not
// a load error - the form still instantiates - it is a warning saying the
// binding could not be resolved, and the control is simply missing.
static QStringList s_qmlComplaints;

static void collectComplaints(QtMsgType, const QMessageLogContext &, const QString &message)
{
    s_qmlComplaints.append(message);
}

// A project's CMake panel is not an options page, so the page census never sees
// it - which is why it is asserted here.
class CMakeProjectPanelTest final : public QObject
{
    Q_OBJECT

private slots:
    void testThePanelDrawsTheFlagAndTheSettings()
    {
        CMakeProjectPanel panel(&cmakeSettingsForProject(nullptr));

        s_qmlComplaints.clear();
        QtMessageHandler previous = qInstallMessageHandler(collectComplaints);
        const std::unique_ptr<QWidget> form(Core::createAspectForm(&panel));
        qInstallMessageHandler(previous);

        QVERIFY2(form, "the panel produced no form at all");

        QObject *quickWidget = nullptr;
        const QList<QObject *> children = form->findChildren<QObject *>();
        for (QObject * const child : children) {
            if (QLatin1String(child->metaObject()->className()) == QLatin1String("QQuickWidget"))
                quickWidget = child;
        }
        QVERIFY2(quickWidget, "the panel produced a widget form, so the Quick one was declined");

        const int ready = 1; // QQuickWidget::Ready
        QCOMPARE(quickWidget->property("status").toInt(), ready);

        // Loaded is not enough: a form naming an aspect that is not there loads
        // perfectly well and leaves the control out. Every name it reaches the
        // settings by is a chance to get that wrong, and this is what says so.
        const QStringList aboutThisPanel
            = Utils::filtered(s_qmlComplaints, [](const QString &complaint) {
                  return complaint.contains("CMakeProjectPanel.qml");
              });
        QVERIFY2(aboutThisPanel.isEmpty(), qPrintable(aboutThisPanel.join("; ")));
    }
};

QObject *createCMakeProjectPanelTest()
{
    return new CMakeProjectPanelTest;
}

#endif // WITH_TESTS

} // CMakeProjectManager::Internal

#ifdef WITH_TESTS
#include "cmakespecificsettings.moc"
#endif
