// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "qmljseditorsettings.h"
#include "qmljseditorconstants.h"
#include "qmljseditortr.h"

#include <coreplugin/coreconstants.h>
#include <coreplugin/icore.h>
#include <coreplugin/dialogs/ioptionspage.h>

#include <languageclient/languageclientsettings.h>

#include <projectexplorer/projectexplorer.h>
#include <projectexplorer/projectmanager.h>
#include <projectexplorer/projecttree.h>

#include <qmljs/qmljscheck.h>
#include <qmljs/qmljsstaticanalysismessage.h>

#include <qtsupport/qtsupportconstants.h>

#include <updateinfo/updateinfoservice.h>

#include <extensionsystem/pluginmanager.h>

#include <utils/aspectwidgets.h>
#include <utils/algorithm.h>
#include <utils/guiutils.h>
#include <utils/hostosinfo.h>
#include <utils/layoutbuilder.h>
#include <utils/pathchooser.h>
#include <utils/qtcsettings.h>
#include <utils/treemodel.h>

#include <QGroupBox>
#include <QMenu>
#include <QPushButton>
#include <QTreeView>


using namespace QmlJSEditor::Internal;
using namespace QtSupport;
using namespace Utils;
using namespace ProjectExplorer;

namespace QmlJSEditor::Internal {

const char AUTO_FORMAT_ON_SAVE[] = "QmlJSEditor.AutoFormatOnSave";
const char AUTO_FORMAT_ONLY_CURRENT_PROJECT[] = "QmlJSEditor.AutoFormatOnlyCurrentProject";
const char QML_CONTEXTPANE_KEY[] = "QmlJSEditor.ContextPaneEnabled";
const char QML_CONTEXTPANEPIN_KEY[] = "QmlJSEditor.ContextPanePinned";
const char FOLD_AUX_DATA[] = "QmlJSEditor.FoldAuxData";
const char UIQML_OPEN_MODE[] = "QmlJSEditor.openUiQmlMode";
const char CUSTOM_ANALYZER[] = "QmlJSEditor.useCustomAnalyzer";
const char DISABLED_MESSAGES[] = "QmlJSEditor.disabledMessages";
const char DISABLED_MESSAGES_NONQUICKUI[] = "QmlJSEditor.disabledMessagesNonQuickUI";
const char QDS_COMMAND[] = "QmlJSEditor.qdsCommand";
const char SETTINGS_PAGE[] = "C.QmlJsEditing";

QmlJsEditingSettings &settings()
{
    static QmlJsEditingSettings settings;
    return settings;
}

using namespace LanguageClient;

static QList<int> defaultDisabledMessages()
{
    static const QList<int> disabledByDefault = Utils::transform(
                QmlJS::Check::defaultDisabledMessages(),
                [](QmlJS::StaticAnalysis::Type t) { return int(t); });
    return disabledByDefault;
}

static QList<int> defaultDisabledMessagesNonQuickUi()
{
    static const QList<int> disabledForNonQuickUi = Utils::transform(
        QmlJS::Check::defaultDisabledMessagesForNonQuickUi(),
        [](QmlJS::StaticAnalysis::Type t){ return int(t); });
    return disabledForNonQuickUi;
}

static void openQtVersionsOptions()
{
    Core::ICore::showSettings(QtSupport::Constants::QTVERSION_SETTINGS_PAGE_ID);
}

static UpdateInfo::Service *updateInfoService()
{
    return ExtensionSystem::PluginManager::getObject<UpdateInfo::Service>();
}

QmlJsEditingSettings::QmlJsEditingSettings()
{
    setAutoApply(false);
    const Key group = QmlJSEditor::Constants::SETTINGS_CATEGORY_QML;

    enableContextPane.setSettingsKey(group, QML_CONTEXTPANE_KEY);
    enableContextPane.setLabelText(Tr::tr("Always show Qt Quick Toolbar"));

    pinContextPane.setSettingsKey(group, QML_CONTEXTPANEPIN_KEY);
    pinContextPane.setLabelText(Tr::tr("Pin Qt Quick Toolbar"));

    autoFormatOnSave.setSettingsKey(group, AUTO_FORMAT_ON_SAVE);
    autoFormatOnSave.setLabelText(Tr::tr("Enable auto format on file save"));

    autoFormatOnlyCurrentProject.setSettingsKey(group, AUTO_FORMAT_ONLY_CURRENT_PROJECT);
    autoFormatOnlyCurrentProject.setLabelText(
        Tr::tr("Restrict to files contained in the current project"));

    foldAuxData.setSettingsKey(group, FOLD_AUX_DATA);
    foldAuxData.setDefaultValue(true);
    foldAuxData.setLabelText(Tr::tr("Auto-fold auxiliary data"));

    uiQmlOpenMode.setSettingsKey(group, UIQML_OPEN_MODE);
    uiQmlOpenMode.setUseDataAsSavedValue();
    uiQmlOpenMode.setDisplayStyle(SelectionAspect::DisplayStyle::ComboBox);
    uiQmlOpenMode.setLabelText(Tr::tr("Open .ui.qml files with:"));
    uiQmlOpenMode.addOption({Tr::tr("Always Ask")});
    uiQmlOpenMode.addOption({Tr::tr("Qt Design Studio"), {}, Core::Constants::MODE_DESIGN});
    uiQmlOpenMode.addOption({Tr::tr("Qt Creator"), {}, Core::Constants::MODE_EDIT});

    useCustomAnalyzer.setSettingsKey(group, CUSTOM_ANALYZER);
    useCustomAnalyzer.setLabelText(Tr::tr("Use customized static analyzer"));

    qdsCommand.setSettingsKey(group, QDS_COMMAND);
    qdsCommand.setPlaceHolderText(defaultQdsCommand().toUserOutput());
    qdsCommand.setLabelText(Tr::tr("Command:"));
    qdsCommand.setVisible(false);

    qdsHint.setQmlName("QdsHint");
    qdsHint.setWordWrap(true);
    qdsHint.setText(Tr::tr("Set the path to the Qt Design Studio application to enable "
                           "the \"Open in Qt Design Studio\" feature. If you have Qt "
                           "Design Studio installed alongside Qt Creator with the Qt "
                           "Online Installer, it is used as the default. Use "
                           "<a href=\"linkwithqt\">\"Link with Qt\"</a> to link an "
                           "offline installation of Qt Creator to a Qt Online Installer."));
    connect(&qdsHint, &TextDisplay::linkActivated, this, [] { openQtVersionsOptions(); });

    // There is nothing to install with if no updater is loaded, and nothing to
    // install if a Qt Design Studio is already known.
    const auto updateQdsInstall = [this] {
        qdsCommand.setPlaceHolderText(defaultQdsCommand().toUserOutput());
        qdsInstall.setVisible(defaultQdsCommand().isEmpty() && updateInfoService());
    };

    qdsInstall.setQmlName("QdsInstall");
    qdsInstall.setActionText(Tr::tr("Install Qt Design Studio"));
    qdsInstall.setAction([this, updateQdsInstall] {
        UpdateInfo::Service *updater = updateInfoService();
        QTC_ASSERT(updater, return);
        if (updater->installPackages("^qt[.].*qtdesignstudio.*$")) {
            updateQdsInstall();
            emit qdsCommand.changed();
        }
    });

    openLanguageServerSettings.setQmlName("OpenLanguageServerSettings");
    openLanguageServerSettings.setActionText(Tr::tr("Open Language Server preferences..."));
    openLanguageServerSettings.setAction([] {
        Core::ICore::showSettings(LanguageClient::Constants::LANGUAGECLIENT_SETTINGS_PAGE);
    });

    resetAnalyzerMessages.setQmlName("ResetAnalyzerMessages");
    resetAnalyzerMessages.setActionText(Tr::tr("Reset to Default"));
    resetAnalyzerMessages.setToolTip(Tr::tr("Turns every check back on except the ones that are "
                                            "off by default."));
    resetAnalyzerMessages.setAction([this] { analyzerMessages.resetToDefault(); });

    setQmlSource(QUrl("qrc:/qt/qml/QtCreator/QmlJSEditor/QmlJsEditingSettingsPage.qml"));

    readSettings();

    analyzerMessages.setEnabler(&useCustomAnalyzer);
    resetAnalyzerMessages.setEnabler(&useCustomAnalyzer);

    updateQdsInstall();
}

FilePath QmlJsEditingSettings::defaultQdsCommand() const
{
    QtcSettings *settings = Core::ICore::settings();
    const Key qdsInstallationEntry = "QML/Designer/DesignStudioInstallation"; //set in installer
    return FilePath::fromUserInput(settings->value(qdsInstallationEntry).toString());
}

static QStringList disabledMessagesToStringList(const QList<int> &list)
{
    return Utils::transform<QStringList>(list, [](int i) { return QString::number(i); });
}

static QList<int> disabledMessagesFromStringList(const QStringList &list)
{
    return Utils::transform<QList<int>>(list, [](const QString &s) { return s.toInt(); });
}

// One row per known static analyzer message: a check box that turns the check
// on, a second that turns it off in files that are not a Qt Quick UI, and the
// message itself to read. The check states are the aspect's volatile value,
// which is what this writes into.
class AnalyzerMessagesModel : public QAbstractTableModel
{
public:
    enum Column { ColumnEnabled, ColumnNonQuickUi, ColumnMessage, ColumnCount };

