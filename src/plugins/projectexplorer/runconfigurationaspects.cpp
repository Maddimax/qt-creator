// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "runconfigurationaspects.h"

#ifdef WITH_TESTS
#include <QTest>
#endif

#include "buildmanager.h"
#include "buildpropertiessettings.h"
#include "devicesupport/devicekitaspects.h"
#include "devicesupport/devicemanager.h"
#include "devicesupport/idevice.h"
#include "environmentaspect.h"
#include "projectexplorersettings.h"
#include "projectexplorertr.h"
#include "target.h"

#include <coreplugin/icore.h>

#include <utils/aspectwidgets.h>
#include <utils/algorithm.h>
#include <utils/fancylineedit.h>
#include <utils/hostosinfo.h>
#include <utils/layoutbuilder.h>
#include <utils/pathchooser.h>
#include <utils/qtcassert.h>
#include <utils/qtcprocess.h>
#include <utils/utilsicons.h>
#include <utils/widgets.h>

#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QFormLayout>
#include <QPushButton>

using namespace Utils;
using namespace Layouting;

namespace ProjectExplorer {

/*!
    \class ProjectExplorer::TerminalAspect
    \inmodule QtCreator

    \brief The TerminalAspect class lets a user specify that an executable
    should be run in a separate terminal.

    The initial value is provided as a hint from the build systems.
*/

TerminalAspect::TerminalAspect(AspectContainer *container)
    : BaseAspect(container)
{
    setDisplayName(Tr::tr("Terminal"));
    setId("TerminalAspect");
    setSettingsKey("RunConfiguration.UseTerminal");
    addDataExtractor(this, &TerminalAspect::useTerminal, &Data::useTerminal);
    addDataExtractor(this, &TerminalAspect::isUserSet, &Data::isUserSet);

    calculateUseTerminal();
    ProjectExplorerSettings::registerCallback(this, &ProjectExplorerSettings::terminalMode, [this] {
        calculateUseTerminal();
    });
}

AspectPresentation TerminalAspect::presentation() const
{
    AspectPresentation p = BaseAspect::presentation();
    p.control = AspectControls::CheckBox;
    p.labelText = Tr::tr("Run in terminal");
    // The label is the box's own text, with the form's label column left
    // empty - which is what the closure this replaced did by hand.
    p.labelPlacement = AspectControls::LabelPlacement::AtControl;
    return p;
}

/*!
    \reimp
*/
void TerminalAspect::fromMap(const Store &map)
{
    if (map.contains(settingsKey())) {
        m_useTerminal = map.value(settingsKey()).toBool();
        m_userSet = true;
    } else {
        m_userSet = false;
    }

    emit volatileValueChanged();
}

/*!
    \reimp
*/
void TerminalAspect::toMap(Store &data) const
{
    if (m_userSet)
        data.insert(settingsKey(), m_useTerminal);
}

void TerminalAspect::calculateUseTerminal()
{
    if (m_userSet)
        return;
    bool useTerminal;
    switch (ProjectExplorerSettings::get(container()).terminalMode()) {
    case TerminalMode::On: useTerminal = true; break;
    case TerminalMode::Off: useTerminal = false; break;
    default: useTerminal = m_useTerminalHint;
    }
    if (m_useTerminal != useTerminal) {
        m_useTerminal = useTerminal;
        emit changed();
    }
    emit volatileValueChanged();
}

/*!
    Returns whether a separate terminal should be used.
*/
bool TerminalAspect::useTerminal() const
{
    return m_useTerminal;
}

/*!
    Sets the initial value to \a hint.
*/
void TerminalAspect::setUseTerminalHint(bool hint)
{
    m_useTerminalHint = hint;
    calculateUseTerminal();
}

QVariant TerminalAspect::variantValue() const
{
    return m_useTerminal;
}

void TerminalAspect::setVariantValue(const QVariant &value, Announcement howToAnnounce)
{
    const bool useTerminal = value.toBool();
    if (m_userSet && m_useTerminal == useTerminal)
        return;
    m_userSet = true;
    m_useTerminal = useTerminal;
    emit volatileValueChanged();
    if (howToAnnounce == DoEmit)
        emit changed();
}

/*!
    Returns whether the user set the value.
*/
bool TerminalAspect::isUserSet() const
{
    return m_userSet;
}

/*!
    \class ProjectExplorer::WorkingDirectoryAspect
    \inmodule QtCreator

    \brief The WorkingDirectoryAspect class lets the user specify a
    working directory for running the executable.
*/

WorkingDirectoryAspect::WorkingDirectoryAspect(AspectContainer *container)
    : FilePathAspect(container)
{
    setDisplayName(Tr::tr("Working Directory"));
    setLabelText(Tr::tr("Working directory:"));
    setId("WorkingDirectoryAspect");
    setSettingsKey("RunConfiguration.WorkingDirectory");
    setExpectedKind(Utils::PathChooserKind::ExistingDirectory);
    setPromptDialogTitle(Tr::tr("Select Working Directory"));
    setHistoryCompleter(settingsKey());
    // What the reset goes back to is the default, which is a value the
    // descriptor already carries. The icon-only tool button that used to sit
    // beside the chooser says "Reset" in words now.
    setUseResetButton();
    setDefaultWorkingDirectory(
        FilePath::fromUserInput(buildPropertiesSettings().workingDirectoryTemplate.value()));
}

void WorkingDirectoryAspect::setEnvironment(EnvironmentAspect *envAspect)
{
    m_envAspect = envAspect;
    if (!m_envAspect)
        return;
    // Asked for when something draws, not every time it changes. This aspect
    // outlives the widgets it fills, and evaluating the environment reaches
    // into device and kit machinery, so a torn-down run settings panel used to
    // pay for an answer with nobody left to give it to.
    const Lazy<Environment> environment([this] {
        return m_envAspect ? m_envAspect->environment() : Environment();
    });
    connect(m_envAspect, &EnvironmentAspect::environmentChanged, this, [this, environment] {
        FilePathAspect::setEnvironment(environment);
    });
    FilePathAspect::setEnvironment(environment);
}

/*!
    \reimp
*/
void WorkingDirectoryAspect::fromMap(const Store &map)
{
    setDefaultPathValue(
        FilePath::fromString(map.value(settingsKey() + ".default").toString()));

    FilePath workingDir = FilePath::fromString(map.value(settingsKey()).toString());
    if (workingDir.isEmpty())
        workingDir = defaultWorkingDirectory();
    setValue(workingDir, BeQuiet);
}

/*!
    \reimp
*/
void WorkingDirectoryAspect::toMap(Store &data) const
{
    // Nothing where it is still the default, so that a default that changes -
    // a new template on the Build & Run page - reaches a configuration that
    // never overrode it.
    const FilePath workingDir = unexpandedWorkingDirectory();
    const QString wd = workingDir == defaultWorkingDirectory()
        ? QString() : workingDir.toUrlishString();
    saveToMap(data, wd, QString(), settingsKey());
    saveToMap(data, defaultWorkingDirectory().toUrlishString(), QString(),
              settingsKey() + ".default");
}

/*!
    Returns the selected directory.

    Macros in the value are expanded using \a expander.
*/
FilePath WorkingDirectoryAspect::workingDirectory() const
{
    const FilePath workingDir = expandedValue();
    if (m_envAspect)
        return m_envAspect->environment().expandVariables(workingDir);
    return workingDir.deviceEnvironment().expandVariables(workingDir);
}

FilePath WorkingDirectoryAspect::defaultWorkingDirectory() const
{
    return FilePath::fromString(defaultValue());
}

/*!
    Returns the selected directory.

    Macros in the value are not expanded.
*/
FilePath WorkingDirectoryAspect::unexpandedWorkingDirectory() const
{
    return FilePath::fromString(value());
}

/*!
    Sets the default value to \a defaultWorkingDir.
*/
void WorkingDirectoryAspect::setDefaultWorkingDirectory(const FilePath &defaultWorkingDir)
{
    const FilePath oldDefaultDir = defaultWorkingDirectory();
    if (defaultWorkingDir == oldDefaultDir)
        return;

    // A configuration that never said otherwise follows the default; one that
    // did keeps what it said. Read before setting, because setDefaultValue()
    // writes the value as well.
    const FilePath current = unexpandedWorkingDirectory();
    const bool wasFollowingTheDefault = current.isEmpty() || current == oldDefaultDir;

    setDefaultPathValue(defaultWorkingDir);
    setBaseDirectory(defaultWorkingDir);
    if (!wasFollowingTheDefault)
        setValue(current);
}

/*!
    \class ProjectExplorer::ArgumentsAspect
    \inmodule QtCreator

    \brief The ArgumentsAspect class lets a user specify command line
    arguments for an executable.
*/

ArgumentsAspect::ArgumentsAspect(AspectContainer *container)
    : AspectContainer(container)
{
    setDisplayName(Tr::tr("Arguments"));
    setLabelText(Tr::tr("Command line arguments:"));
    setId("ArgumentsAspect");
    setSettingsKey("RunConfiguration.Arguments");
    // A field and its buttons are one row.
    setInlineRow(true);

    addDataExtractor(this, &ArgumentsAspect::arguments, &Data::arguments);

    oneLine.setDisplayStyle(StringAspect::LineEditDisplay);
    oneLine.setHistoryCompleter(settingsKey());
    manyLines.setDisplayStyle(StringAspect::TextEditDisplay);
    manyLines.setVisible(false);

    for (StringAspect * const field : {&oneLine, &manyLines}) {
        field->addOnVolatileValueChanged(this, [this, field] {
            if (!m_showing)
                setArguments(field->volatileValue());
        });
    }

    expand.setActionIcon(Icons::EXPAND.icon());
    expand.setToolTip(Tr::tr("Toggle multi-line mode."));
    expand.setAction([this] {
        m_multiLine = !m_multiLine;
        showArguments();
    });

    // Only where a run configuration knows what the arguments ought to be.
    reset.setActionIcon(Icons::RESET.icon());
    reset.setToolTip(Tr::tr("Reset to Default"));
    reset.setVisible(false);
    reset.setAction([this] { resetArguments(); });

    showArguments();
}

void ArgumentsAspect::showArguments()
{
    m_showing = true;
    oneLine.setVolatileValue(m_arguments);
    manyLines.setVolatileValue(m_arguments);
    oneLine.setVisible(!m_multiLine);
    manyLines.setVisible(m_multiLine);
    expand.setActionIcon(m_multiLine ? Icons::COLLAPSE.icon() : Icons::EXPAND.icon());
    m_showing = false;
}

/*!
    Returns the main value of this aspect.

    Macros in the value are expanded using \a expander.
*/
QString ArgumentsAspect::arguments() const
{
    if (m_currentlyExpanding)
        return m_arguments;

    m_currentlyExpanding = true;
    const Result<QString> expanded = macroExpander()->expandProcessArgs(m_arguments);
    QTC_ASSERT_RESULT(expanded, return m_arguments);

    m_currentlyExpanding = false;
    return *expanded;
}

/*!
    Returns the main value of this aspect.

    Macros in the value are not expanded.
*/
QString ArgumentsAspect::unexpandedArguments() const
{
    return m_arguments;
}

/*!
    Sets the main value of this aspect to \a arguments.
*/
void ArgumentsAspect::setArguments(const QString &arguments)
{
    if (arguments != m_arguments) {
        m_arguments = arguments;
        emit changed();
    }
    if (!m_showing)
        showArguments();
}

/*!
    Adds a button to reset the main value of this aspect to the value
    computed by \a resetter.
*/
void ArgumentsAspect::setResetter(const std::function<QString()> &resetter)
{
    m_resetter = resetter;
    // No way back to a default nobody named.
    reset.setVisible(bool(m_resetter));
}

/*!
    Resets the main value of this aspect.
*/
void ArgumentsAspect::resetArguments()
{
    QString arguments;
    if (m_resetter)
        arguments = m_resetter();
    setArguments(arguments);
}

/*!
    \reimp
*/
void ArgumentsAspect::fromMap(const Store &map)
{
    QVariant args = map.value(settingsKey());
    // Until 3.7 a QStringList was stored for Remote Linux
    if (args.typeId() == QMetaType::QStringList)
        m_arguments = ProcessArgs::joinArgs(args.toStringList(), OsTypeLinux);
    else
        m_arguments = args.toString();

    m_multiLine = map.value(settingsKey() + ".multi", false).toBool();

    showArguments();
}

/*!
    \reimp
*/
void ArgumentsAspect::toMap(Store &map) const
{
    saveToMap(map, m_arguments, QString(), settingsKey());
    saveToMap(map, m_multiLine, false, settingsKey() + ".multi");
}

void ArgumentsAspect::setFocusToInputField()
{
    // Whichever one is on screen.
    (m_multiLine ? manyLines : oneLine).setFocusToInputField();
}

/*!
    \class ProjectExplorer::ExecutableAspect
    \inmodule QtCreator

    \brief The ExecutableAspect class provides a building block to provide an
    executable for a RunConfiguration.

    It combines a StringAspect that is typically updated automatically
    by the build system's parsing results with an optional manual override.
*/

ExecutableAspect::ExecutableAspect(AspectContainer *container)
    : AspectContainer(container)
{
    setDisplayName(Tr::tr("Executable"));
    setId("ExecutableAspect");
    setReadOnly(true);
    // No box around them: they are rows of whatever page lists this aspect.
    setFlattened(true);
    addDataExtractor(this, &ExecutableAspect::executable, &Data::executable);

    m_executable.setPlaceHolderText(Tr::tr("Enter the path to the executable"));
    m_executable.setLabelText(Tr::tr("Executable:"));
    // Not owned - it is a member. Registering forwards its changes and reads
    // and writes it, which this aspect used to do by hand.
    registerAspect(&m_executable);
    // A build can turn a path that did not exist into one that does.
    connect(BuildManager::instance(), &BuildManager::buildQueueFinished,
            &m_executable, &FilePathAspect::validateInput);
}

/*!
    \internal
*/

static IDevice::ConstPtr executionDevice(const Kit *k,
                                         ExecutableAspect::ExecutionDeviceSelector selector)
{
    if (k) {
        if (selector == ExecutableAspect::RunDevice)
            return RunDeviceKitAspect::device(k);
        if (selector == ExecutableAspect::BuildDevice)
            return BuildDeviceKitAspect::device(k);
    }
    return DeviceManager::defaultDesktopDevice();
}

ExecutableAspect::~ExecutableAspect() = default;

void ExecutableAspect::setDeviceSelector(Kit *kit, ExecutionDeviceSelector selector)
{
    m_kit = kit;
    m_selector = selector;

    const IDevice::ConstPtr dev = executionDevice(m_kit, m_selector);
    const OsType osType = dev ? dev->osType() : HostOsInfo::hostOs();

    m_executable.setDisplayFilter([osType](const QString &pathName) {
        return OsSpecificAspects::pathWithNativeSeparators(osType, pathName);
    });
}

/*!
   Sets the settings key for history completion to \a historyCompleterKey.

   \sa Utils::PathChooser::setHistoryCompleter()
*/
void ExecutableAspect::setHistoryCompleter(const Key &historyCompleterKey)
{
    m_executable.setHistoryCompleter(historyCompleterKey);
    if (m_alternativeExecutable)
        m_alternativeExecutable->setHistoryCompleter(historyCompleterKey);
}

/*!
   Sets the acceptable kind of path values to \a expectedKind.

   \sa Utils::PathChooser::setExpectedKind()
*/
void ExecutableAspect::setExpectedKind(const PathChooserKind expectedKind)
{
    m_executable.setExpectedKind(expectedKind);
    if (m_alternativeExecutable)
        m_alternativeExecutable->setExpectedKind(expectedKind);
}

/*!
   Sets the environment in which paths will be searched when the expected kind
   of paths is chosen as PathChooserKind::Command or PathChooserKind::ExistingCommand
   to \a env.
*/
void ExecutableAspect::setEnvironment(const Environment &env)
{
    m_executable.setEnvironment(env);
    if (m_alternativeExecutable)
        m_alternativeExecutable->setEnvironment(env);
}

void ExecutableAspect::setReadOnly(bool readOnly)
{
    BaseAspect::setReadOnly(readOnly);
    m_executable.setReadOnly(readOnly);
}

void ExecutableAspect::setFocusToInputField()
{
    m_executable.setFocusToInputField();
}

/*!
   Makes an auto-detected executable overridable by the user.

   The \a overridingKey specifies the settings key for the user-provided executable,
   the \a useOverridableKey the settings key for the fact that it
   is actually overridden the user.

   \sa Utils::StringAspect::makeCheckable()
*/
void ExecutableAspect::makeOverridable(const Key &overridingKey, const Key &useOverridableKey)
{
    QTC_ASSERT(!m_alternativeExecutable, return);
    m_alternativeExecutable = new FilePathAspect;
    m_alternativeExecutable->setLabelText(Tr::tr("Alternate executable on device:"));
    m_alternativeExecutable->setSettingsKey(overridingKey);
    m_alternativeExecutable->makeCheckable(CheckBoxPlacement::Right,
                                           Tr::tr("Use this command instead"), useOverridableKey);
    registerAspect(m_alternativeExecutable, /*takeOwnership=*/true);
}

/*!
    Returns the path of the executable specified by this aspect. In case
    the user selected a manual override this will be the value specified
    by the user.

    \sa makeOverridable()
 */
FilePath ExecutableAspect::executable() const
{
    FilePath exe = m_alternativeExecutable && m_alternativeExecutable->isChecked()
            ? (*m_alternativeExecutable)()
            : m_executable();

    if (const IDevice::ConstPtr dev = executionDevice(m_kit, m_selector))
        exe = dev->rootPath().withNewMappedPath(exe);

    return exe;
}

/*!
    Sets the label text for the main chooser to
    \a labelText.

    \sa Utils::StringAspect::setLabelText()
*/
void ExecutableAspect::setLabelText(const QString &labelText)
{
    m_executable.setLabelText(labelText);
}

/*!
    Sets the place holder text for the main chooser to
    \a placeHolderText.

    \sa Utils::StringAspect::setPlaceHolderText()
*/
void ExecutableAspect::setPlaceHolderText(const QString &placeHolderText)
{
    m_executable.setPlaceHolderText(placeHolderText);
}

/*!
    Sets the value of the main chooser to \a executable.
*/
void ExecutableAspect::setExecutable(const FilePath &executable)
{
   m_executable.setValue(executable);
   m_executable.setShowToolTipOnLabel(true);
}

/*!
    Sets the settings key to \a key.
*/
void ExecutableAspect::setSettingsKey(const Key &key)
{
    BaseAspect::setSettingsKey(key);
    m_executable.setSettingsKey(key);
}

/*!
    \class ProjectExplorer::UseLibraryPathsAspect
    \inmodule QtCreator

    \brief The UseLibraryPathsAspect class lets a user specify whether build
    library search paths should be added to the relevant environment
    variables.

    This modifies DYLD_LIBRARY_PATH and DYLD_FRAMEWORK_PATH on Mac, PATH
    on Windows and LD_LIBRARY_PATH everywhere else.
*/
UseLibraryPathsAspect::UseLibraryPathsAspect(AspectContainer *container)
    : BoolAspect(container)
{
    setId("UseLibraryPath");
    setSettingsKey("RunConfiguration.UseLibrarySearchPath");
    setOsType(HostOsInfo::hostOs());
    setValue(ProjectExplorerSettings::get(container).addLibraryPathsToRunEnv());
}

void UseLibraryPathsAspect::setOsType(OsType osType)
{
    switch (osType) {
    case OsTypeMac:
        setLabel(Tr::tr("Add build library search path to DYLD_LIBRARY_PATH and "
                        "DYLD_FRAMEWORK_PATH"), LabelPlacement::AtCheckBox);
        break;
    case OsTypeWindows:
        setLabel(Tr::tr("Add build library search path to PATH"), LabelPlacement::AtCheckBox);
        break;
    default:
        setLabel(Tr::tr("Add build library search path to LD_LIBRARY_PATH"),
                 LabelPlacement::AtCheckBox);
        break;
    }
}

/*!
    \class ProjectExplorer::UseVncDisplayAspect
    \inmodule QtCreator

    \brief The UseVncDisplayAspect class lets a user run the application
    with the Qt VNC platform plugin.

    Sets the \c QT_QPA_PLATFORM environment variable to \c vnc.
    It is mostly useful when running Qt applications on remote devices as an
    alternative to X11 forwarding.
*/
UseVncDisplayAspect::UseVncDisplayAspect(AspectContainer *container)
    : BoolAspect(container)
{
    setId("UseVncDisplay");
    setSettingsKey("RunConfiguration.UseVncDisplay");
    setLabel(Tr::tr("Use the Qt VNC platform for display"), LabelPlacement::AtCheckBox);
    setToolTip(
        Tr::tr(
            "Sets QT_QPA_PLATFORM=vnc which forwards the Qt application's UI to a built-in "
            "VNC server. Connect to the application UI with a VNC client. See the application "
            "output for details."));
}

/*!
    \class ProjectExplorer::UseDyldSuffixAspect
    \inmodule QtCreator

    \brief The UseDyldSuffixAspect class lets a user specify whether the
    DYLD_IMAGE_SUFFIX environment variable should be used on Mac.
*/

UseDyldSuffixAspect::UseDyldSuffixAspect(AspectContainer *container)
    : BoolAspect(container)
{
    setId("UseDyldSuffix");
    setSettingsKey("RunConfiguration.UseDyldImageSuffix");
    setLabel(Tr::tr("Use debug version of frameworks (DYLD_IMAGE_SUFFIX=_debug)"),
             LabelPlacement::AtCheckBox);
    setVisible(HostOsInfo::isMacHost());
}

void UseDyldSuffixAspect::applyTo(EnvironmentAspect &environment)
{
    if (!HostOsInfo::isMacHost())
        return;

    connect(this, &UseDyldSuffixAspect::changed,
            &environment, &EnvironmentAspect::environmentChanged);
    environment.addModifier([this](Environment &env) {
        if (value())
            env.set("DYLD_IMAGE_SUFFIX", "_debug");
    });
}

/*!
    \class ProjectExplorer::RunAsAspect
    \inmodule QtCreator

    \brief The RunAsAspect class lets a user specify that the
    application should run under a specific account.
*/
RunAsAspect::RunAsAspect(Utils::AspectContainer *container) : AspectContainer(container)
{
    setId("RunAs");
    setDisplayName(Tr::tr("Run as User"));
    setLabelText(Tr::tr("Run as user:"));
    // Which user, and - for "Other" - which name, read as one answer, so they
    // are one row with one label. That is what this aspect's own layout did.
    setInlineRow(true);

    m_selection.setId("RunAsSelection");
    m_selection.setSettingsKey("RunConfiguration.RunAsRoot"); // Backward compat.
    m_selection.addOption(Tr::tr("Default"));
    m_selection.addOption(Tr::tr("root"));
    m_selection.addOption(Tr::tr("Other"));
    m_selection.setDefaultValue(0);
    m_user.setId("RunAsName");
    m_user.setSettingsKey("RunConfiguration.RunAsName");
    m_user.setDisplayStyle(StringAspect::LineEditDisplay);

    updateUserNameEnabled();
    connect(&m_selection, &SelectionAspect::changed, this, &RunAsAspect::updateUserNameEnabled);

    // Not technically correct, but sensible approximation.
    // Client code with more context can override.
    setVisible(HostOsInfo::isAnyUnixHost());
}

QString RunAsAspect::user() const
{
    switch (m_selection()) {
    case 0:
        break;
    case 1:
        return "root";
    case 2:
        return m_user();
    }
    return {};
}

void RunAsAspect::setUser(const QString &user)
{
    if (user.isEmpty()) {
        m_selection.setValue(0);
    } else if (user == "root") {
        m_selection.setValue(1);
    } else {
        m_selection.setValue(2);
        m_user.setValue(user);
    }
}

void RunAsAspect::fromMap(const Utils::Store &map)
{
    AspectContainer::fromMap(map);
    updateUserNameEnabled();
}

void RunAsAspect::updateUserNameEnabled()
{
    m_user.setEnabled(m_selection.value() == 2);
}

/*!
    \class ProjectExplorer::EnableCategoriesFilterAspect
    \inmodule QtCreator

    \brief The EnableCategoriesFilterAspect class lets a user specify whether
    the application output should show the categories filtering widget.
*/

EnableCategoriesFilterAspect::EnableCategoriesFilterAspect(AspectContainer *container)
    : BoolAspect(container)
{
    setId("EnableCategoriesFilter");
    setSettingsKey("RunConfiguration.EnableCategoriesFilter");
    setLabel(Tr::tr("Enable logging category filtering"), LabelPlacement::AtCheckBox);
    setToolTip(
        Tr::tr(
            "Enables filtering for logging categories (QLoggingCategory) in the Application "
            "Output. Requires Qt 6.11 or later."));
}

Interpreter::Interpreter()
    : id(QUuid::createUuid().toString())
{}

Interpreter::Interpreter(const QString &_id,
                         const QString &_name,
                         const FilePath &_command,
                         const DetectionSource &_detectionSource)
    : id(_id)
    , name(_name)
    , command(_command)
    , detectionSource(_detectionSource)
{}

void Interpreter::fromMap(const Utils::Store &store)
{
    id = store.value("Interpreter.id").toString();
    name = store.value("Interpreter.name").toString();
    command = FilePath::fromSettings(store.value("Interpreter.command"));
    detectionSource.fromMap(store);
}

void Interpreter::toMap(Utils::Store &store) const
{
    store.insert("Interpreter.id", id);
    store.insert("Interpreter.name", name);
    store.insert("Interpreter.command", command.toSettings());
    detectionSource.toMap(store);
}

static QString launcherType2UiString(const QString &type)
{
    if (type == "test")
        return Tr::tr("Test");
    else if (type == "emulator")
        return Tr::tr("Emulator");
    return QString();
}

Launcher::Launcher(const LauncherInfo &launcherInfo, const FilePath &sourceDirectory)
    : id(launcherInfo.type)
    , arguments(launcherInfo.arguments)
{
    if (launcherInfo.type != "unused") {
        command = launcherInfo.command;
        if (command.isRelativePath())
            command = sourceDirectory.resolvePath(command);
        displayName = QString("%1 (%2)").arg(launcherType2UiString(launcherInfo.type),
                                      CommandLine(command, arguments).displayName());
    }
}

Launcher::Launcher(const LauncherInfo &testLauncherInfo, const LauncherInfo &emulatorLauncherInfo, const Utils::FilePath &sourceDirectory)
    : id(testLauncherInfo.type + " + " + emulatorLauncherInfo.type)
    , command(testLauncherInfo.command)
    , arguments(testLauncherInfo.arguments)
{
    if (command.isRelativePath())
        command = sourceDirectory.resolvePath(command);
    FilePath command1 = emulatorLauncherInfo.command;
    if (command1.isRelativePath())
        command1 = sourceDirectory.resolvePath(command1);
    arguments.append(command1.toUrlishString());
    arguments.append(emulatorLauncherInfo.arguments);
    displayName = QString("%1 + %2 (%3)").arg(launcherType2UiString(testLauncherInfo.type),
                                       launcherType2UiString(emulatorLauncherInfo.type),
                                       CommandLine(command, arguments).displayName());
}

/*!
\class ProjectExplorer::LauncherAspect
\inmodule QtCreator

\brief With the LauncherAspect class, a user can specify a launcher program for
use with executable files for which a launcher program is optionally available.
*/

LauncherAspect::LauncherAspect(AspectContainer *container)
    : BaseAspect(container)
{
    addDataExtractor(this, &LauncherAspect::currentLauncher, &Data::launcher);
}

Launcher LauncherAspect::currentLauncher() const
{
    return Utils::findOrDefault(m_launchers, Utils::equal(&Launcher::id, m_currentId));
}

void LauncherAspect::updateLaunchers(const QList<Launcher> &launchers)
{
    if (m_launchers == launchers)
        return;
    m_launchers = launchers;
    // The entries are the descriptor's, so a new list is a new descriptor.
    emit controlConfigurationChanged();
    if (m_currentId.isEmpty() || !Utils::anyOf(m_launchers,
                                               Utils::equal(&Launcher::id, m_currentId))) {
        setCurrentLauncherId(m_defaultId);
    }
}

void LauncherAspect::setDefaultLauncher(const Launcher &launcher)
{
    if (m_defaultId == launcher.id)
        return;
    m_defaultId = launcher.id;
    if (m_currentId.isEmpty())
        setCurrentLauncher(launcher);
}

void LauncherAspect::setCurrentLauncher(const Launcher &launcher)
{
    setCurrentLauncherId(launcher.id);
}

void LauncherAspect::setVariantValue(const QVariant &value, Announcement howToAnnounce)
{
    const QString id = value.toString();
    if (id == m_currentId)
        return;
    m_currentId = id;
    emit volatileValueChanged();
    if (howToAnnounce == DoEmit)
        emit changed();
}

void LauncherAspect::fromMap(const Store &map)
{
    setCurrentLauncherId(map.value(settingsKey(), m_defaultId).toString());
}

void LauncherAspect::toMap(Store &map) const
{
    if (m_currentId != m_defaultId)
        saveToMap(map, m_currentId, QString(), settingsKey());
}

AspectPresentation LauncherAspect::presentation() const
{
    AspectPresentation p = BaseAspect::presentation();
    p.control = AspectControls::ComboBox;
    p.labelText = Tr::tr("Launcher:");
    // The value is the launcher's id, not its place in the list: the list is
    // refilled whenever the device's launchers change.
    p.valueIsChoiceId = true;
    for (const Launcher &launcher : m_launchers) {
        p.choices.append(
            {launcher.displayName, launcher.command.toUserOutput(), true, launcher.id});
    }
    return p;
}

void LauncherAspect::setCurrentLauncherId(const QString &id)
{
    setVariantValue(id);
}

/*!
    \class ProjectExplorer::X11ForwardingAspect
    \inmodule QtCreator

    \brief The X11ForwardingAspect class lets a user specify a display
     for a remotely running X11 client.
*/

X11ForwardingAspect::X11ForwardingAspect(AspectContainer *container)
    : StringAspect(container)
{
    setDisplayStyle(LineEditDisplay);
    setId("X11ForwardingAspect");
    setSettingsKey("RunConfiguration.X11Forwarding");
    makeCheckable(
        CheckBoxPlacement::Left, Tr::tr("Use X11 forwarding:"), "RunConfiguration.UseX11Forwarding");
    setPlaceHolderText(hostX11Display());
    setLabelText("DISPLAY=");

    addDataExtractor(this, &X11ForwardingAspect::display, &Data::display);
}

QString X11ForwardingAspect::display() const
{
    if (!isChecked())
        return {};

    // Follow the current session unless the user pinned a display.
    const QString display = value().isEmpty() ? hostX11Display() : value();
    return macroExpander()->expand(display);
}


/*!
    \class ProjectExplorer::SymbolFileAspect
    \inmodule QtCreator

    \brief The SymbolFileAspect class lets a user specify a symbol file
     for debugging.
*/


SymbolFileAspect::SymbolFileAspect(AspectContainer *container)
    : FilePathAspect(container)
{}

MainScriptAspect::MainScriptAspect(AspectContainer *container)
    : FilePathAspect(container)
{}

#ifdef WITH_TESTS
class WorkingDirectoryAspectTest final : public QObject
{
    Q_OBJECT

private slots:
    void testItFollowsTheDefaultUntilSomethingElseIsSaid()
    {
        // The aspect is a path with a reset button now, so the two values it
        // used to keep by hand - the directory and the default - are the
        // aspect's value and its default value.
        WorkingDirectoryAspect wd;
        const FilePath first = FilePath::fromUserInput("/first");
        const FilePath second = FilePath::fromUserInput("/second");

        wd.setDefaultWorkingDirectory(first);
        QCOMPARE(wd.defaultWorkingDirectory(), first);
        // Nothing was said, so it is the default.
        QCOMPARE(wd.unexpandedWorkingDirectory(), first);

        // A new default reaches a configuration that never overrode it - which
        // is what changing the template on the Build & Run page has to do.
        wd.setDefaultWorkingDirectory(second);
        QCOMPARE(wd.unexpandedWorkingDirectory(), second);

        // Once something else is said, the default no longer moves it.
        const FilePath chosen = FilePath::fromUserInput("/chosen");
        wd.setValue(chosen);
        wd.setDefaultWorkingDirectory(first);
        QCOMPARE(wd.unexpandedWorkingDirectory(), chosen);
        QCOMPARE(wd.defaultWorkingDirectory(), first);
    }

