// Copyright (C) 2019 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "pythonsettings.h"

#include "pythonconstants.h"
#include "pythonkitaspect.h"
#include "pythontr.h"
#include "pythonutils.h"

#include <coreplugin/dialogs/ioptionspage.h>
#include <coreplugin/icore.h>
#include <coreplugin/messagemanager.h>
#include <coreplugin/progressmanager/processprogress.h>

#include <debugger/debuggerkitaspect.h>

#include <projectexplorer/kitaspect.h>
#include <projectexplorer/environmentkitaspect.h>
#include <projectexplorer/kitmanager.h>

#include <extensionsystem/pluginmanager.h>

#include <languageclient/languageclient_global.h>
#include <languageclient/languageclientsettings.h>
#include <languageclient/languageclientmanager.h>

#include <texteditor/textdocument.h>
#include <texteditor/texteditor.h>

#include <utils/algorithm.h>

#ifdef WITH_TESTS
#include <QTest>
#endif
#include <utils/async.h>
#include <utils/environment.h>
#include <utils/guiutils.h>
#include <utils/layoutbuilder.h>
#include <utils/listmodel.h>
#include <utils/pathchooser.h>
#include <utils/qtcprocess.h>
#include <utils/qtcassert.h>
#include <utils/treemodel.h>
#include <utils/utilsicons.h>
#include <utils/shutdownguard.h>

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFormLayout>
#include <QGroupBox>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLoggingCategory>
#include <QPointer>
#include <QPushButton>
#include <QScopeGuard>
#include <QSettings>
#include <QStackedWidget>
#include <QTreeView>
#include <QVBoxLayout>
#include <QWidget>

using namespace ProjectExplorer;
using namespace QtTaskTree;
using namespace Utils;

namespace Python::Internal {

// This is defacto the unapplied state of PythonSettings::defaultInterpreter().id()
static QString s_defaultId;

static Q_LOGGING_CATEGORY(pylspLog, "qtc.python.pylsp", QtWarningMsg)

InterpreterModel::InterpreterModel(const std::function<bool(QString)> &isDefaultId)
{
    setDataAccessor([isDefaultId](const Interpreter &interpreter, int column, int role) -> QVariant {
        if (interpreter.id == "none") {
            if (role == Qt::DisplayRole)
                return Tr::tr("None", "No Python interpreter");
            if (role == KitAspect::IsNoneRole)
                return true;
            return {};
        }
        switch (role) {
        case Qt::DisplayRole:
            return interpreter.name;
        case Qt::FontRole: {
            QFont f;
            f.setBold(isDefaultId && isDefaultId(interpreter.id));
            return f;
        }
        case Qt::ToolTipRole:
            if (!interpreter.command.isLocal())
                break;
            if (interpreter.command.isEmpty())
                return Tr::tr("Executable is empty.");
            if (!interpreter.command.exists())
                return Tr::tr("\"%1\" does not exist.").arg(interpreter.command.toUserOutput());
            if (!interpreter.command.isExecutableFile())
                return Tr::tr("\"%1\" is not an executable file.")
                    .arg(interpreter.command.toUserOutput());
            break;
        case Qt::DecorationRole:
            if (column == 0 && !PythonSettings::interpreterIsValid(interpreter))
                return Utils::Icons::CRITICAL.icon();
            break;
        case KitAspect::IdRole:
            return interpreter.id;
        case KitAspect::QualityRole:
            return int(PythonSettings::interpreterIsValid(interpreter));
        default:
            break;
        }
        return {};
    });
    setAllData(PythonSettings::interpreters());
}

// This is defacto the unapplied state of PythonSettings::interpreters()
InterpreterModel &interpreterModel()
{
    static InterpreterModel theInterpreterModel([](const QString &id) { return id == s_defaultId; });
    return theInterpreterModel;
}

Interpreter PythonSettings::createInterpreter(
    const FilePath &python,
    const QString &defaultName,
    const QString &suffix,
    const DetectionSource &detectionSource)
{
    Interpreter result;
    result.id = QUuid::createUuid().toString();
    result.command = python;
    result.detectionSource = detectionSource;

    result.name = pythonVersion(python);
    if (result.name.isEmpty())
        result.name = defaultName;
    QDir pythonDir(python.parentDir().toUrlishString());
    if (pythonDir.exists() && pythonDir.exists("activate") && pythonDir.cdUp())
        result.name += QString(" (%1)").arg(pythonDir.dirName());
    if (!suffix.isEmpty())
        result.name += ' ' + suffix;

    return result;
}

// The interpreters, handed to whichever renderer is drawing the page. At file
// scope because moc does not do signals in a nested class.
class InterpreterListAspect final : public BaseAspect
{
    Q_OBJECT

public:
    using BaseAspect::BaseAspect;

    AspectPresentation presentation() const override
    {
        AspectPresentation p = BaseAspect::presentation();
        p.control = AspectControls::Table;
        // Adding one takes a name and a path, which the form below asks for;
        // the list's own Add would give a blank row.
        p.allowAdding = false;
        p.allowRemoving = false;
        return p;
    }

    QAbstractItemModel *tableModel() override { return &interpreterModel(); }

    // Which one the details below are about. The view says so; the page reads
    // it. -1 when nothing is picked.
    Q_INVOKABLE void setCurrentRow(int row)
    {
        if (row == m_currentRow)
            return;
        const int previous = m_currentRow;
        m_currentRow = row;
        emit currentRowChanged(previous, row);
    }

