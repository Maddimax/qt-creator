// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "qmljssettings.h"

#include "qmlformatsettings.h"
#include "qmljssettings.h"
#include "qmljsqtstylecodeformatter.h"
#include "qmljsindenter.h"
#include "qmljstoolsconstants.h"
#include "qmljstoolsinternalconstants.h"
#include "qmljstoolstr.h"

#include <coreplugin/icore.h>
#include <coreplugin/messagemanager.h>

#include <qmljseditor/qmljseditorconstants.h>

#include <projectexplorer/editorconfiguration.h>
#include <projectexplorer/project.h>
#include <projectexplorer/projectexplorer.h>
#include <projectexplorer/projecttree.h>

#include <texteditor/codestylepool.h>
#include <texteditor/command.h>
#include <texteditor/formattexteditor.h>
#include <texteditor/icodestylepreferencesfactory.h>
#include <texteditor/indenter.h>
#include <texteditor/tabsettings.h>

#include <utils/aspectpresentation.h>
#include <utils/aspects.h>
#include <utils/commandline.h>
#include <utils/filepath.h>
#include <utils/guard.h>
#include <utils/mimeconstants.h>
#include <utils/mimeutils.h>
#include <utils/qtcassert.h>
#include <utils/qtcprocess.h>
#include <utils/shutdownguard.h>

#include <QAbstractTableModel>
#include <QColor>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QVersionNumber>

using namespace std::chrono_literals;
using namespace TextEditor;
using namespace QmlJSTools::Internal;
using namespace Utils;

namespace QmlJSTools {

const char idKey[] = "QmlJSGlobal";
const char lineLengthKey[] = "LineLength";
const char qmlformatIniContentKey[] = "QmlFormatIniContent";
const char formatterKey[] = "Formatter";
const char customFormatterPathKey[] = "CustomFormatterPath";
const char customFormatterArgumentsKey[] = "CustomFormatterArguments";

// QmlJSCodeStyleSettings

QmlJSCodeStyleSettings::QmlJSCodeStyleSettings() = default;

void QmlJSCodeStyleSettings::toMap(Store &map) const
{
    map.insert(formatterKey, formatter);
    map.insert(lineLengthKey, lineLength);
    map.insert(qmlformatIniContentKey, qmlformatIniContent);
    map.insert(customFormatterPathKey, customFormatterPath.toUrlishString());
    map.insert(customFormatterArgumentsKey, customFormatterArguments);
}

void QmlJSCodeStyleSettings::fromMap(const Store &map)
{
    lineLength = map.value(lineLengthKey, lineLength).toInt();
    qmlformatIniContent = map.value(qmlformatIniContentKey, qmlformatIniContent).toString();
    formatter = static_cast<Formatter>(map.value(formatterKey, formatter).toInt());
    customFormatterPath = Utils::FilePath::fromString(map.value(customFormatterPathKey).toString());
    customFormatterArguments = map.value(customFormatterArgumentsKey).toString();
}

bool QmlJSCodeStyleSettings::equals(const QmlJSCodeStyleSettings &rhs) const
{
    return lineLength == rhs.lineLength && qmlformatIniContent == rhs.qmlformatIniContent
           && formatter == rhs.formatter && customFormatterPath == rhs.customFormatterPath
           && customFormatterArguments == rhs.customFormatterArguments;
}

QmlJSCodeStyleSettings QmlJSCodeStyleSettings::currentGlobalCodeStyle()
{
    QmlJSCodeStylePreferences *QmlJSCodeStylePreferences = globalQmlJSCodeStyle();
    QTC_ASSERT(QmlJSCodeStylePreferences, return QmlJSCodeStyleSettings());

    return QmlJSCodeStylePreferences->currentCodeStyleSettings();
}

TabSettingsData QmlJSCodeStyleSettings::currentGlobalTabSettings()
{
    QmlJSCodeStylePreferences *QmlJSCodeStylePreferences = globalQmlJSCodeStyle();
    QTC_ASSERT(QmlJSCodeStylePreferences, return TabSettingsData());

    return QmlJSCodeStylePreferences->currentTabSettings();
}

Id QmlJSCodeStyleSettings::settingsId()
{
    return Constants::QML_JS_CODE_STYLE_SETTINGS_ID;
}

// QmlFormatOptionsModel

class QmlFormatOptionsModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    enum Column : int { Name = 0, Value };