    struct State
    {
        QList<int> disabled;
        QList<int> disabledForNonQuickUi;
    };

    AnalyzerMessagesModel(State *state, QObject *parent)
        : QAbstractTableModel(parent)
        , m_state(state)
    {
        using namespace QmlJS::StaticAnalysis;
        for (Type type : Utils::sorted(Message::allMessageTypes())) {
            m_rows.append({int(type),
                           Message::prototypeForMessageType(type).message.split('\n').first()});
        }
    }

    void reread()
    {
        if (m_rows.isEmpty())
            return;
        emit dataChanged(index(0, ColumnEnabled),
                         index(m_rows.size() - 1, ColumnNonQuickUi),
                         {Qt::CheckStateRole});
    }

    int rowCount(const QModelIndex &parent) const override
    {
        return parent.isValid() ? 0 : m_rows.size();
    }

    int columnCount(const QModelIndex &parent) const override
    {
        return parent.isValid() ? 0 : ColumnCount;
    }

    QVariant data(const QModelIndex &index, int role) const override
    {
        const Row &row = m_rows.at(index.row());
        switch (role) {
        case Qt::DisplayRole:
            switch (index.column()) {
            case ColumnEnabled: return QString("M%1").arg(row.number);
            case ColumnMessage: return row.message;
            default:            return QString();
            }
        case Qt::CheckStateRole:
            if (index.column() == ColumnEnabled)
                return m_state->disabled.contains(row.number) ? Qt::Unchecked : Qt::Checked;
            if (index.column() == ColumnNonQuickUi) {
                return m_state->disabledForNonQuickUi.contains(row.number) ? Qt::Checked
                                                                          : Qt::Unchecked;
            }
            return {};
        case Qt::ToolTipRole:
            return row.message;
        case AspectTable::EditableRole:
            return AspectTable::isWritable(flags(index));
        case AspectTable::CheckableRole:
            return flags(index).testFlag(Qt::ItemIsUserCheckable);
        default:
            return {};
        }
    }