    int currentRow() const { return m_currentRow; }

signals:
    void currentRowChanged(int previous, int current);

private:
    int m_currentRow = -1;
};

// What the Interpreters page edits. The interpreters live in
// interpreterModel(), which the rest of the plugin reads; applying the page is
// handing that list to PythonSettings.
class InterpretersAspects final : public AspectContainer
{
public:
    InterpretersAspects()
    {
        setAutoApply(false);
        setQmlSource(QUrl("qrc:/qt/qml/QtCreator/Python/PythonInterpretersPage.qml"));

        interpreters.setQmlName("Interpreters");

        add.setQmlName("Add");
        add.setActionText(Tr::tr("&Add"));
        add.setAction([this] {
            interpreterModel().appendItem({QUuid::createUuid().toString(), QString("Python"),
                                           FilePath(), DetectionSource::Manual});
            interpreters.setCurrentRow(interpreterModel().rowCount({}) - 1);
            updateActions();
        });

        remove.setQmlName("Remove");
        remove.setActionText(Tr::tr("&Delete"));
        remove.setAction([this] {
            const int row = interpreters.currentRow();
            if (row < 0)
                return;
            interpreterModel().destroyItem(interpreterModel().itemAt(row));
            interpreters.setCurrentRow(-1);
            updateActions();
        });

        makeDefault.setQmlName("MakeDefault");
        makeDefault.setActionText(Tr::tr("&Make Default"));
        makeDefault.setAction([this] {
            const int row = interpreters.currentRow();
            if (row < 0)
                return;
            const QModelIndex was = interpreterModel().findIndex(
                [](const Interpreter &i) { return i.id == s_defaultId; });
            s_defaultId = interpreterModel().itemAt(row)->itemData.id;
            const QModelIndex now = interpreterModel().index(row, 0);
            emit interpreterModel().dataChanged(now, now, {Qt::FontRole});
            if (was.isValid())
                emit interpreterModel().dataChanged(was, was, {Qt::FontRole});
            updateActions();
        });

        generateKit.setQmlName("GenerateKit");
        generateKit.setActionText(Tr::tr("&Generate Kit"));
        generateKit.setAction([this] {
            const int row = interpreters.currentRow();
            if (row >= 0)
                PythonSettings::addKitsForInterpreter(interpreterModel().itemAt(row)->itemData, true);
            updateActions();
        });

        cleanUp.setQmlName("CleanUp");
        cleanUp.setActionText(Tr::tr("&Clean Up"));
        cleanUp.setToolTip(Tr::tr("Remove all Python interpreters without a valid executable."));
        cleanUp.setAction([this] {
            interpreterModel().destroyItems(
                [](const Interpreter &i) { return !i.command.isExecutableFile(); });
            interpreters.setCurrentRow(-1);
            updateActions();
        });

        details.setQmlName("Details");
        name.setQmlName("Name");
        name.setLabelText(Tr::tr("Name:"));
        name.setDisplayStyle(StringAspect::LineEditDisplay);
        executable.setQmlName("Executable");
        executable.setLabelText(Tr::tr("Executable:"));
        executable.setExpectedKind(PathChooserKind::ExistingCommand);
        executable.setAllowPathFromDevice(true);

        // Behaviour, not layout.
        connect(&interpreters, &InterpreterListAspect::currentRowChanged,
                this, [this](int previous, int current) { showInterpreter(previous, current); });
        name.addOnVolatileValueChanged(this, [this] { store(); });
        executable.addOnVolatileValueChanged(this, [this] { store(); });
        showInterpreter(-1, -1);
    }

    void apply() override
    {
        AspectContainer::apply();
        store();
        PythonSettings::setInterpreter(interpreterModel().interpreters(), s_defaultId);
    }

    void cancel() override
    {
        AspectContainer::cancel();
        interpreterModel().setInterpreters(PythonSettings::interpreters());
        s_defaultId = PythonSettings::defaultInterpreterId();
        interpreters.setCurrentRow(-1);
    }

    InterpreterListAspect interpreters{this};
    ActionAspect add{this};
    ActionAspect remove{this};
    ActionAspect makeDefault{this};
    ActionAspect generateKit{this};
    ActionAspect cleanUp{this};
    AspectContainer details{this};
    StringAspect name{&details};
    FilePathAspect executable{&details};

private:
    // What was on screen has already been written back - every field stores as
    // it changes - so there is nothing to save on the way out.
    void showInterpreter(int previous, int current)
    {
        Q_UNUSED(previous)
        const bool has = current >= 0 && current < interpreterModel().rowCount({});
        details.setVisible(has);
        remove.setEnabled(has);
        makeDefault.setEnabled(has);
        // Nothing is current while the form is being filled in.
        m_loaded = -1;
        if (has) {
            const Interpreter interpreter = interpreterModel().itemAt(current)->itemData;
            name.setValue(interpreter.name);
            executable.setValue(interpreter.command);
        }
        m_loaded = has ? current : -1;
        updateActions();
    }

    void store()
    {
        if (m_loaded < 0)
            return;
        writeInto(m_loaded);
        updateActions();
    }

    void writeInto(int row)
    {
        if (row < 0 || row >= interpreterModel().rowCount({}))
            return;
        Interpreter interpreter = interpreterModel().itemAt(row)->itemData;
        interpreter.name = name.volatileValue();
        interpreter.command = executable.expandedVolatileValue();
        interpreterModel().itemAt(row)->itemData = interpreter;
        const QModelIndex index = interpreterModel().index(row, 0);
        emit interpreterModel().dataChanged(index, index);
    }

    void updateActions()
    {
        cleanUp.setEnabled(Utils::anyOf(interpreterModel().allData(), [](const Interpreter &i) {
            return !i.command.isExecutableFile();
        }));
        const int row = interpreters.currentRow();
        if (row < 0 || row >= interpreterModel().rowCount({})) {
            generateKit.setEnabled(false);
            return;
        }
        // One kit per interpreter, and only for one that can be run.
        const Interpreter interpreter = interpreterModel().itemAt(row)->itemData;
        generateKit.setEnabled(!KitManager::kit(Id::fromString(interpreter.id))
                               && (!interpreter.command.isLocal()
                                   || interpreter.command.isExecutableFile()));
    }

    // The interpreter the form is showing, and nothing while it is being
    // filled in.
    int m_loaded = -1;
};

QVariant InterpreterModel::data(const QModelIndex &index, int role) const
{
    if (role == AspectTable::EditableRole)
        return false;
    return ListModel<Interpreter>::data(index, role);
}

QHash<int, QByteArray> InterpreterModel::roleNames() const
{
    return AspectTable::withRoleNames(ListModel<Interpreter>::roleNames());
}

void InterpreterModel::addInterpreter(const Interpreter &interpreter)
{
    appendItem(interpreter);
}

void InterpreterModel::removeInterpreterFrom(const QString &detectionSource)
{
    destroyItems([&detectionSource](const Interpreter &interpreter) {
        return interpreter.detectionSource.id == detectionSource;
    });
}

void InterpreterModel::setInterpreters(const QList<Interpreter> &interpreters)
{
    clear();
    for (const Interpreter &interpreter : interpreters)
        appendItem(interpreter);
}

QList<Interpreter> InterpreterModel::interpreters() const
{
    QList<Interpreter> interpreters;
    for (const TreeItem *treeItem : *this)
        interpreters << static_cast<const ListItem<Interpreter> *>(treeItem)->itemData;
    return interpreters;
}

QList<Interpreter> InterpreterModel::interpreterFrom(const QString &detectionSource) const
{
    return allData([&detectionSource](const Interpreter &interpreter) {
        return interpreter.detectionSource.id == detectionSource;
    });
}

static const QStringList &plugins()
{
    static const QStringList plugins{"flake8",
                                     "jedi_completion",
                                     "jedi_definition",
                                     "jedi_hover",
                                     "jedi_references",
                                     "jedi_signature_help",
                                     "jedi_symbols",
                                     "mccabe",
                                     "pycodestyle",
                                     "pydocstyle",
                                     "pyflakes",
                                     "pylint",
                                     "yapf"};
    return plugins;
}

// The Python language server's configuration. The JSON is what is stored; the
// plugin check boxes are a reading of it, and writing one rewrites the JSON -
// which is why "neither" is a state they can be in, for a plugin the JSON says
// nothing about.
class PyLSAspects : public AspectContainer
{
public:
    PyLSAspects()
    {
        setAutoApply(false);
        setQmlSource(QUrl("qrc:/qt/qml/QtCreator/Python/PyLSSettingsPage.qml"));

        m_enabled.setQmlName("Enabled");
        m_enabled.setLabelText(Tr::tr("Use Python Language Server"));

        m_plugins.setQmlName("Plugins");
        for (const QString &plugin : plugins()) {
            auto aspect = new TriStateAspect(&m_plugins);
            aspect->setUseCheckBox(true);
            aspect->setLabelText(plugin);
            aspect->setQmlName(plugin);
            aspect->addOnVolatileValueChanged(this, [this, plugin, aspect] {
                // The volatile value: a page that is not applied yet has the
                // user's answer there, and value() still has the old one.
                if (!m_reading)
                    writePluginIntoConfiguration(
                        plugin, TriState::fromInt(aspect->volatileValue()));
            });
            m_pluginAspects.insert(plugin, aspect);
        }

        m_advanced.setQmlName("Advanced");
        m_advanced.setLabelText(Tr::tr("Advanced"));

        m_documentation.setQmlName("Documentation");
        m_documentation.setTextFormat(AspectControls::TextFormat::MarkdownText);
        m_documentation.setText(
            Tr::tr("For a complete list of available options, consult the "
                   "[Python LSP Server configuration documentation](%1).")
                .arg("https://github.com/python-lsp/python-lsp-server/blob/"
                     "develop/CONFIGURATION.md"));

        m_configuration.setQmlName("Configuration");
        m_configuration.setDisplayStyle(StringAspect::TextEditDisplay);

        m_error.setQmlName("Error");
        m_error.setIconType(InfoType::Error);

        // Behaviour, not layout.
        m_configuration.addOnVolatileValueChanged(this, [this] {
            readPluginsFromConfiguration();
            checkConfiguration();
        });
        m_advanced.addOnVolatileValueChanged(this, [this] { updateVisibility(); });

        m_enabled.setValue(PythonSettings::pylsEnabled());
        m_configuration.setValue(PythonSettings::pylsConfiguration());
        m_advanced.setValue(false);
        readPluginsFromConfiguration();
        checkConfiguration();
        updateVisibility();
    }