    struct Option {
        QString name;
        QVariant value;
        QString hint;
        bool hidden = false;

        bool isBool() const { return hint == QString::fromUtf8(QMetaType::fromType<bool>().name()); }
        bool isInt() const { return hint == QString::fromUtf8(QMetaType::fromType<int>().name()); }
        bool isString() const { return hint == QString::fromUtf8(QMetaType::fromType<QString>().name()); }
        bool isStringList() const { return !isBool() && !isInt() && !isString() && !isNull(); }
        bool isNull() const { return hint.isEmpty(); }
    };

    explicit QmlFormatOptionsModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setOptionsFromJson(const QJsonDocument &doc);
    QString writeGlobalQmlFormatIniFile() const;
    void loadGlobalQmlFormatIniFile();

    const QList<Option> &options() const { return m_options; }

private:
    QList<Option> m_options;
};

QmlFormatOptionsModel::QmlFormatOptionsModel(QObject *parent)
    : QAbstractTableModel(parent)
{}

int QmlFormatOptionsModel::rowCount(const QModelIndex &) const { return m_options.size(); }
int QmlFormatOptionsModel::columnCount(const QModelIndex &) const { return 2; }

QVariant QmlFormatOptionsModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_options.size())
        return QVariant();

    const Option &option = m_options.at(index.row());
    const bool isValueCell = index.column() == Column::Value;

    if (role == Qt::DisplayRole) {
        switch (index.column()) {
        case Column::Name: return option.name;
        case Column::Value: return option.isBool() ? QString() : option.value;
        }
    } else if (role == Qt::EditRole) {
        switch (index.column()) {
        case Column::Name: return option.name;
        case Column::Value: return option.value;
        }
    } else if (role == Qt::CheckStateRole && isValueCell && option.isBool()) {
        return option.value.toBool() ? Qt::Checked : Qt::Unchecked;
    } else if (role == Qt::ForegroundRole && option.hidden) {
        return QColor(Qt::gray);
    } else if (role == Qt::ToolTipRole && option.hidden) {
        return Tr::tr("This option was found in the INI file but is not a standard qmlformat option.");
    } else if (role == AspectTable::EditableRole) {
        return AspectTable::isWritable(flags(index));
    } else if (role == AspectTable::CheckableRole) {
        return isValueCell && option.isBool();
    } else if (role == AspectTable::ChoicesRole && isValueCell && option.isStringList()) {
        // What the hint lists, as the choices a view offers. Which cells offer
        // one depends on the option, which is why the model answers and not
        // the view - it is the same answer for a QTableView.
        QVariantList choices;
        const QStringList values = option.hint.split(',');
        for (const QString &value : values)
            choices.append(QVariantMap{{"display", value}, {"id", value}});
        return choices;
    } else if (role == AspectTable::ValidatorRole && isValueCell && option.isInt()) {
        return QString(R"(-?\d*)");
    }

    return QVariant();
}

QVariant QmlFormatOptionsModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation == Qt::Horizontal && role == Qt::DisplayRole) {
        switch (section) {
        case Column::Name: return Tr::tr("Option");
        case Column::Value: return Tr::tr("Value");
        }
    }
    return QVariant();
}

bool QmlFormatOptionsModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (!index.isValid() || index.row() >= m_options.size() || index.column() != Column::Value)
        return false;

    if (role == Qt::EditRole) {
        m_options[index.row()].value = value;
        emit dataChanged(index, index);
        return true;
    } else if (role == Qt::CheckStateRole && m_options[index.row()].isBool()) {
        m_options[index.row()].value = (value.toInt() == Qt::Checked);
        emit dataChanged(index, index);
        return true;
    }
    return false;
}