    void testTheResetGoesBackToTheDefault()
    {
        // The reset used to be an icon-only tool button this aspect built and
        // wired itself. It is the descriptor's now, so the button and what it
        // goes back to are one statement.
        WorkingDirectoryAspect wd;
        wd.setDefaultWorkingDirectory(FilePath::fromUserInput("/default"));
        wd.setValue(FilePath::fromUserInput("/elsewhere"));

        const AspectPresentation p = wd.presentation();
        QVERIFY(p.withResetButton);
        QCOMPARE(FilePath::fromString(p.defaultValue.toString()),
                 FilePath::fromUserInput("/default"));
        QCOMPARE(p.pathKind, AspectControls::PathKind::ExistingDirectory);
    }

    void testWhatIsStoredIsWhatWasOverridden()
    {
        // A directory that is still the default is stored as nothing, so that
        // a later change to the default is picked up rather than frozen in.
        WorkingDirectoryAspect wd;
        wd.setSettingsKey("RunConfiguration.WorkingDirectory");
        wd.setDefaultWorkingDirectory(FilePath::fromUserInput("/default"));

        Store store;
        static_cast<BaseAspect &>(wd).toMap(store);
        QCOMPARE(store.value("RunConfiguration.WorkingDirectory").toString(), QString());
        QCOMPARE(store.value("RunConfiguration.WorkingDirectory.default").toString(),
                 QString("/default"));

        wd.setValue(FilePath::fromUserInput("/elsewhere"));
        store.clear();
        static_cast<BaseAspect &>(wd).toMap(store);
        QCOMPARE(store.value("RunConfiguration.WorkingDirectory").toString(),
                 QString("/elsewhere"));

        // And it comes back as what was stored.
        WorkingDirectoryAspect restored;
        restored.setSettingsKey("RunConfiguration.WorkingDirectory");
        static_cast<BaseAspect &>(restored).fromMap(store);
        QCOMPARE(restored.unexpandedWorkingDirectory(), FilePath::fromUserInput("/elsewhere"));
        QCOMPARE(restored.defaultWorkingDirectory(), FilePath::fromUserInput("/default"));

        // One that stored nothing comes back on the default.
        Store defaulted;
        defaulted.insert("RunConfiguration.WorkingDirectory.default", "/default");
        WorkingDirectoryAspect untouched;
        untouched.setSettingsKey("RunConfiguration.WorkingDirectory");
        static_cast<BaseAspect &>(untouched).fromMap(defaulted);
        QCOMPARE(untouched.unexpandedWorkingDirectory(), FilePath::fromUserInput("/default"));
    }
};

class ArgumentsAspectTest final : public QObject
{
    Q_OBJECT

private slots:
    void testBothFieldsHoldTheArgumentsAndOnlyOneIsShown()
    {
        // The row swapped one editor for the other in place, deleting the one
        // it swapped out. Both exist now and one is hidden, which is the same
        // thing to look at and a great deal less to go wrong.
        ArgumentsAspect args;
        args.setArguments("-v --input file.txt");

        QVERIFY(args.oneLine.isVisible());
        QVERIFY(!args.manyLines.isVisible());
        QCOMPARE(args.oneLine.volatileValue(), args.unexpandedArguments());
        QCOMPARE(args.manyLines.volatileValue(), args.unexpandedArguments());

        // Expanding shows the other one. What is typed does not move, because
        // it was never only in the one on screen.
        args.expand.triggerAction();
        QVERIFY(!args.oneLine.isVisible());
        QVERIFY(args.manyLines.isVisible());
        QCOMPARE(args.manyLines.volatileValue(), QString("-v --input file.txt"));

        // And typing in whichever is shown is what the aspect holds.
        args.manyLines.setVolatileValue("-q");
        QCOMPARE(args.unexpandedArguments(), QString("-q"));
        QCOMPARE(args.oneLine.volatileValue(), QString("-q"));

        args.expand.triggerAction();
        QVERIFY(args.oneLine.isVisible());
    }

    void testTheWayBackIsOnlyOfferedWhereThereIsOne()
    {
        // Most run configurations cannot say what the arguments ought to be,
        // and a button that resets to nothing in particular is worse than no
        // button.
        ArgumentsAspect args;
        QVERIFY(!args.reset.isVisible());

        args.setResetter([] { return QString("--from-the-project"); });
        QVERIFY(args.reset.isVisible());

        args.setArguments("-v");
        args.reset.triggerAction();
        QCOMPARE(args.unexpandedArguments(), QString("--from-the-project"));
        QCOMPARE(args.oneLine.volatileValue(), QString("--from-the-project"));
    }
};

QObject *createArgumentsAspectTest()
{
    return new ArgumentsAspectTest;
}

QObject *createWorkingDirectoryAspectTest()
{
    return new WorkingDirectoryAspectTest;
}
#endif // WITH_TESTS

} // namespace ProjectExplorer

#include "runconfigurationaspects.moc"