    void apply() override
    {
        AspectContainer::apply();
        PythonSettings::setPylsEnabled(m_enabled.volatileValue());
        PythonSettings::setPyLSConfiguration(m_configuration.volatileValue());
    }

private:
    void updateVisibility()
    {
        const bool advanced = m_advanced.volatileValue();
        m_plugins.setVisible(!advanced);
        m_documentation.setVisible(advanced);
        m_configuration.setVisible(advanced);
        m_error.setVisible(advanced && !m_error.text().isEmpty());
    }

    // What the JSON says about each plugin. Nothing at all is a state of its
    // own: the server's own default applies, and saying "off" instead would be
    // a decision the user never made.
    void readPluginsFromConfiguration()
    {
        const QJsonDocument document = QJsonDocument::fromJson(
            m_configuration.volatileValue().toUtf8());
        if (!document.isObject())
            return;
        const QJsonObject pluginsObject
            = document.object()["pylsp"].toObject()["plugins"].toObject();
        // Reading the JSON into the boxes is not the user ticking them, and a
        // box that writes back would rewrite the JSON under the cursor.
        m_reading = true;
        const QScopeGuard done([this] { m_reading = false; });
        for (auto it = m_pluginAspects.cbegin(); it != m_pluginAspects.cend(); ++it) {
            const QJsonValue enabled = pluginsObject[it.key()].toObject()["enabled"];
            const TriState state = !enabled.isBool()
                                       ? TriState::Default
                                       : (enabled.toBool(false) ? TriState::Enabled
                                                                : TriState::Disabled);
            it.value()->setValue(state);
        }
    }

    void writePluginIntoConfiguration(const QString &plugin, TriState state)
    {
        if (state == TriState::Default)
            return;
        QJsonDocument document = QJsonDocument::fromJson(
            m_configuration.volatileValue().toUtf8());
        QJsonObject config;
        if (!document.isNull())
            config = document.object();
        QJsonObject pylsp = config["pylsp"].toObject();
        QJsonObject pluginsObject = pylsp["plugins"].toObject();
        QJsonObject pluginValue = pluginsObject[plugin].toObject();
        pluginValue.insert("enabled", state == TriState::Enabled);
        pluginsObject.insert(plugin, pluginValue);
        pylsp.insert("plugins", pluginsObject);
        config.insert("pylsp", pylsp);
        document.setObject(config);
        m_configuration.setValue(QString::fromUtf8(document.toJson()));
    }

    // What the widget editor put on the line as a text mark. A settings page
    // has one place to say it, so it says it there.
    void checkConfiguration()
    {
        const QString content = m_configuration.volatileValue().trimmed();
        QString message;
        if (!content.isEmpty()) {
            QJsonParseError error;
            QJsonDocument::fromJson(content.toUtf8(), &error);
            if (error.error != QJsonParseError::NoError)
                message = Tr::tr("JSON Error: %1").arg(error.errorString());
        }
        m_error.setText(message);
        m_error.setVisible(m_advanced.volatileValue() && !message.isEmpty());
    }

    QMap<QString, TriStateAspect *> m_pluginAspects;
    bool m_reading = false;

public:
    TriStateAspect *pluginAspect(const QString &name) const
    {
        return m_pluginAspects.value(name);
    }