    bool setData(const QModelIndex &index, const QVariant &value, int role) override
    {
        if (role != Qt::CheckStateRole || !flags(index).testFlag(Qt::ItemIsUserCheckable))
            return false;
        const int number = m_rows.at(index.row()).number;
        const bool on = value.toInt() == Qt::Checked;
        // A message the user ticked is one that is not disabled, so the Enabled
        // column stores the opposite of what it shows.
        QList<int> &list = index.column() == ColumnEnabled ? m_state->disabled
                                                           : m_state->disabledForNonQuickUi;
        const bool listed = index.column() == ColumnEnabled ? !on : on;
        if (listed) {
            if (!list.contains(number))
                list.append(number);
        } else {
            list.removeAll(number);
        }
        emit dataChanged(index, index, {Qt::CheckStateRole});
        return true;
    }

    QVariant headerData(int section, Qt::Orientation orientation, int role) const override
    {
        if (orientation == Qt::Vertical || role != Qt::DisplayRole)
            return {};
        switch (section) {
        case ColumnEnabled:    return Tr::tr("Enabled");
        case ColumnNonQuickUi: return Tr::tr("Only for Qt Quick UI");
        default:               return Tr::tr("Message");
        }
    }

    QHash<int, QByteArray> roleNames() const override
    {
        return AspectTable::withRoleNames(QAbstractTableModel::roleNames());
    }

    Qt::ItemFlags flags(const QModelIndex &index) const override
    {
        const Qt::ItemFlags flags = QAbstractTableModel::flags(index);
        if (index.column() == ColumnMessage)
            return flags;
        return flags | Qt::ItemIsUserCheckable;
    }

private:
    struct Row
    {
        int number = -1;
        QString message;
    };