Qt::ItemFlags QmlFormatOptionsModel::flags(const QModelIndex &index) const
{
    if (!index.isValid())
        return Qt::NoItemFlags;

    Qt::ItemFlags flags = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    if (index.column() == Column::Value) {
        flags |= Qt::ItemIsEditable;
        if (index.row() < m_options.size() && m_options[index.row()].isBool())
            flags |= Qt::ItemIsUserCheckable;
    }
    return flags;
}

QHash<int, QByteArray> QmlFormatOptionsModel::roleNames() const
{
    return AspectTable::withRoleNames(QAbstractTableModel::roleNames());
}

void QmlFormatOptionsModel::setOptionsFromJson(const QJsonDocument &doc)
{
    beginResetModel();
    m_options.clear();

    if (doc.isObject()) {
        for (const QJsonValue &val : doc.object()["options"].toArray()) {
            if (!val.isObject())
                continue;
            QJsonObject obj = val.toObject();
            m_options.append({obj["name"].toString(), obj["value"].toVariant(),
                              obj["hint"].toString()});
        }
    }

    endResetModel();
}

QString QmlFormatOptionsModel::writeGlobalQmlFormatIniFile() const
{
    QSettings settings(QmlFormatSettings::instance().globalQmlFormatIniFile().toFSPathString(),
                       QSettings::IniFormat);
    settings.clear();
    for (const Option &option : m_options)
        settings.setValue(option.name, option.value);
    settings.sync();

    QFile file(QmlFormatSettings::instance().globalQmlFormatIniFile().toFSPathString());
    QTC_CHECK(file.open(QIODevice::ReadOnly));
    return QString::fromUtf8(file.readAll());
}

void QmlFormatOptionsModel::loadGlobalQmlFormatIniFile()
{
    beginResetModel();

    QSettings settings(QmlFormatSettings::instance().globalQmlFormatIniFile().toFSPathString(),
                       QSettings::IniFormat);

    QSet<QString> foundOptions;
    for (Option &option : m_options) {
        if (settings.contains(option.name)) {
            option.value = settings.value(option.name);
            foundOptions.insert(option.name);
        }
    }
    for (const QString &key : settings.allKeys()) {
        if (!foundOptions.contains(key))
            m_options.append({key, settings.value(key), QString(), true});
    }
    std::sort(m_options.begin(), m_options.end(),
              [](const Option &a, const Option &b) { return a.name < b.name; });

    endResetModel();
}

// What qmlformat offers when it cannot be asked. Kept so that the page shows
// the usual options with no Qt kit around, and so that a test does not depend
// on one being installed.
static QJsonDocument fallbackOptionsJson()
{
    QJsonObject root;
    QJsonArray optionsArray;
    auto addOption = [&](QLatin1StringView name, const QJsonValue &value, QLatin1StringView hint) {
        QJsonObject obj;
        obj["name"] = name;
        obj["value"] = value;
        obj["hint"] = hint;
        optionsArray.append(obj);
    };
    addOption(QLatin1String("NewlineType"), "native", QLatin1String("native,macos,unix,windows"));
    addOption(QLatin1String("MaxColumnWidth"), -1, QLatin1String(QMetaType::fromType<int>().name()));
    addOption(QLatin1String("UseTabs"), false, QLatin1String(QMetaType::fromType<bool>().name()));
    addOption(QLatin1String("NormalizeOrder"), false, QLatin1String(QMetaType::fromType<bool>().name()));
    addOption(QLatin1String("ObjectsSpacing"), false, QLatin1String(QMetaType::fromType<bool>().name()));
    addOption(QLatin1String("FunctionsSpacing"), false, QLatin1String(QMetaType::fromType<bool>().name()));
    addOption(QLatin1String("IndentWidth"), 4, QLatin1String(QMetaType::fromType<int>().name()));
    addOption(QLatin1String("SemicolonRule"), "always", QLatin1String("always,essential"));
    root[QStringLiteral("options")] = optionsArray;
    return QJsonDocument(root);
}