    BoolAspect m_enabled{this};
    AspectContainer m_plugins{this};
    BoolAspect m_advanced{this};
    TextDisplay m_documentation{this};
    StringAspect m_configuration{this};
    TextDisplay m_error{this};
};


class PyLSOptionsPage : public Core::IOptionsPage
{
public:
    PyLSOptionsPage()
    {
        setId(Constants::C_PYLSCONFIGURATION_PAGE_ID);
        setDisplayName(Tr::tr("Language Server Configuration"));
        setCategory(Constants::C_PYTHON_SETTINGS_CATEGORY);
        setSettingsProvider([] {
            static GuardedObject<PyLSAspects> theAspects;
            return theAspects.get();
        });
    }
};

static PyLSOptionsPage &pylspOptionsPage()
{
    static PyLSOptionsPage page;
    return page;
}

constexpr char settingsGroupKey[] = "Python";
constexpr char interpreterKey[] = "Interpeter";
constexpr char defaultKey[] = "DefaultInterpeter";
constexpr char kitsGeneratedKey[] = "KitsGenerated";
constexpr char pylsEnabledKey[] = "PylsEnabled";
constexpr char pylsConfigurationKey[] = "PylsConfiguration";

static QString defaultPylsConfiguration()
{
    static QJsonObject configuration;
    if (configuration.isEmpty()) {
        QJsonObject enabled;
        enabled.insert("enabled", true);
        QJsonObject disabled;
        disabled.insert("enabled", false);
        QJsonObject plugins;
        plugins.insert("flake8", disabled);
        plugins.insert("jedi_completion", enabled);
        plugins.insert("jedi_definition", enabled);
        plugins.insert("jedi_hover", enabled);
        plugins.insert("jedi_references", enabled);
        plugins.insert("jedi_signature_help", enabled);
        plugins.insert("jedi_symbols", enabled);
        plugins.insert("mccabe", disabled);
        plugins.insert("pycodestyle", disabled);
        plugins.insert("pydocstyle", disabled);
        plugins.insert("pyflakes", enabled);
        plugins.insert("pylint", disabled);
        plugins.insert("yapf", enabled);
        QJsonObject pylsp;
        pylsp.insert("plugins", plugins);
        configuration.insert("pylsp", pylsp);
    }
    return QString::fromUtf8(QJsonDocument(configuration).toJson());
}

void PythonSettings::disableOutdatedPylsNow()
{
    using namespace LanguageClient;
    const QList<BaseSettings *>
            settings = LanguageClientSettings::pageSettings();
    for (const BaseSettings *setting : settings) {
        if (setting->settingsTypeId() != LanguageClient::Constants::LANGUAGECLIENT_STDIO_SETTINGS_ID)
            continue;
        auto stdioSetting = static_cast<const StdIOSettings *>(setting);
        if (stdioSetting->arguments().startsWith("-m pyls")
                && stdioSetting->languageFilter().isSupported("foo.py", Constants::C_PY_MIMETYPE)) {
            LanguageClientManager::enableClientSettings(stdioSetting->id(), false);
        }
    }
}

void PythonSettings::disableOutdatedPyls()
{
    using namespace ExtensionSystem;
    if (PluginManager::isInitializationDone()) {
        disableOutdatedPylsNow();
    } else {
        QObject::connect(PluginManager::instance(), &PluginManager::initializationDone,
                         this, &PythonSettings::disableOutdatedPylsNow);
    }
}

static void pythonsFromRegistry(QPromise<QList<Interpreter>> &promise)
{
    QList<Interpreter> pythons;
    QSettings pythonRegistry("HKEY_LOCAL_MACHINE\\SOFTWARE\\Python\\PythonCore",
                             QSettings::NativeFormat);
    for (const QString &versionGroup : pythonRegistry.childGroups()) {
        if (promise.isCanceled())
            return;

        pythonRegistry.beginGroup(versionGroup);
        QString name = pythonRegistry.value("DisplayName").toString();
        QVariant regVal = pythonRegistry.value("InstallPath/ExecutablePath");
        if (regVal.isValid()) {
            const FilePath &executable = FilePath::fromUserInput(regVal.toString());
            if (executable.exists()) {
                pythons << Interpreter{QUuid::createUuid().toString(),
                                       name,
                                       FilePath::fromUserInput(regVal.toString())};
            }
        }
        regVal = pythonRegistry.value("InstallPath/.");
        if (regVal.isValid()) {
            const FilePath &path = FilePath::fromUserInput(regVal.toString());
            const FilePath python = path.pathAppended("python").withExecutableSuffix();
            if (python.exists())
                pythons << PythonSettings::createInterpreter(python, "Python " + versionGroup);
        }
        pythonRegistry.endGroup();
    }
    promise.addResult(pythons);
}

static void pythonsFromPath(QPromise<QList<Interpreter>> &promise)
{
    QList<Interpreter> pythons;
    if (HostOsInfo::isWindowsHost()) {
        for (const FilePath &executable : FilePath("python").searchAllInPath()) {
            if (promise.isCanceled())
                return;

            // Windows creates empty redirector files that may interfere
            if (executable.toFileInfo().size() == 0)
                continue;
            if (executable.exists())
                pythons << PythonSettings::createInterpreter(executable, "Python from Path");
        }
    } else {
        const QStringList filters = {"python",
                                     "python[1-9].[0-9]",
                                     "python[1-9].[1-9][0-9]",
                                     "python[1-9]"};
        const FilePaths dirs = Environment::systemEnvironment().path();
        QSet<FilePath> used;
        for (const FilePath &path : dirs) {
            const QDir dir(path.toUrlishString());
            for (const QFileInfo &fi : dir.entryInfoList(filters)) {
                if (promise.isCanceled())
                    return;

                const FilePath executable = FilePath::fromUserInput(fi.canonicalFilePath());
                if (!used.contains(executable) && executable.exists()) {
                    used.insert(executable);
                    pythons << PythonSettings::createInterpreter(executable, "Python from Path");
                }
            }
        }
    }
    promise.addResult(pythons);
}

static QString idForPythonFromPath(const QList<Interpreter> &pythons)
{
    FilePath pythonFromPath = FilePath("python3").searchInPath();
    if (pythonFromPath.isEmpty())
        pythonFromPath = FilePath("python").searchInPath();
    if (pythonFromPath.isEmpty())
        return {};
    const Interpreter &defaultInterpreter
        = findOrDefault(pythons, [pythonFromPath](const Interpreter &interpreter) {
              return interpreter.command == pythonFromPath;
          });
    return defaultInterpreter.id;
}

static PythonSettings *settingsInstance = nullptr;

static bool alreadyRegistered(const Interpreter &candidate)
{
    return Utils::anyOf(settingsInstance->interpreters(),
                        [candidate = candidate.command](const Interpreter &interpreter) {
                            return interpreter.command.isSameDevice(candidate)
                                   && interpreter.command.resolveSymlinks()
                                          == candidate.resolveSymlinks();
                        });
}

std::optional<QStringList> activePythonVersions(const QList<Interpreter> &interpreters)
{
    QStringList versions;
    for (const Interpreter &interpreter : interpreters) {
        if (!interpreter.command.isLocal() || !interpreter.command.isExecutableFile())
            continue;
        const QString version = pythonVersion(interpreter.command);
        // The interpreter is there, we just cannot read its version right now.
        if (version.isEmpty()) {
            qCDebug(pylspLog) << "Not pruning, no version for" << interpreter.command;
            return std::nullopt;
        }
        versions << version;
    }
    return versions;
}

void prunePylspInstallations(const FilePath &pylspRoot, const QStringList &keepVersions)
{
    if (!pylspRoot.isDir())
        return;

    const FilePaths versionDirs
        = pylspRoot.dirEntries(DirFilterFlag::Dirs | DirFilterFlag::NoDotAndDotDot);
    for (const FilePath &dir : versionDirs) {
        if (keepVersions.contains(dir.fileName()))
            continue;
        qCDebug(pylspLog) << "Removing stale pylsp installation" << dir;
        const Result<> removed = dir.removeRecursively();
        QTC_CHECK_RESULT(removed);
    }
}

static void pruneStalePylspInstallations(const QList<Interpreter> &interpreters)
{
    if (const std::optional<QStringList> versions = activePythonVersions(interpreters))
        prunePylspInstallations(localPylspRoot(), *versions);
}

PythonSettings::PythonSettings()
{
    QTC_ASSERT(!settingsInstance, return);
    settingsInstance = this;

    setObjectName("PythonSettings");
    ExtensionSystem::PluginManager::addObject(this);

    initFromSettings(Core::ICore::settings());

    const auto onRegistrySetup = [](Async<QList<Interpreter>> &task) {
        task.setConcurrentCallData(pythonsFromRegistry);
    };
    const auto onPathSetup = [](Async<QList<Interpreter>> &task) {
        task.setConcurrentCallData(pythonsFromPath);
    };
    const auto onTaskDone = [](const Async<QList<Interpreter>> &task) {
        if (!task.isResultAvailable())
            return;

        const auto interpreters = task.result();
        for (const Interpreter &interpreter : interpreters) {
            if (!alreadyRegistered(interpreter))
                settingsInstance->addInterpreter(interpreter);
        }
    };
    const auto onPruneSetup = [](Async<void> &task) {
        task.setConcurrentCallData(pruneStalePylspInstallations, settingsInstance->m_interpreters);
    };

    // The group is sequential, so the prune sees the settled interpreter list.
    const Group recipe {
        finishAllAndSuccess,
        Utils::HostOsInfo::isWindowsHost()
            ? AsyncTask<QList<Interpreter>>(onRegistrySetup, onTaskDone) : nullItem,
        AsyncTask<QList<Interpreter>>(onPathSetup, onTaskDone),
        AsyncTask<void>(onPruneSetup)
    };
    m_taskTreeRunner.start(recipe);

    if (m_defaultInterpreterId.isEmpty())
        m_defaultInterpreterId = idForPythonFromPath(m_interpreters);

    s_defaultId = m_defaultInterpreterId;

    writeToSettings(Core::ICore::settings());

    pylspOptionsPage();
}

PythonSettings::~PythonSettings()
{
    ExtensionSystem::PluginManager::removeObject(this);
    settingsInstance = nullptr;
}

static void setRelevantAspectsToKit(Kit *k)
{
    QTC_ASSERT(k, return);
    QSet<Utils::Id> relevantAspects = k->relevantAspects();
    relevantAspects.unite(
        {PythonKitAspect::id(), EnvironmentKitAspect::id(), Debugger::DebuggerKitAspect::id()});
    k->setRelevantAspects(relevantAspects);
}

void PythonSettings::addKitsForInterpreter(const Interpreter &interpreter, bool force)
{
    if (!KitManager::isLoaded()) {
        connect(KitManager::instance(),
                &KitManager::kitsLoaded,
                settingsInstance,
                [interpreter, force]() { addKitsForInterpreter(interpreter, force); });
        return;
    }

    const Id kitId = Id::fromString(interpreter.id);
    if (Kit *k = KitManager::kit(kitId)) {
        setRelevantAspectsToKit(k);
    } else if (force || !isVenvPython(interpreter.command)) {
        KitManager::registerKit(
            [interpreter](Kit *k) {
                if (interpreter.detectionSource.id.isEmpty())
                    k->setDetectionSource({DetectionSource::FromSystem, "Python"});
                else
                    k->setDetectionSource(interpreter.detectionSource);

                k->setUnexpandedDisplayName("%{Python:Name}");
                setRelevantAspectsToKit(k);
                PythonKitAspect::setPython(k, interpreter.id);
                k->setSticky(PythonKitAspect::id(), true);
            },
            kitId);
    }
}

void PythonSettings::removeKitsForInterpreter(const Interpreter &interpreter)
{
    if (!KitManager::isLoaded()) {
        connect(KitManager::instance(), &KitManager::kitsLoaded, settingsInstance, [interpreter]() {
            removeKitsForInterpreter(interpreter);
        });
        return;
    }

    if (Kit *k = KitManager::kit(Id::fromString(interpreter.id)))
        KitManager::deregisterKit(k);
}

bool PythonSettings::interpreterIsValid(const Interpreter &interpreter)
{
    return !interpreter.command.isLocal() || interpreter.command.isExecutableFile();
}

void PythonSettings::setInterpreter(const QList<Interpreter> &interpreters, const QString &defaultId)
{
    if (defaultId == settingsInstance->m_defaultInterpreterId
        && interpreters == settingsInstance->m_interpreters) {
        return;
    }
    QList<Interpreter> toRemove = settingsInstance->m_interpreters;
    for (const Interpreter &interpreter : interpreters) {
        if (!Utils::eraseOne(toRemove, Utils::equal(&Interpreter::id, interpreter.id)))
            addKitsForInterpreter(interpreter, false);
    }
    for (const Interpreter &interpreter : std::as_const(toRemove))
        removeKitsForInterpreter(interpreter);
    settingsInstance->m_interpreters = interpreters;
    settingsInstance->m_defaultInterpreterId = defaultId;
    saveSettings();
}

void PythonSettings::setPyLSConfiguration(const QString &configuration)
{
    if (configuration == settingsInstance->m_pylsConfiguration)
        return;
    settingsInstance->m_pylsConfiguration = configuration;
    saveSettings();
    emit instance()->pylsConfigurationChanged(configuration);
}

void PythonSettings::setPylsEnabled(const bool &enabled)
{
    if (enabled == settingsInstance->m_pylsEnabled)
        return;
    settingsInstance->m_pylsEnabled = enabled;
    saveSettings();
    emit instance()->pylsEnabledChanged(enabled);
}

bool PythonSettings::pylsEnabled()
{
    return settingsInstance->m_pylsEnabled;
}

QString PythonSettings::pylsConfiguration()
{
    return settingsInstance->m_pylsConfiguration;
}

void PythonSettings::addInterpreter(const Interpreter &interpreter, bool isDefault)
{
    if (Utils::anyOf(settingsInstance->m_interpreters, Utils::equal(&Interpreter::id, interpreter.id)))
        return;
    settingsInstance->m_interpreters.append(interpreter);
    if (isDefault)
        settingsInstance->m_defaultInterpreterId = interpreter.id;
    saveSettings();
    addKitsForInterpreter(interpreter, false);
}

Interpreter PythonSettings::addInterpreter(const FilePath &interpreterPath,
                                           bool isDefault,
                                           const QString &nameSuffix)
{
    const Interpreter interpreter = createInterpreter(interpreterPath, {}, nameSuffix);
    addInterpreter(interpreter, isDefault);
    markSettingsDirty();
    return interpreter;
}

PythonSettings *PythonSettings::instance()
{
    QTC_CHECK(settingsInstance);
    return settingsInstance;
}

void PythonSettings::createVirtualEnvironmentInteractive(
    const FilePath &startDirectory,
    const Interpreter &defaultInterpreter,
    const std::function<void(const FilePath &)> &callback)
{
    QDialog dialog;
    dialog.setModal(true);
    auto layout = new QFormLayout(&dialog);
    auto interpreters = new QComboBox;
    const QString preselectedId = defaultInterpreter.id.isEmpty()
                                      ? PythonSettings::defaultInterpreter().id
                                      : defaultInterpreter.id;
    for (const Interpreter &interpreter : PythonSettings::interpreters()) {
        interpreters->addItem(interpreter.name, interpreter.id);
        if (!preselectedId.isEmpty() && interpreter.id == preselectedId)
            interpreters->setCurrentIndex(interpreters->count() - 1);
    }
    layout->addRow(Tr::tr("Python interpreter:"), interpreters);
    auto pathChooser = new PathChooser();
    pathChooser->setInitialBrowsePathBackup(startDirectory);
    pathChooser->setExpectedKind(PathChooserKind::Directory);
    pathChooser->setPromptDialogTitle(Tr::tr("New Python Virtual Environment Directory"));
    layout->addRow(Tr::tr("Virtual environment directory:"), pathChooser);
    auto buttons = new QDialogButtonBox(QDialogButtonBox::Cancel);
    auto createButton = buttons->addButton(Tr::tr("Create"), QDialogButtonBox::AcceptRole);
    createButton->setEnabled(false);
    connect(pathChooser,
            &PathChooser::validChanged,
            createButton,
            [createButton](bool valid) { createButton->setEnabled(valid); });
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addRow(buttons);
    dialog.setLayout(layout);
    if (dialog.exec() == QDialog::Rejected) {
        callback({});
        return;
    }

    const Interpreter interpreter = PythonSettings::interpreter(
        interpreters->currentData().toString());

    auto venvDir = pathChooser->filePath();
    createVirtualEnvironment(interpreter.command, venvDir, callback);
}

static QSet<FilePath> &venvsCurrentlySettingUp()
{
    static QSet<FilePath> venvs;
    return venvs;
}

bool PythonSettings::isRunningVirtualEnvironmentSetup(const FilePath &python)
{
    return Utils::anyOf(venvsCurrentlySettingUp(), [python](const FilePath &venv) {
        return python.isChildOf(venv);
    });
}

void PythonSettings::createVirtualEnvironment(
    const FilePath &python,
    const FilePath &directory,
    const std::function<void(const FilePath &)> &callback)
{
    QTC_ASSERT(python.isExecutableFile(), return);
    QTC_ASSERT(!directory.exists() || directory.isDir(), return);

    const CommandLine command(python, QStringList{"-m", "venv", directory.toUserOutput()});
    venvsCurrentlySettingUp().insert(directory);

    auto process = new Process;
    auto progress = new Core::ProcessProgress(process);
    progress->setDisplayName(Tr::tr("Create Python venv"));
    QObject::connect(process, &Process::done, [directory, process, callback](){
        venvsCurrentlySettingUp().remove(directory);
        switch (process->result()) {
        case ProcessResult::FinishedWithSuccess: {
            FilePath venvPython = directory.osType() == Utils::OsTypeWindows ? directory / "Scripts"
                                                                             : directory / "bin";
            venvPython = venvPython.pathAppended("python").withExecutableSuffix();
            if (venvPython.exists()) {
                if (callback)
                    callback(venvPython);
                emit instance()->virtualEnvironmentCreated(venvPython);
            }
            break;
        }
        case ProcessResult::FinishedWithError:
            Core::MessageManager::writeFlashing(
                Tr::tr("Venv creation failed:\n%1").arg(process->allOutput()));
            break;
        case ProcessResult::TerminatedAbnormally:
            Core::MessageManager::writeFlashing(
                Tr::tr("Venv creation terminated abnormally:\n%1").arg(process->errorString()));
            break;
        case ProcessResult::StartFailed:
            Core::MessageManager::writeFlashing(
                Tr::tr("Venv creation could not be started:\n%1").arg(process->allOutput()));
            break;
        case ProcessResult::Canceled:
            Core::MessageManager::writeFlashing(Tr::tr("Venv creation canceled."));
            break;
        }

        process->deleteLater();
    });
    process->setCommand(command);
    process->start();
}

QList<Interpreter> PythonSettings::detectPythonVenvs(const FilePath &path)
{
    QList<Interpreter> result;
    QDir dir = path.toFileInfo().isDir() ? QDir(path.toUrlishString()) : path.toFileInfo().dir();
    if (dir.exists()) {
        const QString venvPython = HostOsInfo::withExecutableSuffix("python");
        const QString activatePath = HostOsInfo::isWindowsHost() ? QString{"Scripts"}
                                                                 : QString{"bin"};
        do {
            for (const QString &directory : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
                if (dir.cd(directory)) {
                    if (dir.cd(activatePath)) {
                        if (dir.exists("activate") && dir.exists(venvPython)) {
                            FilePath python = FilePath::fromString(dir.absoluteFilePath(venvPython));
                            dir.cdUp();
                            const QString defaultName = QString("Python (%1 Virtual Environment)")
                                                            .arg(dir.dirName());
                            Interpreter interpreter
                                = Utils::findOrDefault(PythonSettings::interpreters(),
                                                       Utils::equal(&Interpreter::command, python));
                            if (interpreter.command.isEmpty()) {
                                interpreter = createInterpreter(python, defaultName);
                                PythonSettings::addInterpreter(interpreter);
                            }
                            result << interpreter;
                        } else {
                            dir.cdUp();
                        }
                    }
                    dir.cdUp();
                }
            }
        } while (dir.cdUp() && !(dir.isRoot() && Utils::HostOsInfo::isAnyUnixHost()));
    }
    return result;
}

void PythonSettings::initFromSettings(QtcSettings *settings)
{
    settings->beginGroup(settingsGroupKey);
    const QVariantList interpreterList = settings->value(interpreterKey).toList();
    for (const QVariant &interpreterVariant : interpreterList) {
        if (isStore(interpreterVariant)) {
            Interpreter interpreter;
            interpreter.fromMap(Utils::storeFromVariant(interpreterVariant));
            Utils::erase(m_interpreters, Utils::equal(&Interpreter::id, interpreter.id));
            m_interpreters << interpreter;
            continue;
        }

        auto interpreterMembers = interpreterVariant.toList();
        if (interpreterMembers.size() <= 3)
            continue; // old settings, skip

        const auto id = interpreterMembers.value(0).toString();
        if (Utils::contains(m_interpreters, Utils::equal(&Interpreter::id, id)))
            continue; // already exists

        const bool isAutoDetected = interpreterMembers.value(3, true).toBool();
        const QString detectionSourceId = interpreterMembers.value(4, QString()).toString();
        DetectionSource detectionSource{
            isAutoDetected ? DetectionSource::FromSystem : DetectionSource::Manual,
            detectionSourceId};

        m_interpreters << Interpreter{
            id,
            interpreterMembers.value(1).toString(),
            FilePath::fromSettings(interpreterMembers.value(2)),
            detectionSource};
    }

    const auto keepInterpreter = [](const Interpreter &interpreter) {
        return !interpreter.detectionSource.isAutoDetected() // always keep user added interpreters
               || !interpreter.command.isLocal() // remote devices might not be reachable at startup
               || interpreter.command.isExecutableFile();
    };

    const auto [valid, outdatedInterpreters] = Utils::partition(m_interpreters, keepInterpreter);
    m_interpreters = valid;

    const bool kitsGenerated = settings->value(kitsGeneratedKey, false).toBool();
    if (kitsGenerated)
        fixupPythonKits();
    for (const Interpreter &interpreter : std::as_const(m_interpreters)) {
        if (!kitsGenerated) {
            if (interpreter.detectionSource.isAutoDetected()) {
                const FilePath &cmd = interpreter.command;
                if (!cmd.isLocal() || cmd.parentDir().pathAppended("activate").exists())
                    continue;
            }
            addKitsForInterpreter(interpreter, false);
        }
    }

    for (const Interpreter &outdated : outdatedInterpreters)
        removeKitsForInterpreter(outdated);

    m_defaultInterpreterId = settings->value(defaultKey).toString();

    QVariant pylsEnabled = settings->value(pylsEnabledKey);
    if (pylsEnabled.isNull())
        disableOutdatedPyls();
    else
        m_pylsEnabled = pylsEnabled.toBool();
    const QVariant pylsConfiguration = settings->value(pylsConfigurationKey);
    if (!pylsConfiguration.isNull())
        m_pylsConfiguration = pylsConfiguration.toString();
    else
        m_pylsConfiguration = defaultPylsConfiguration();
    settings->endGroup();
}

void PythonSettings::writeToSettings(QtcSettings *settings)
{
    settings->beginGroup(settingsGroupKey);
    QVariantList interpretersList;
    for (const Interpreter &interpreter : std::as_const(m_interpreters)) {
        if (interpreter.detectionSource.isTemporary())
            continue;

        QVariantList members{
            interpreter.id,
            interpreter.name,
            interpreter.command.toSettings(),
            interpreter.detectionSource.isAutoDetected()};
        // We need to cast to QVariant() here, otherwise interpretersList will simply append each
        // member as a separate item.
        interpretersList.append(QVariant(members)); // old settings
        Store newSettings;
        interpreter.toMap(newSettings);
        interpretersList.append(variantFromStore(newSettings)); // new settings
    }
    settings->setValue(interpreterKey, interpretersList);
    settings->setValue(defaultKey, m_defaultInterpreterId);

    settings->setValueWithDefault(pylsConfigurationKey,
                                  m_pylsConfiguration,
                                  defaultPylsConfiguration());

    settings->setValue(pylsEnabledKey, m_pylsEnabled);
    settings->setValue(kitsGeneratedKey, true);
    settings->endGroup();
}

QString PythonSettings::defaultInterpreterId()
{
    return settingsInstance->m_defaultInterpreterId;
}

std::optional<ExecutableItem> PythonSettings::autoDetect(
    Kit *kit,
    const Utils::FilePaths &searchPaths,
    const DetectionSource &detectionSource,
    const LogCallback &logCallback)
{
    Q_UNUSED(kit);

    const auto setupSearch = [searchPaths, detectionSource](Async<Interpreter> &task) {
        const QList<Interpreter> alreadyConfigured = PythonSettings::interpreters();

        task.setConcurrentCallData(
            [](QPromise<Interpreter> &promise,
               const FilePaths &searchPaths,
               const QList<Interpreter> &alreadyConfigured,
               const DetectionSource &detectionSource) {
                for (const FilePath &path : searchPaths) {
                    const FilePath python = path.pathAppended("python3").withExecutableSuffix();
                    if (!python.isExecutableFile())
                        continue;
                    if (Utils::contains(
                            alreadyConfigured, Utils::equal(&Interpreter::command, python)))
                        continue;

                    Interpreter interpreter = PythonSettings::createInterpreter(
                        python, {}, "(" + python.toUserOutput() + ")", detectionSource);

                    promise.addResult(interpreter);
                }
            },
            searchPaths,
            alreadyConfigured,
            detectionSource);
    };

    const auto searchDone = [detectionSource, logCallback](const Async<Interpreter> &task) {
        for (const auto &interpreter : task.results()) {
            interpreterModel().addInterpreter(interpreter);
            logCallback(
                Tr::tr("Found \"%1\" (%2).")
                    .arg(interpreter.name, interpreter.command.toUserOutput()));
        }
    };

    return AsyncTask<Interpreter>(setupSearch, searchDone);
}

void PythonSettings::removeDetectedPython(
    const QString &detectionSource, const LogCallback &logCallback)
{
    for (Interpreter &interpreter : interpreterModel().interpreterFrom(detectionSource))
        logCallback(Tr::tr("Removing Python: %1.").arg(interpreter.name));

    interpreterModel().removeInterpreterFrom(detectionSource);
}

void PythonSettings::listDetectedPython(
    const QString &detectionSource, const LogCallback &logCallback)
{
    for (Interpreter &interpreter: interpreterModel().interpreterFrom(detectionSource))
        logCallback(Tr::tr("Python: %1.").arg(interpreter.name));
}

void PythonSettings::fixupPythonKits()
{
    if (!KitManager::isLoaded()) {
        connect(KitManager::instance(),
                &KitManager::kitsLoaded,
                settingsInstance,
                &PythonSettings::fixupPythonKits,
                Qt::UniqueConnection);
        return;
    }
    for (const Interpreter &interpreter : std::as_const(m_interpreters)) {
        if (auto k = KitManager::kit(Id::fromString(interpreter.id)))
            setRelevantAspectsToKit(k);
    }
}

void PythonSettings::saveSettings()
{
    QTC_ASSERT(settingsInstance, return);
    settingsInstance->writeToSettings(Core::ICore::settings());
    emit settingsInstance->interpretersChanged(settingsInstance->m_interpreters,
                                               settingsInstance->m_defaultInterpreterId);
}

QList<Interpreter> PythonSettings::interpreters()
{
    return settingsInstance->m_interpreters;
}

Interpreter PythonSettings::defaultInterpreter()
{
    return interpreter(settingsInstance->m_defaultInterpreterId);
}

Interpreter PythonSettings::interpreter(const QString &interpreterId)
{
    return Utils::findOrDefault(settingsInstance->m_interpreters,
                                Utils::equal(&Interpreter::id, interpreterId));
}

#ifdef WITH_TESTS

// The interpreters lived in a QTreeView's selection and a details widget that
// was shown and hidden, so which one was being edited could only be read back
// out of widgets.

class PythonInterpretersTest : public QObject
{
    Q_OBJECT

private slots:
    void init() { m_original = PythonSettings::interpreters(); }
    void cleanup()
    {
        if (InterpretersAspects *p = page())
            static_cast<BaseAspect *>(p)->cancel();
        PythonSettings::setInterpreter(m_original, PythonSettings::defaultInterpreterId());
    }