    State *m_state = nullptr;
    QList<Row> m_rows;
};

static bool operator==(const AnalyzerMessagesModel::State &first,
                       const AnalyzerMessagesModel::State &second)
{
    return Utils::sorted(first.disabled) == Utils::sorted(second.disabled)
           && Utils::sorted(first.disabledForNonQuickUi)
                  == Utils::sorted(second.disabledForNonQuickUi);
}

class AnalyzerMessagesAspectPrivate
{
public:
    explicit AnalyzerMessagesAspectPrivate(AnalyzerMessagesAspect *aspect)
        : m_model(&m_volatileState, aspect)
    {}

    // What was applied, and what the check boxes hold now.
    AnalyzerMessagesModel::State m_state;
    AnalyzerMessagesModel::State m_volatileState;
    AnalyzerMessagesModel m_model;
};

AnalyzerMessagesAspect::AnalyzerMessagesAspect(AspectContainer *container)
    : BaseAspect(container)
    , d(new AnalyzerMessagesAspectPrivate(this))
{
    setQmlName("AnalyzerMessages");
    setToolTip(Tr::tr("Enabled checks can be disabled for non Qt Quick UI"
                      " files, but disabled checks cannot get explicitly"
                      " enabled for non Qt Quick UI files."));

    connect(&d->m_model, &QAbstractItemModel::dataChanged, this, [] {
        Utils::checkSettingsDirty();
    });
}

AnalyzerMessagesAspect::~AnalyzerMessagesAspect()
{
    delete d;
}

AspectPresentation AnalyzerMessagesAspect::presentation() const
{
    AspectPresentation p = BaseAspect::presentation();
    p.control = AspectControls::Table;
    // The rows are the known message types.
    p.allowAdding = false;
    p.allowRemoving = false;
    return p;
}

QAbstractItemModel *AnalyzerMessagesAspect::tableModel()
{
    return &d->m_model;
}

void AnalyzerMessagesAspect::resetToDefault()
{
    d->m_volatileState = {defaultDisabledMessages(), defaultDisabledMessagesNonQuickUi()};
    d->m_model.reread();
    Utils::checkSettingsDirty();
}

void AnalyzerMessagesAspect::apply()
{
    d->m_state = d->m_volatileState;
}

void AnalyzerMessagesAspect::cancel()
{
    d->m_volatileState = d->m_state;
    d->m_model.reread();
}

bool AnalyzerMessagesAspect::isDirty() const
{
    return !(d->m_volatileState == d->m_state);
}

void AnalyzerMessagesAspect::readSettings()
{
    QtcSettings &s = Utils::userSettings();
    s.beginGroup(QmlJSEditor::Constants::SETTINGS_CATEGORY_QML);
    d->m_state.disabled = disabledMessagesFromStringList(
        s.value(DISABLED_MESSAGES, disabledMessagesToStringList(defaultDisabledMessages()))
            .toStringList());
    d->m_state.disabledForNonQuickUi = disabledMessagesFromStringList(
        s.value(DISABLED_MESSAGES_NONQUICKUI,
                disabledMessagesToStringList(defaultDisabledMessagesNonQuickUi()))
            .toStringList());
    s.endGroup();

    d->m_volatileState = d->m_state;
    d->m_model.reread();
}

void AnalyzerMessagesAspect::writeSettings() const
{
    QtcSettings &s = Utils::userSettings();
    s.beginGroup(QmlJSEditor::Constants::SETTINGS_CATEGORY_QML);
    s.setValue(DISABLED_MESSAGES, disabledMessagesToStringList(d->m_state.disabled));
    s.setValue(DISABLED_MESSAGES_NONQUICKUI,
               disabledMessagesToStringList(d->m_state.disabledForNonQuickUi));
    s.endGroup();
}

class QmlJsEditingSettingsPage : public Core::IOptionsPage
{
public:
    QmlJsEditingSettingsPage()
    {
        setId(SETTINGS_PAGE);
        setDisplayName(::QmlJSEditor::Tr::tr("QML/JS Editing"));
        setCategory(Constants::SETTINGS_CATEGORY_QML);
        setSettingsProvider([] { return &settings(); });
    }
};

void setupQmlJsEditingSettings()
{
    static QmlJsEditingSettingsPage theQmlJsEditingSettingsPage;
}


} // QmlJsEditor::Internal