// QmlFormatOptionsAspect

// The global qmlformat configuration, as a table. The value is the INI file's
// contents, because that is what the code style stores and what qmlformat
// reads; the rows are the model's, and the two are kept in step through the
// file itself, which has to be on disk for qmlformat to find it anyway.
class QmlFormatOptionsAspect final : public TypedAspect<QString>
{
public:
    explicit QmlFormatOptionsAspect(AspectContainer *container);

    AspectPresentation presentation() const override;
    QAbstractItemModel *tableModel() override;

    // Puts qmlformat's own options back, discarding what the INI file held.
    void resetToQmlFormatDefaults();
    // The options qmlformat reports, or the fallback. Asked for once and
    // asynchronously: this is built while a settings page is being opened.
    void askQmlFormatForItsOptions();

protected:
    void volatileValueToGui() override;

private:
    void setOptionsJson(const QJsonDocument &doc);

    // Parented: a model handed to QML from a Q_INVOKABLE with no parent is one
    // QML takes ownership of and deletes.
    QmlFormatOptionsModel m_model{this};
    std::unique_ptr<Process> m_optionsProcess;
    QJsonDocument m_reportedOptions;
    // The model is what produced the current value, so reloading it from that
    // value would reset the view while a cell editor is still open in it.
    bool m_writingBack = false;
};

QmlFormatOptionsAspect::QmlFormatOptionsAspect(AspectContainer *container)
    : TypedAspect<QString>(container)
{
    setQmlName("QmlFormatOptions");
    m_reportedOptions = fallbackOptionsJson();
    setOptionsJson(m_reportedOptions);

    connect(&m_model, &QAbstractItemModel::dataChanged, this, [this] {
        m_writingBack = true;
        setVolatileValue(m_model.writeGlobalQmlFormatIniFile());
        m_writingBack = false;
    });
}

AspectPresentation QmlFormatOptionsAspect::presentation() const
{
    AspectPresentation presentation = TypedAspect<QString>::presentation();
    presentation.control = AspectControls::Table;
    // The options are qmlformat's, so there is no row to add or take away.
    presentation.allowAdding = false;
    presentation.allowRemoving = false;
    return presentation;
}

QAbstractItemModel *QmlFormatOptionsAspect::tableModel()
{
    return &m_model;
}

void QmlFormatOptionsAspect::volatileValueToGui()
{
    if (m_writingBack)
        return;
    QmlFormatSettings::globalQmlFormatIniFile().writeFileContents(m_volatileValue.toUtf8());
    m_model.loadGlobalQmlFormatIniFile();
}

void QmlFormatOptionsAspect::setOptionsJson(const QJsonDocument &doc)
{
    m_model.setOptionsFromJson(doc);
    m_model.loadGlobalQmlFormatIniFile();
}

void QmlFormatOptionsAspect::resetToQmlFormatDefaults()
{
    setOptionsJson(m_reportedOptions);
    m_writingBack = true;
    setVolatileValue(m_model.writeGlobalQmlFormatIniFile());
    m_writingBack = false;
}