    void testNoInterpreterIsShownUntilOneIsPicked();
    void testEditingAFieldUpdatesTheInterpreter();
    void testShowingOneDoesNotWriteTheLastOneIntoIt();
    void testCleanUpOnlyOffersWhenThereIsSomethingToRemove();
    void testApplyingHandsTheListOver();

private:
    QList<Interpreter> m_original;

    static InterpretersAspects *page()
    {
        Core::IOptionsPage *found = Utils::findOrDefault(
            Core::IOptionsPage::allOptionsPages(), [](Core::IOptionsPage *p) {
                return p->id() == Constants::C_PYTHONOPTIONS_PAGE_ID;
            });
        if (!found)
            return nullptr;
        const std::optional<AspectContainer *> aspects = found->aspects();
        return aspects ? static_cast<InterpretersAspects *>(*aspects) : nullptr;
    }
};

void PythonInterpretersTest::testNoInterpreterIsShownUntilOneIsPicked()
{
    InterpretersAspects *p = page();
    QVERIFY(p);
    p->interpreters.setCurrentRow(-1);
    QVERIFY(!p->details.isVisible());
    QVERIFY(!p->remove.isEnabled());
    QVERIFY(!p->makeDefault.isEnabled());
    QVERIFY(!p->generateKit.isEnabled());

    p->add.triggerAction();
    QVERIFY(p->interpreters.currentRow() >= 0);
    QVERIFY(p->details.isVisible());
    QCOMPARE(p->name.volatileValue(), QString("Python"));
    QVERIFY(p->remove.isEnabled());
    QVERIFY(p->makeDefault.isEnabled());

    p->remove.triggerAction();
    QVERIFY(!p->details.isVisible());
}

void PythonInterpretersTest::testEditingAFieldUpdatesTheInterpreter()
{
    InterpretersAspects *p = page();
    QVERIFY(p);
    p->add.triggerAction();
    const int row = p->interpreters.currentRow();
    QVERIFY(row >= 0);

    // Each field on its own: they store through the same call, so setting both
    // before looking would let either one carry the other.
    p->name.setVolatileValue(QString("Renamed"));
    QCOMPARE(interpreterModel().itemAt(row)->itemData.name, QString("Renamed"));
    p->executable.setVolatileValue(QString("/usr/bin/python3"));
    QCOMPARE(interpreterModel().itemAt(row)->itemData.command,
             FilePath::fromString("/usr/bin/python3"));

    p->remove.triggerAction();
}

void PythonInterpretersTest::testShowingOneDoesNotWriteTheLastOneIntoIt()
{
    InterpretersAspects *p = page();
    QVERIFY(p);
    p->add.triggerAction();
    const int first = p->interpreters.currentRow();
    p->name.setVolatileValue(QString("First"));
    p->add.triggerAction();
    const int second = p->interpreters.currentRow();
    QVERIFY(second != first);
    p->name.setVolatileValue(QString("Second"));

    // Going back and forth must leave each one as it was: loading fills the
    // form field by field, which looks exactly like the user typing.
    p->interpreters.setCurrentRow(first);
    QCOMPARE(p->name.volatileValue(), QString("First"));
    p->interpreters.setCurrentRow(second);
    QCOMPARE(p->name.volatileValue(), QString("Second"));
    QCOMPARE(interpreterModel().itemAt(first)->itemData.name, QString("First"));
    QCOMPARE(interpreterModel().itemAt(second)->itemData.name, QString("Second"));
}

void PythonInterpretersTest::testCleanUpOnlyOffersWhenThereIsSomethingToRemove()
{
    InterpretersAspects *p = page();
    QVERIFY(p);
    // One that names nothing runnable is exactly what Clean Up is for.
    p->add.triggerAction();
    const int row = p->interpreters.currentRow();
    p->executable.setVolatileValue(QString("/nowhere/python"));
    QVERIFY(p->cleanUp.isEnabled());

    const int before = interpreterModel().rowCount({});
    p->cleanUp.triggerAction();
    QVERIFY(interpreterModel().rowCount({}) < before);
    QVERIFY(!p->details.isVisible());
    Q_UNUSED(row)
}

void PythonInterpretersTest::testApplyingHandsTheListOver()
{
    InterpretersAspects *p = page();
    QVERIFY(p);
    p->add.triggerAction();
    p->name.setVolatileValue(QString("Applied"));

    static_cast<BaseAspect *>(p)->apply();

    QVERIFY2(Utils::anyOf(PythonSettings::interpreters(),
                          [](const Interpreter &i) { return i.name == "Applied"; }),
             "the interpreter did not reach PythonSettings");
}

class PyLSSettingsTest : public QObject
{
    Q_OBJECT

private slots:
    void cleanup()
    {
        if (PyLSAspects *p = page())
            static_cast<BaseAspect *>(p)->cancel();
    }