void QmlFormatOptionsAspect::askQmlFormatForItsOptions()
{
    using namespace Core;
    const FilePath &qmlFormatPath = QmlFormatSettings::instance().latestQmlFormatPath();
    if (qmlFormatPath.isEmpty()) {
        MessageManager::writeSilently(Tr::tr("qmlformat not found. Using fallback output options."));
        return;
    }

    const FilePath executable = CommandLine(qmlFormatPath).executable();
    m_optionsProcess.reset(new Process);
    m_optionsProcess->setCommand({executable, {"--output-options"}});
    m_optionsProcess->setUtf8StdOutCodec();
    connect(m_optionsProcess.get(), &Process::done, this, [this, executable] {
        const QString errorText = m_optionsProcess->readAllStandardError();
        if (m_optionsProcess->result() != ProcessResult::FinishedWithSuccess
            || !errorText.isEmpty()) {
            MessageManager::writeFlashing(
                Tr::tr("\"%1\": %2. Using fallback output options.")
                    .arg(executable.toUserOutput(),
                         errorText.isEmpty() ? m_optionsProcess->errorString() : errorText));
            return;
        }

        const QJsonDocument doc
            = QJsonDocument::fromJson(m_optionsProcess->readAllStandardOutput().toUtf8());
        if (doc.isNull() || !doc.isObject() || !doc.object().contains("options")) {
            MessageManager::writeFlashing(
                Tr::tr("Invalid JSON response from qmlformat. Using fallback output options."));
            return;
        }
        m_reportedOptions = doc;
        setOptionsJson(doc);
    });
    m_optionsProcess->start();
}

// How the preview is formatted, which is by whichever formatter the style
// selected. The built-in one has no command to run: its formatting is the
// indenter, which the preview runs anyway.
static Result<QString> formatPreview(ICodeStylePreferences *codeStyle, const QString &text)
{
    auto preferences = dynamic_cast<QmlJSCodeStylePreferences *>(codeStyle);
    QTC_ASSERT(preferences, return text);
    const QmlJSCodeStyleSettings settings = preferences->currentCodeStyleSettings();

    FilePath executable;
    QStringList arguments;
    switch (settings.formatter) {
    case QmlJSCodeStyleSettings::Builtin:
        return text;
    case QmlJSCodeStyleSettings::QmlFormat:
        executable = QmlFormatSettings::instance().latestQmlFormatPath();
        if (executable.isEmpty())
            return ResultError(Tr::tr("qmlformat not found."));
        break;
    case QmlJSCodeStyleSettings::Custom:
        executable = settings.customFormatterPath;
        if (executable.isEmpty())
            return ResultError(Tr::tr("Custom formatter not found."));
        arguments = settings.customFormatterArguments.split(' ', Qt::SkipEmptyParts);
        break;
    }

    const CommandLine commandLine(executable, arguments);
    TextEditor::Command command;
    command.setExecutable(commandLine.executable());
    command.setProcessing(TextEditor::Command::FileProcessing);
    command.addOptions(commandLine.splitArguments());
    command.addOption("--inplace");
    command.addOption("%file");
    if (!command.isValid())
        return ResultError(Tr::tr("Cannot run \"%1\".").arg(executable.toUserOutput()));

    // A file name rather than a real file: the formatter is told what the code
    // is by its suffix, and formatText() writes the text to a temporary copy.
    return TextEditor::formatText(FilePath::fromString("preview.qml"), text, command);
}

// QmlJSCodeStyleAspects

// What the QML/JS Code Style form edits. The settings live in the code style
// rather than in a settings key of their own, so these aspects read from and
// write to the preferences the page handed over - its own editable copy - and
// nothing here is saved directly.
class QmlJSCodeStyleAspects final : public AspectContainer
{
public:
    explicit QmlJSCodeStyleAspects(QmlJSCodeStylePreferences *preferences);

private:
    void readFromPreferences();
    void writeToPreferences();
    void updateState();

    QmlJSCodeStylePreferences *m_preferences = nullptr;
    Guard m_reading;

    SelectionAspect m_formatter{this};

    AspectContainer m_builtinSettings{this};
    TabSettings m_tabSettings;
    IntegerAspect m_lineLength{&m_builtinSettings};

    AspectContainer m_qmlFormatSettings{this};
    TextDisplay m_qmlFormatVersion{&m_qmlFormatSettings};
    QmlFormatOptionsAspect m_qmlFormatOptions{&m_qmlFormatSettings};
    ActionAspect m_deployIni{&m_qmlFormatSettings};
    ActionAspect m_resetOptions{&m_qmlFormatSettings};

    AspectContainer m_customSettings{this};
    FilePathAspect m_customFormatterPath{&m_customSettings};
    StringAspect m_customFormatterArguments{&m_customSettings};
};

QmlJSCodeStyleAspects::QmlJSCodeStyleAspects(QmlJSCodeStylePreferences *preferences)
    : m_preferences(preferences)
{
    m_formatter.setQmlName("Formatter");
    m_formatter.setLabelText(Tr::tr("Formatter"));
    m_formatter.setDisplayStyle(SelectionAspect::DisplayStyle::RadioButtons);
    m_formatter.setDefaultValue(QmlJSCodeStyleSettings::Builtin);
    m_formatter.addOption(Tr::tr("Built-In Formatter [Deprecated]"));
    m_formatter.addOption(Tr::tr("QmlFormat [LSP]"));
    m_formatter.addOption(Tr::tr("Custom Formatter [Must be qmlformat compatible]"));

    m_builtinSettings.setQmlName("BuiltinSettings");
    m_tabSettings.setQmlName("TabSettings");
    m_builtinSettings.registerAspect(&m_tabSettings);
    m_tabSettings.setPreferences(preferences);
    m_lineLength.setQmlName("LineLength");
    m_lineLength.setLabelText(Tr::tr("Line length:"));
    m_lineLength.setRange(0, 999);

    m_qmlFormatSettings.setQmlName("QmlFormatSettings");
    m_qmlFormatVersion.setQmlName("QmlFormatVersion");
    m_deployIni.setQmlName("DeployIni");
    m_deployIni.setActionText(Tr::tr("Deploy INI File to Current Project"));
    m_deployIni.setAction([this] {
        if (ProjectExplorer::Project *const project = ProjectExplorer::ProjectTree::currentProject())
            project->projectDirectory().pathAppended(".qmlformat.ini")
                .writeFileContents(m_qmlFormatOptions.volatileValue().toUtf8());
    });
    m_resetOptions.setQmlName("ResetOptions");
    m_resetOptions.setActionText(Tr::tr("Reset to Defaults"));
    m_resetOptions.setAction([this] { m_qmlFormatOptions.resetToQmlFormatDefaults(); });

    m_customSettings.setQmlName("CustomSettings");
    m_customFormatterPath.setQmlName("CustomFormatterPath");
    m_customFormatterPath.setLabelText(Tr::tr("Command:"));
    m_customFormatterPath.setPlaceHolderText(
        QmlFormatSettings::instance().latestQmlFormatPath().toUrlishString());
    m_customFormatterArguments.setQmlName("CustomFormatterArguments");
    m_customFormatterArguments.setLabelText(Tr::tr("Arguments:"));
    m_customFormatterArguments.setDisplayStyle(StringAspect::LineEditDisplay);

    // The version qmlformat reports is the version of the Qt it ships with, and
    // that is already known - asking the binary would mean running it while a
    // settings page is being opened.
    const QVersionNumber version = QmlFormatSettings::instance().latestQmlFormatVersion();
    m_qmlFormatVersion.setText(version.isNull()
                                   ? Tr::tr("Unknown qmlformat version")
                                   : Tr::tr("qmlformat %1").arg(version.toString()));

    readFromPreferences();
    m_qmlFormatOptions.askQmlFormatForItsOptions();

    connect(this, &AspectContainer::volatileValueChanged,
            this, &QmlJSCodeStyleAspects::writeToPreferences);
    connect(&m_formatter, &BaseAspect::volatileValueChanged,
            this, &QmlJSCodeStyleAspects::updateState);
    connect(preferences, &QmlJSCodeStylePreferences::currentValueChanged,
            this, &QmlJSCodeStyleAspects::readFromPreferences);
    connect(preferences, &QmlJSCodeStylePreferences::currentPreferencesChanged,
            this, &QmlJSCodeStyleAspects::readFromPreferences);

    // Only where there is a project to deploy into.
    const auto updateDeploy = [this] {
        m_deployIni.setEnabled(ProjectExplorer::ProjectTree::currentProject() != nullptr);
    };
    connect(ProjectExplorer::ProjectTree::instance(),
            &ProjectExplorer::ProjectTree::currentProjectChanged, this, updateDeploy);
    updateDeploy();
}