    void testTheBoxesAndTheJsonSayTheSameThing()
    {
        // The check boxes are a reading of the configuration, and ticking one
        // rewrites it. A plugin the JSON says nothing about is neither on nor
        // off - saying "off" would be a decision the user never made.
        PyLSAspects *p = page();
        QVERIFY(p);
        TriStateAspect *flake8 = p->pluginAspect("flake8");
        TriStateAspect *pylint = p->pluginAspect("pylint");
        QVERIFY(flake8);
        QVERIFY(pylint);

        p->m_configuration.setValue(
            R"({"pylsp": {"plugins": {"flake8": {"enabled": true},
                                      "pylint": {"enabled": false}}}})");
        QCOMPARE(TriState::fromInt(flake8->volatileValue()), TriState::Enabled);
        QCOMPARE(TriState::fromInt(pylint->volatileValue()), TriState::Disabled);

        p->m_configuration.setValue("{}");
        QCOMPARE(TriState::fromInt(flake8->volatileValue()), TriState::Default);
        QCOMPARE(TriState::fromInt(pylint->volatileValue()), TriState::Default);

        // And the other way: a box that is ticked says so in the JSON.
        flake8->setValue(TriState::Enabled);
        const QJsonObject enabled
            = QJsonDocument::fromJson(p->m_configuration.volatileValue().toUtf8())
                  .object()["pylsp"].toObject()["plugins"].toObject();
        QCOMPARE(enabled["flake8"].toObject()["enabled"].toBool(), true);
        // The one that was never touched is still not mentioned.
        QVERIFY(!enabled.contains("pylint"));
    }

    void testBadJsonSaysWhatIsWrongWithIt()
    {
        // The widget editor put this on the line as a text mark; a settings
        // page has one place to say it.
        PyLSAspects *p = page();
        QVERIFY(p);
        p->m_configuration.setValue("{}");
        QVERIFY(p->m_error.text().isEmpty());

        p->m_configuration.setValue("{ not json");
        QVERIFY(!p->m_error.text().isEmpty());

        p->m_configuration.setValue("{}");
        QVERIFY(p->m_error.text().isEmpty());
    }