void QmlJSCodeStyleAspects::readFromPreferences()
{
    const GuardLocker locker(m_reading);
    const QmlJSCodeStyleSettings settings = m_preferences->currentCodeStyleSettings();
    m_formatter.setValue(settings.formatter);
    m_lineLength.setValue(settings.lineLength);
    m_qmlFormatOptions.setValue(settings.qmlformatIniContent);
    m_customFormatterPath.setValue(settings.customFormatterPath);
    m_customFormatterArguments.setValue(settings.customFormatterArguments);
    updateState();
}

void QmlJSCodeStyleAspects::writeToPreferences()
{
    if (m_reading.isLocked())
        return;

    auto current = dynamic_cast<QmlJSCodeStylePreferences *>(m_preferences->currentPreferences());
    if (!current || current->isReadOnly())
        return;

    QmlJSCodeStyleSettings settings = current->codeStyleSettings();
    settings.formatter
        = static_cast<QmlJSCodeStyleSettings::Formatter>(m_formatter.volatileValue());
    settings.lineLength = m_lineLength.volatileValue();
    settings.qmlformatIniContent = m_qmlFormatOptions.volatileValue();
    settings.customFormatterPath = FilePath::fromUserInput(m_customFormatterPath.volatileValue());
    settings.customFormatterArguments = m_customFormatterArguments.volatileValue();
    current->setCodeStyleSettings(settings);
}

void QmlJSCodeStyleAspects::updateState()
{
    auto current = dynamic_cast<QmlJSCodeStylePreferences *>(m_preferences->currentPreferences());
    const bool editable = current && !current->isReadOnly();
    const int formatter = m_formatter.volatileValue();

    m_formatter.setEnabled(editable);

    // One formatter's settings at a time: the widget page stacked them, and
    // there is nothing useful to say about the two that are not in use.
    const auto show = [editable](AspectContainer &group, bool selected) {
        group.setVisible(selected);
        group.setEnabled(editable);
    };
    show(m_builtinSettings, formatter == QmlJSCodeStyleSettings::Builtin);
    show(m_qmlFormatSettings, formatter == QmlJSCodeStyleSettings::QmlFormat);
    show(m_customSettings, formatter == QmlJSCodeStyleSettings::Custom);
}

// QmlJSCodeStyleSettingsPage

QmlJSCodeStyleSettingsPage::QmlJSCodeStyleSettingsPage()
{
    setId(Constants::QML_JS_CODE_STYLE_SETTINGS_ID);
    setDisplayName(Tr::tr(Constants::QML_JS_CODE_STYLE_SETTINGS_NAME));
    setCategory(QmlJSEditor::Constants::SETTINGS_CATEGORY_QML);
    setSettingsProvider([] {
        static CodeStyleAspect theSettings(globalQmlJSCodeStyle(),
                                           QmlJSTools::Constants::QML_JS_SETTINGS_ID);
        return &theSettings;
    });
}

// QmlJSCodeStylePreferencesFactory