private:
    static PyLSAspects *page()
    {
        Core::IOptionsPage *found = Utils::findOrDefault(
            Core::IOptionsPage::allOptionsPages(), [](Core::IOptionsPage *p) {
                return p->id() == Constants::C_PYLSCONFIGURATION_PAGE_ID;
            });
        if (!found)
            return nullptr;
        const std::optional<AspectContainer *> aspects = found->aspects();
        return aspects ? static_cast<PyLSAspects *>(*aspects) : nullptr;
    }
};

QObject *createPyLSSettingsTest()
{
    return new PyLSSettingsTest;
}

QObject *createPythonInterpretersTest()
{
    return new PythonInterpretersTest;
}

#endif // WITH_TESTS

void setupPythonSettings()
{
    static GuardedObject thePythonSettings{new PythonSettings};
}

// InterpreterOptionsPage

class InterpreterOptionsPage : public Core::IOptionsPage
{
public:
    InterpreterOptionsPage()
    {
        setId(Constants::C_PYTHONOPTIONS_PAGE_ID);
        setDisplayName(Tr::tr("Interpreters"));
        setCategory(Constants::C_PYTHON_SETTINGS_CATEGORY);
        setSettingsProvider([] {
            static GuardedObject<InterpretersAspects> theAspects;
            return theAspects.get();
        });
        setFixedKeywords({
            Tr::tr("Name:"),
            Tr::tr("Executable:"),
            Tr::tr("&Add"),
            Tr::tr("&Delete"),
            Tr::tr("&Clean Up"),
            Tr::tr("&Make Default"),
            Tr::tr("&Generate Kit"),
            PathChooser::browseButtonLabel()
        });
    }
};

static InterpreterOptionsPage page;

} // Python::Internal

#include "pythonsettings.moc"