class QmlJSCodeStylePreferencesFactory final : public ICodeStylePreferencesFactory
{
public:
    QmlJSCodeStylePreferencesFactory()
        : ICodeStylePreferencesFactory(Constants::QML_JS_SETTINGS_ID)
    {
        setDisplayName(Tr::tr("Qt Quick"));
        setSnippetGroupId(QmlJSEditor::Constants::QML_SNIPPETS_GROUP_ID);
        setPreviewText(QString::fromLatin1(Internal::previewText));
        setIndenterCreator([](QTextDocument *doc) { return QmlJSEditor::createQmlJsIndenter(doc); });
        setCodeStyleCreator([] { return new QmlJSCodeStylePreferences; });
        setQmlSource(QUrl("qrc:/qt/qml/QtCreator/QmlJSTools/QmlJSCodeStylePage.qml"));
        setSettingsAspectsCreator([](ICodeStylePreferences *codeStyle) {
            return new QmlJSCodeStyleAspects(
                static_cast<QmlJSCodeStylePreferences *>(codeStyle));
        });
        setPreviewFormatter(&formatPreview);

        setGlobalCodeStyleId(idKey);
        setDefaultCodeStyleId("qt");
        setBuiltInCodeStyles([this](CodeStylePool *pool) {
            m_qtCodeStyle.setId("qt");
            m_qtCodeStyle.setDisplayName(Tr::tr("Qt"));
            m_qtCodeStyle.setReadOnly(true);
            TabSettingsData qtTabSettings;
            qtTabSettings.m_tabPolicy = TabSettingsData::SpacesOnlyTabPolicy;
            qtTabSettings.m_tabSize = 4;
            qtTabSettings.m_indentSize = 4;
            qtTabSettings.m_continuationAlignBehavior = TabSettingsData::ContinuationAlignWithIndent;
            m_qtCodeStyle.setTabSettings(qtTabSettings);
            pool->addCodeStyle(&m_qtCodeStyle);
        });
        setupCodeStyles();

        // Push a created qmlformat.ini into the built-in styles' settings.
        QObject::connect(&QmlFormatSettings::instance(), &QmlFormatSettings::qmlformatIniCreated,
                         [](Utils::FilePath qmlformatIniPath) {
            QmlJSCodeStyleSettings s;
            s.lineLength = 80;
            const Utils::Result<QByteArray> fileContents = qmlformatIniPath.fileContents();
            if (fileContents)
                s.qmlformatIniContent = QString::fromUtf8(*fileContents);
            auto csPool = TextEditor::codeStylePool(QmlJSTools::Constants::QML_JS_SETTINGS_ID);
            QTC_ASSERT(csPool, return);
            const auto builtInCodeStyles = csPool->builtInCodeStyles();
            for (auto codeStyle : builtInCodeStyles) {
                if (auto qtCodeStyle = dynamic_cast<QmlJSCodeStylePreferences *>(codeStyle))
                    qtCodeStyle->setCodeStyleSettings(s);
            }
        });

        using namespace Utils::Constants;
        registerMimeTypeForLanguageId(QML_MIMETYPE, Constants::QML_JS_SETTINGS_ID);
        registerMimeTypeForLanguageId(QMLUI_MIMETYPE, Constants::QML_JS_SETTINGS_ID);
        registerMimeTypeForLanguageId(QBS_MIMETYPE, Constants::QML_JS_SETTINGS_ID);
        registerMimeTypeForLanguageId(QMLPROJECT_MIMETYPE, Constants::QML_JS_SETTINGS_ID);
        registerMimeTypeForLanguageId(QMLTYPES_MIMETYPE, Constants::QML_JS_SETTINGS_ID);
        registerMimeTypeForLanguageId(JS_MIMETYPE, Constants::QML_JS_SETTINGS_ID);
        registerMimeTypeForLanguageId(JSON_MIMETYPE, Constants::QML_JS_SETTINGS_ID);
    }

private:
    QmlJSCodeStylePreferences m_qtCodeStyle;
};

QmlJSCodeStylePreferences *globalQmlJSCodeStyle()
{
    return static_cast<QmlJSCodeStylePreferences *>(
        codeStyleForLanguage(QmlJSTools::Constants::QML_JS_SETTINGS_ID));
}

void Internal::setupQmlJSToolsSettings()
{
    static GuardedObject<QmlJSCodeStylePreferencesFactory> theQmlJSCodeStylePreferencesFactory;
}

} // QmlJSTools::Internal

#include "qmljssettings.moc"
