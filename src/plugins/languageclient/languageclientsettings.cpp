// Copyright (C) 2018 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "languageclientsettings.h"

#include "client.h"
#include "languageclient_global.h"
#include "languageclientinterface.h"
#include "languageclientmanager.h"
#include "languageclienttr.h"

#include <coreplugin/dialogs/ioptionspage.h>
#include <coreplugin/editormanager/documentmodel.h>
#include <coreplugin/icore.h>
#include <coreplugin/idocument.h>

#include <projectexplorer/buildconfiguration.h>
#include <projectexplorer/project.h>
#include <projectexplorer/projectmanager.h>
#include <projectexplorer/projectpanelfactory.h>

#include <texteditor/plaintexteditorfactory.h>
#include <texteditor/textmark.h>

#include <utils/aspectpresentation.h>
#include <utils/shutdownguard.h>
#include <utils/algorithm.h>
#include <utils/fancylineedit.h>
#include <utils/guiutils.h>
#include <utils/layoutbuilder.h>
#include <utils/macroexpander.h>
#include <utils/mimeconstants.h>
#include <utils/pathchooser.h>
#include <utils/stringutils.h>
#include <utils/utilsicons.h>
#include <utils/variablechooser.h>

#include <QBoxLayout>
#include <QComboBox>
#include <QCompleter>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QJsonDocument>
#include <QLabel>
#include <QMimeData>
#include <QSortFilterProxyModel>

#ifdef WITH_TESTS
#include <QTest>
#endif
#include <QStyledItemDelegate>

constexpr char typeIdKey[] = "typeId";
constexpr char enabledKey[] = "enabled";
constexpr char startupBehaviorKey[] = "startupBehavior";
constexpr char settingsGroupKey[] = "LanguageClient";
constexpr char clientsKey[] = "clients";
constexpr char typedClientsKey[] = "typedClients";
constexpr char outlineSortedKey[] = "outlineSorted";
constexpr char mimeType[] = "application/language.client.setting";

using namespace ProjectExplorer;
using namespace Utils;
using namespace TextEditor;

namespace LanguageClient {

class LanguageClientSettingsModel : public QAbstractListModel
{
public:
    LanguageClientSettingsModel() = default;
    ~LanguageClientSettingsModel() override;

    // QAbstractItemModel interface
    int rowCount(const QModelIndex &/*parent*/ = QModelIndex()) const final { return m_settings.count(); }
    // The one column has no name; the widget view hid the header instead.
    QVariant headerData(int, Qt::Orientation, int) const final { return {}; }
    // What a Qt Quick view reads off a cell, which it cannot take from flags().
    QHash<int, QByteArray> roleNames() const final
    {
        return AspectTable::withRoleNames(QAbstractItemModel::roleNames());
    }
    QVariant data(const QModelIndex &index, int role) const final;
    bool removeRows(int row, int count = 1, const QModelIndex &parent = QModelIndex()) final;
    bool insertRows(int row, int count = 1, const QModelIndex &parent = QModelIndex()) final;
    bool setData(const QModelIndex &index, const QVariant &value, int role) final;
    Qt::ItemFlags flags(const QModelIndex &index) const final;
    Qt::DropActions supportedDropActions() const override { return Qt::MoveAction; }
    QStringList mimeTypes() const override { return {mimeType}; }
    QMimeData *mimeData(const QModelIndexList &indexes) const override;
    bool dropMimeData(const QMimeData *data,
                      Qt::DropAction action,
                      int row,
                      int column,
                      const QModelIndex &parent) override;

    void reset(const QList<BaseSettings *> &settings);
    QList<BaseSettings *> settings() const { return m_settings; }
    QModelIndex insertSettings(BaseSettings *settings);
    void enableSetting(const QString &id, bool enable = true);
    QList<BaseSettings *> removed() const { return m_removed; }
    BaseSettings *settingForIndex(const QModelIndex &index) const;
    QModelIndex indexForSetting(BaseSettings *setting) const;

private:
    static constexpr int idRole = Qt::UserRole + 1;
    QList<BaseSettings *> m_settings; // owned
    QList<BaseSettings *> m_removed;
};

class FilterProxy final : public QSortFilterProxyModel
{
public:
    FilterProxy(LanguageClientSettingsModel &sourceModel)
        : m_settings(sourceModel)
    {
        setSourceModel(&sourceModel);
    }

    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const final
    {
        const QModelIndex index = sourceModel()->index(sourceRow, 0, sourceParent);
        const BaseSettings *setting
            = static_cast<LanguageClientSettingsModel *>(sourceModel())->settingForIndex(index);
        return setting && setting->showInSettings();
    }

    void reset(QList<BaseSettings *> settings)
    {
        m_settings.reset(settings);
        invalidateFilter();
    }

    QModelIndex insertSettings(BaseSettings *settings)
    {
        const auto idx = m_settings.insertSettings(settings);
        invalidateFilter();
        return mapFromSource(idx);
    }

    BaseSettings *settingForIndex(const QModelIndex &index) const
    {
        return m_settings.settingForIndex(mapToSource(index));
    }

    QModelIndex indexForSetting(BaseSettings *setting) const
    {
        return mapFromSource(m_settings.indexForSetting(setting));
    }

    QList<BaseSettings *> removed() const { return m_settings.removed(); }

private:
    LanguageClientSettingsModel &m_settings;
};

// The clients, as the page lists them: each renamed and switched on in place,
// and dragged into the order they are tried in. All of that is the model's
// answer already; the aspect only says the tree may be reordered.
class ClientTreeAspect final : public BaseAspect
{
    Q_OBJECT

public:
    ClientTreeAspect(AspectContainer *container, LanguageClientSettingsModel &model)
        : BaseAspect(container)
        , m_proxy(model)
    {}

    AspectPresentation presentation() const override
    {
        AspectPresentation p = BaseAspect::presentation();
        p.control = AspectControls::Tree;
        p.allowReordering = true;
        return p;
    }

    QAbstractItemModel *tableModel() override { return &m_proxy; }
    FilterProxy &proxy() { return m_proxy; }

    // Which client the form beside the tree is about. The view says so; the
    // page reads it.
    Q_INVOKABLE void setCurrentIndex(const QModelIndex &index)
    {
        if (index == m_current)
            return;
        m_current = index;
        emit currentChanged();
    }

    QModelIndex currentIndex() const { return m_current; }
    BaseSettings *currentSetting() const { return m_proxy.settingForIndex(m_current); }

signals:
    void currentChanged();

private:
    FilterProxy m_proxy;
    QPersistentModelIndex m_current;
};

class LanguageClientSettingsPageWidget final : public AspectContainer
{
public:
    LanguageClientSettingsPageWidget(LanguageClientSettingsModel &settings,
                                     QSet<QString> &changedSettings);

    void apply() final
    {
        applyCurrentSettings();
        LanguageClientManager::applySettings();

        for (BaseSettings *setting : m_clients.proxy().removed()) {
            for (Client *client : LanguageClientManager::clientsForSetting(setting))
                LanguageClientManager::shutdownClient(client);
        }

        const int row = m_clients.currentIndex().row();
        m_clients.proxy().reset(LanguageClientManager::currentSettings());
        showCurrentSettings(m_clients.proxy().index(row, 0));
    }

    void cancel() final
    {
        m_clients.proxy().reset(LanguageClientManager::currentSettings());
        m_changedSettings.clear();
        showCurrentSettings({});
    }

private:
    void showCurrentSettings(const QModelIndex &index);
    void applyCurrentSettings();
    void addItem(const Id &clientTypeId);
    void deleteItem();

    LanguageClientSettingsModel &m_model;
    QSet<QString> &m_changedSettings;

    ClientTreeAspect m_clients{this, m_model};
    ActionAspect m_add{this};
    ActionAspect m_delete{this};
    // What the selected client asks for. Rebuilt per selection, and owned so
    // that the previous one goes when the next arrives.
    ContainerAspect m_current{this};
    BaseSettings *m_shown = nullptr;
};

static QMap<Id, ClientType> &clientTypes()
{
    static QMap<Id, ClientType> types;
    return types;
}

LanguageClientSettingsPageWidget::LanguageClientSettingsPageWidget(
    LanguageClientSettingsModel &settings, QSet<QString> &changedSettings)
    : m_model(settings)
    , m_changedSettings(changedSettings)
{
    setAutoApply(false);
    setQmlSource(QUrl("qrc:/qt/qml/QtCreator/LanguageClient/LanguageClientsPage.qml"));

    m_clients.setQmlName("Clients");

    // Adding a client is picking a kind, so the button offers rather than
    // does. The kinds are fixed once the plugins are loaded.
    m_add.setQmlName("Add");
    m_add.setActionText(Tr::tr("Add"));
    QList<AspectPresentation::Choice> kinds;
    for (const ClientType &type : clientTypes()) {
        if (!type.userAddable)
            continue;
        kinds.append({type.name, {}, true, type.id.toSetting()});
    }
    m_add.setChoices(kinds);
    m_add.setOnChoice([this](const QVariant &id) { addItem(Id::fromSetting(id)); });

    m_delete.setQmlName("Delete");
    m_delete.setActionText(Tr::tr("Delete"));
    m_delete.setAction([this] { deleteItem(); });

    m_current.setQmlName("Current");

    // Behaviour, not layout.
    connect(&m_clients, &ClientTreeAspect::currentChanged, this, [this] {
        showCurrentSettings(m_clients.currentIndex());
    });
    connect(&m_model, &LanguageClientSettingsModel::dataChanged, this,
            [](const QModelIndex &, const QModelIndex &, const QList<int> roles) {
                if (roles.contains(Qt::CheckStateRole))
                    markSettingsDirty();
            });
}

void LanguageClientSettingsPageWidget::showCurrentSettings(const QModelIndex &index)
{
    applyCurrentSettings();

    m_shown = m_clients.proxy().settingForIndex(index);
    if (!m_shown) {
        m_current.setOwnedContainer(nullptr);
        m_delete.setEnabled(false);
        return;
    }

    const auto rows = new AspectContainer;
    // An ordering container has no opinion about applying, and the default is
    // to apply at once - which on a page with Apply and Cancel is wrong.
    rows->setAutoApply(false);
    m_shown->addSettingsRows(*rows);
    m_current.setOwnedContainer(rows);
    m_delete.setEnabled(true);
}

void LanguageClientSettingsPageWidget::applyCurrentSettings()
{
    if (!m_shown)
        return;

    if (m_shown->applySettings()) {
        const QModelIndex index = m_clients.proxy().indexForSetting(m_shown);
        emit m_model.dataChanged(m_model.index(index.row(), 0), m_model.index(index.row(), 0));
    }
}

static BaseSettings *generateSettings(const Id &clientTypeId)
{
    if (auto generator = clientTypes().value(clientTypeId).generator) {
        auto settings = generator();
        settings->settingsTypeId.setValue(clientTypeId);
        return settings;
    }
    return nullptr;
}

void LanguageClientSettingsPageWidget::addItem(const Id &clientTypeId)
{
    auto newSettings = generateSettings(clientTypeId);
    QTC_ASSERT(newSettings, return);
    m_clients.setCurrentIndex(m_clients.proxy().insertSettings(newSettings));
}

void LanguageClientSettingsPageWidget::deleteItem()
{
    const QModelIndex index = m_clients.currentIndex();
    if (!index.isValid())
        return;

    // The rows being shown are the ones about to go.
    showCurrentSettings({});
    m_clients.setCurrentIndex({});
    m_clients.proxy().removeRow(index.row());
}

class LanguageClientSettingsPage : public Core::IOptionsPage
{
public:
    LanguageClientSettingsPage();

    void init();
    bool initialized() const { return m_initialized; }

    QList<BaseSettings *> settings() const;
    QList<BaseSettings *> changedSettings() const;
    void addSettings(BaseSettings *settings);
    void enableSettings(const QString &id, bool enable = true);

private:
    bool m_initialized = false;
    LanguageClientSettingsModel m_model;
    QSet<QString> m_changedSettings;
};

LanguageClientSettingsPage::LanguageClientSettingsPage()
{
    setId(Constants::LANGUAGECLIENT_SETTINGS_PAGE);
    setDisplayName(Tr::tr("General"));
    setCategory(Constants::LANGUAGECLIENT_SETTINGS_CATEGORY);
    setSettingsProvider([this] {
        static GuardedObject<LanguageClientSettingsPageWidget> theAspects(m_model,
                                                                          m_changedSettings);
        return theAspects.get();
    });
    QObject::connect(&m_model, &LanguageClientSettingsModel::dataChanged, [this](const QModelIndex &index) {
        if (BaseSettings *setting = m_model.settingForIndex(index))
            m_changedSettings << setting->id();
    });
}

void LanguageClientSettingsPage::init()
{
    m_initialized = true;
    QList<BaseSettings *> newList = LanguageClientSettings::fromSettings(Core::ICore::settings());
    m_model.reset(newList);
    qDeleteAll(newList);
}

QList<BaseSettings *> LanguageClientSettingsPage::settings() const
{
    return m_model.settings();
}

QList<BaseSettings *> LanguageClientSettingsPage::changedSettings() const
{
    QList<BaseSettings *> result;
    const QList<BaseSettings *> &all = settings();
    for (BaseSettings *setting : all) {
        if (m_changedSettings.contains(setting->id()))
            result << setting;
    }
    return result;
}

void LanguageClientSettingsPage::addSettings(BaseSettings *settings)
{
    m_model.insertSettings(settings);
    m_changedSettings << settings->id();
}

void LanguageClientSettingsPage::enableSettings(const QString &id, bool enable)
{
    m_model.enableSetting(id, enable);
}

LanguageClientSettingsModel::~LanguageClientSettingsModel()
{
    qDeleteAll(m_settings);
}

QVariant LanguageClientSettingsModel::data(const QModelIndex &index, int role) const
{
    BaseSettings *setting = settingForIndex(index);
    if (!setting)
        return QVariant();
    // A widget view reads flags() for these two; a Qt Quick view cannot, so
    // they are answered as roles as well.
    if (role == AspectTable::EditableRole)
        return AspectTable::isWritable(flags(index));
    if (role == AspectTable::CheckableRole)
        return true;
    if (role == Qt::DisplayRole)
        return setting->name();
    else if (role == Qt::CheckStateRole)
        return setting->enabled() ? Qt::Checked : Qt::Unchecked;
    else if (role == idRole)
        return setting->id();
    return QVariant();
}

bool LanguageClientSettingsModel::removeRows(int row, int count, const QModelIndex &parent)
{
    if (row >= int(m_settings.size()))
        return false;
    const int end = qMin(row + count - 1, int(m_settings.size()) - 1);
    beginRemoveRows(parent, row, end);
    for (auto i = end; i >= row; --i)
        m_removed << m_settings.takeAt(i);
    endRemoveRows();
    return true;
}

bool LanguageClientSettingsModel::insertRows(int row, int count, const QModelIndex &parent)
{
    if (row > m_settings.size() || row < 0)
        return false;
    beginInsertRows(parent, row, row + count - 1);
    for (int i = 0; i < count; ++i)
        m_settings.insert(row + i, new StdIOSettings());
    endInsertRows();
    return true;
}

bool LanguageClientSettingsModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    BaseSettings *setting = settingForIndex(index);
    if (!setting || role != Qt::CheckStateRole)
        return false;

    if (setting->enabled() != value.toBool()) {
        setting->enabled.setValue(value.toBool());
        emit dataChanged(index, index, { Qt::CheckStateRole });
    }
    return true;
}

Qt::ItemFlags LanguageClientSettingsModel::flags(const QModelIndex &index) const
{
    const Qt::ItemFlags dragndropFlags = index.isValid() ? Qt::ItemIsDragEnabled
                                                         : Qt::ItemIsDropEnabled;
    return Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsUserCheckable | dragndropFlags;
}

QMimeData *LanguageClientSettingsModel::mimeData(const QModelIndexList &indexes) const
{
    QTC_ASSERT(indexes.count() == 1, return nullptr);

    QMimeData *mimeData = new QMimeData;
    QByteArray encodedData;

    QDataStream stream(&encodedData, QIODevice::WriteOnly);

    for (const QModelIndex &index : indexes) {
        if (index.isValid())
            stream << data(index, idRole).toString();
    }

    mimeData->setData(mimeType, indexes.first().data(idRole).toString().toUtf8());
    return mimeData;
}

bool LanguageClientSettingsModel::dropMimeData(
    const QMimeData *data, Qt::DropAction action, int row, int column, const QModelIndex &parent)
{
    if (!canDropMimeData(data, action, row, column, parent))
        return false;

    if (action == Qt::IgnoreAction)
        return true;

    const QString id = QString::fromUtf8(data->data(mimeType));
    auto setting = findOrDefault(m_settings, [id](const BaseSettings *setting) {
        return setting->id() == id;
    });
    if (!setting)
        return false;

    if (row == -1)
        row = parent.isValid() ? parent.row() : rowCount(QModelIndex());

    beginInsertRows(parent, row, row);
    m_settings.insert(row, setting->copy());
    endInsertRows();

    return true;
}

void LanguageClientSettingsModel::reset(const QList<BaseSettings *> &settings)
{
    beginResetModel();
    qDeleteAll(m_settings);
    qDeleteAll(m_removed);
    m_removed.clear();
    m_settings = Utils::transform(settings, [](const BaseSettings *other) { return other->copy(); });
    endResetModel();
}

QModelIndex LanguageClientSettingsModel::insertSettings(BaseSettings *settings)
{
    int row = rowCount();
    beginInsertRows(QModelIndex(), row, row);
    m_settings.insert(row, settings);
    endInsertRows();
    return createIndex(row, 0, settings);
}

void LanguageClientSettingsModel::enableSetting(const QString &id, bool enable)
{
    BaseSettings *setting = LanguageClientSettings::settingById(m_settings, id);
    if (!setting)
        return;
    if (setting->enabled() == enable)
        return;
    setting->enabled.setValue(enable);
    const QModelIndex &index = indexForSetting(setting);
    if (index.isValid())
        emit dataChanged(index, index, {Qt::CheckStateRole});
}

BaseSettings *LanguageClientSettingsModel::settingForIndex(const QModelIndex &index) const
{
    if (!index.isValid() || index.row() >= m_settings.size())
        return nullptr;
    return m_settings[index.row()];
}

QModelIndex LanguageClientSettingsModel::indexForSetting(BaseSettings *setting) const
{
    const int index = m_settings.indexOf(setting);
    return index < 0 ? QModelIndex() : createIndex(index, 0, setting);
}

// BaseSettings

BaseSettings::BaseSettings()
{
    name.setSettingsKey("name");
    name.setDefaultValue("New Language Server");
    name.setLabelText(Tr::tr("Name:"));
    name.setDisplayStyle(StringAspect::LineEditDisplay);

    filePattern.setSettingsKey("filePattern");
    filePattern.setLabelText(Tr::tr("File pattern:"));
    filePattern.setDisplayStyle(StringAspect::LineEditDisplay);
    filePattern.setPlaceHolderText(Tr::tr("File pattern"));
    filePattern.setToolTip(
        Tr::tr("List of file patterns.\nExample: *.cpp%1*.h").arg(filterSeparator));

    id.setSettingsKey("id");
    id.setDefaultValue(QUuid::createUuid().toString());

    settingsTypeId.setSettingsKey(typeIdKey);

    startBehavior.setSettingsKey(startupBehaviorKey);
    startBehavior.setDisplayStyle(SelectionAspect::DisplayStyle::ComboBox);
    startBehavior.setLabelText(Tr::tr("Startup behavior:"));
    startBehavior.addOption(Tr::tr("Always On"));
    startBehavior.addOption(Tr::tr("Requires an Open File"));
    startBehavior.addOption(Tr::tr("Start Server per Project"));
    startBehavior.setDefaultValue(RequiresFile);

    showInSettings.setDefaultValue(true);

    activatable.setDefaultValue(true);

    mimeTypes.setSettingsKey("mimeType");

    excludeMimeTypes.setSettingsKey("excludeMimeType");

    initializationOptions.setSettingsKey("initializationOptions");
    initializationOptions.setLabelText(Tr::tr("Initialization options:"));
    initializationOptions.setDisplayStyle(StringAspect::LineEditDisplay);
    initializationOptions.setValidationFunction([](const QString &text) -> Result<> {
        const QString value = globalMacroExpander()->expand(text);

        if (value.isEmpty())
            return ResultOk;

        QJsonParseError parseInfo;
        const QJsonDocument json = QJsonDocument::fromJson(value.toUtf8(), &parseInfo);

        if (json.isNull()) {
            return ResultError(Tr::tr("Failed to parse JSON at %1: %2")
                                   .arg(parseInfo.offset)
                                   .arg(parseInfo.errorString()));
        }
        return ResultOk;
    });
    initializationOptions.setPlaceHolderText(
        Tr::tr(
            "Language server-specific JSON to pass via "
            "\"initializationOptions\" field of \"initialize\" "
            "request."));

    configuration.setSettingsKey("configuration");

    enabled.setSettingsKey(enabledKey);
    enabled.setDefaultValue(true);
    enabled.setLabelText(Tr::tr("Enabled:"));
}

QJsonObject BaseSettings::initializationOptionsAsJson() const
{
    return QJsonDocument::fromJson(initializationOptions().toUtf8()).object();
}

QJsonValue BaseSettings::configurationAsJson() const
{
    const QJsonDocument document = QJsonDocument::fromJson(configuration().toUtf8());
    if (document.isArray())
        return document.array();
    if (document.isObject())
        return document.object();
    return {};
}

bool BaseSettings::applySettings()
{
    const bool changed = isDirty();
    AspectContainer::apply();
    return changed;
}

void BaseSettings::addSettingsRows(AspectContainer &rows)
{
    rows.registerAspect(&name);
    rows.registerAspect(&mimeTypes);
    rows.registerAspect(&filePattern);
    rows.registerAspect(&startBehavior);
    rows.registerAspect(&initializationOptions);
}

BaseSettings *BaseSettings::copy() const
{
    BaseSettings *other = create();
    Store store;
    toMap(store);
    other->fromMap(store);
    // some members are not stored in the map, copy them manually, reeavluate whether those settings
    // need to be an aspect at all after full aspectification of the lsp settings
    other->showInSettings.setValue(showInSettings());
    other->activatable.setValue(activatable());
    return other;
}

bool BaseSettings::isValid() const
{
    return !name().isEmpty();
}

bool BaseSettings::isValidOnBuildConfiguration(BuildConfiguration *) const
{
    return isValid();
}

Client *BaseSettings::createClient() const
{
    return createClient(static_cast<BuildConfiguration *>(nullptr));
}

bool BaseSettings::isEnabledOnProject(Project *project) const
{
    if (project) {
        LanguageClient::ProjectSettings settings(project);
        if (settings.enabledSettings().contains(id()))
            return true;
        if (settings.disabledSettings().contains(id()))
            return false;
    }
    return enabled();
}

const LanguageFilter BaseSettings::languageFilter() const
{
    return LanguageFilter{mimeTypes(), filePattern().split(filterSeparator), excludeMimeTypes()};
}

Client *BaseSettings::createClient(BuildConfiguration *bc) const
{
    if (!isValidOnBuildConfiguration(bc))
        return nullptr;
    if (bc && !isEnabledOnProject(bc->project()))
        return nullptr;
    BaseClientInterface *interface = createInterface(bc);
    QTC_ASSERT(interface, return nullptr);
    auto *client = createClient(interface);
    QTC_ASSERT(client, return nullptr);

    if (client->name().isEmpty())
        client->setName(name());

    client->setSupportedLanguage(languageFilter());
    client->setInitializationOptions(initializationOptionsAsJson());
    client->setActivatable(activatable());
    client->setCurrentBuildConfiguration(bc);
    client->updateConfiguration(configurationAsJson());
    return client;
}

Client *BaseSettings::createClient(BaseClientInterface *interface) const
{
    return new Client(interface);
}

static LanguageClientSettingsPage &settingsPage()
{
    static LanguageClientSettingsPage settingsPage;
    return settingsPage;
}

void LanguageClientSettings::init()
{
    settingsPage().init();
    LanguageClientManager::applySettings();
}

bool LanguageClientSettings::initialized()
{
    return settingsPage().initialized();
}

QList<Store> LanguageClientSettings::storesBySettingsType(Id settingsTypeId)
{
    QList<Store> result;

    QtcSettings *settingsIn = Core::ICore::settings();
    settingsIn->beginGroup(settingsGroupKey);

    for (const QVariantList &varList :
         {settingsIn->value(clientsKey).toList(), settingsIn->value(typedClientsKey).toList()}) {
        for (const QVariant &var : varList) {
            const Store store = storeFromVariant(var);
            if (settingsTypeId == Id::fromSetting(store.value(typeIdKey)))
                result << store;
        }
    }

    settingsIn->endGroup();

    return result;
}

QList<BaseSettings *> LanguageClientSettings::fromSettings(QtcSettings *settingsIn)
{
    settingsIn->beginGroup(settingsGroupKey);
    QList<BaseSettings *> result;

    for (const QVariantList &varList :
         {settingsIn->value(clientsKey).toList(), settingsIn->value(typedClientsKey).toList()}) {
        for (const QVariant &var : varList) {
            const Store map = storeFromVariant(var);
            Id typeId = Id::fromSetting(map.value(typeIdKey));
            if (!typeId.isValid())
                typeId = Constants::LANGUAGECLIENT_STDIO_SETTINGS_ID;
            if (BaseSettings *settings = generateSettings(typeId)) {
                settings->fromMap(map);
                result << settings;
            }
        }
    }

    settingsIn->endGroup();
    return result;
}

QList<BaseSettings *> LanguageClientSettings::pageSettings()
{
    return settingsPage().settings();
}

QList<BaseSettings *> LanguageClientSettings::changedSettings()
{
    return settingsPage().changedSettings();
}

BaseSettings *LanguageClientSettings::settingById(QList<BaseSettings *> settings, const QString &id)
{
    return Utils::findOrDefault(settings, [id](const BaseSettings *setting) {
        return setting->id() == id;
    });
}

void LanguageClientSettings::registerClientType(const ClientType &type)
{
    QTC_ASSERT(!clientTypes().contains(type.id), return);
    clientTypes()[type.id] = type;
}

void LanguageClientSettings::addSettings(BaseSettings *settings)
{
    settingsPage().addSettings(settings);
}

void LanguageClientSettings::enableSettings(const QString &id, bool enable)
{
    settingsPage().enableSettings(id, enable);
}

void LanguageClientSettings::toSettings(QtcSettings *settings,
                                        const QList<BaseSettings *> &languageClientSettings)
{
    settings->beginGroup(settingsGroupKey);
    auto transform = [](const QList<BaseSettings *> &settings) {
        return Utils::transform(settings, [](const BaseSettings *setting) {
            Store store;
            setting->toMap(store);
            return variantFromStore(store);
        });
    };
    auto isStdioSetting = [](const BaseSettings *settings) {
        return settings->settingsTypeId() == Id(Constants::LANGUAGECLIENT_STDIO_SETTINGS_ID);
    };
    auto [stdioSettings, typedSettings] = Utils::partition(languageClientSettings, isStdioSetting);
    settings->setValue(clientsKey, transform(stdioSettings));

    // write back typed settings for unregistered client types
    QVariantList typedSettingsVariant;
    for (const QVariant &var : settings->value(typedClientsKey).toList()) {
        const Store map = storeFromVariant(var);
        const Id typeId = Id::fromSetting(map.value(typeIdKey));
        const QString id = map.value("id").toString();
        if (typeId.isValid() && !clientTypes().contains(typeId) && settingById(typedSettings, id))
            typedSettingsVariant << var;
    }

    typedSettingsVariant << transform(typedSettings);
    settings->setValue(typedClientsKey, typedSettingsVariant);
    settings->endGroup();
}

bool LanguageClientSettings::outlineComboBoxIsSorted()
{
    auto settings = Core::ICore::settings();
    settings->beginGroup(settingsGroupKey);
    bool sorted = settings->value(outlineSortedKey).toBool();
    settings->endGroup();
    return sorted;
}

void LanguageClientSettings::setOutlineComboBoxSorted(bool sorted)
{
    auto settings = Core::ICore::settings();
    settings->beginGroup(settingsGroupKey);
    settings->setValue(outlineSortedKey, sorted);
    settings->endGroup();
}

// StdIOSettings

StdIOSettings::StdIOSettings()
{
    executable.setSettingsKey("executable");
    executable.setExpectedKind(PathChooserKind::ExistingCommand);
    executable.setLabelText(Tr::tr("Executable:"));

    arguments.setSettingsKey("arguments");
    arguments.setDisplayStyle(StringAspect::LineEditDisplay);
    arguments.setLabelText(Tr::tr("Arguments:"));
}

StdIOSettings::~StdIOSettings() = default;

void StdIOSettings::addSettingsRows(AspectContainer &rows)
{
    BaseSettings::addSettingsRows(rows);
    rows.registerAspect(&executable);
    rows.registerAspect(&arguments);
}

bool StdIOSettings::isValid() const
{
    return BaseSettings::isValid() && !executable().isEmpty();
}

CommandLine StdIOSettings::command() const
{
    return CommandLine(executable(), arguments(), CommandLine::Raw);
}

BaseClientInterface *StdIOSettings::createInterface(BuildConfiguration *bc) const
{
    auto interface = new StdIOClientInterface;
    interface->setCommandLine(command());
    if (bc)
        interface->setWorkingDirectory(bc->project()->projectDirectory());
    return interface;
}

class JsonTreeItemDelegate : public QStyledItemDelegate
{
public:
    QString displayText(const QVariant &value, const QLocale &) const override
    {
        QString result = value.toString();
        if (result.size() == 1) {
            switch (result.at(0).toLatin1()) {
            case '\n':
                return QString("\\n");
            case '\t':
                return QString("\\t");
            case '\r':
                return QString("\\r");
            }
        }
        return result;
    }
};

static bool inheritsAnyMimeType(const MimeType &mimeType, const QStringList &mimeTypes)
{
    return Utils::anyOf(mimeTypes, [mimeType](const QString &type) {
        return mimeType.inherits(type);
    });
}

bool LanguageFilter::isSupported(const FilePath &filePath, const QString &mimeTypeName) const
{
    if (!mimeTypeName.isEmpty() && (!excludeMimeTypes.isEmpty() || !mimeTypes.isEmpty())) {
        const MimeType mimeType = Utils::mimeTypeForName(mimeTypeName);
        if (inheritsAnyMimeType(mimeType, excludeMimeTypes))
            return false;
        if (inheritsAnyMimeType(mimeType, mimeTypes))
            return true;
    }
    if (filePattern.isEmpty() && filePath.isEmpty())
        return mimeTypes.isEmpty();
    auto regexps = Utils::transform(filePattern, [](const QString &pattern){
        return QRegularExpression(QRegularExpression::wildcardToRegularExpression(pattern),
                                  QRegularExpression::CaseInsensitiveOption);
    });
    return Utils::anyOf(regexps, [filePath](const QRegularExpression &reg){
        return reg.match(filePath.toUrlishString()).hasMatch()
                || reg.match(filePath.fileName()).hasMatch();
    });
}

bool LanguageFilter::isSupported(const Core::IDocument *document) const
{
    return isSupported(document->filePath(), document->mimeType());
}

bool LanguageFilter::operator==(const LanguageFilter &other) const
{
    return this->filePattern == other.filePattern && this->mimeTypes == other.mimeTypes
           && this->excludeMimeTypes == other.excludeMimeTypes;
}

BaseTextEditor *createJsonEditor(QObject *parent)
{
    using namespace Text;
    // The plain text editor, asked for by name. This used to walk the
    // factories that claim a .json and keep the first whose editor is a
    // BaseTextEditor - which, since JSON moved to the Qt Quick view, means
    // building a Qt Quick editor and the Qt Quick plain text editor behind it
    // and throwing both away before landing here anyway. The box is
    // configured below whatever it came from, so ask for it directly.
    BaseTextEditor * const textEditor = createPlainTextEditor();
    QTC_ASSERT(textEditor, return nullptr);
    textEditor->setParent(parent);

    TextDocument *document = textEditor->textDocument();
    TextEditorWidget *widget = textEditor->editorWidget();
    widget->configureGenericHighlighter(mimeTypeForName(Utils::Constants::JSON_MIMETYPE));
    widget->setLineNumbersVisible(false);
    widget->setRevisionsVisible(false);
    widget->setCodeFoldingSupported(false);
    QObject::connect(document, &TextDocument::contentsChanged, widget, [document]() {
        const Id jsonMarkId("LanguageClient.JsonTextMarkId");
        const TextMarks marks = document->marks();
        for (TextMark *mark : marks) {
            if (mark->category().id == jsonMarkId)
                delete mark;
        }
        const QString content = document->plainText().trimmed();
        if (content.isEmpty())
            return;
        QJsonParseError error;
        QJsonDocument::fromJson(content.toUtf8(), &error);
        if (error.error == QJsonParseError::NoError)
            return;
        const Position pos = Position::fromPositionInDocument(document->document(), error.offset);
        if (!pos.isValid())
            return;
        auto mark = new TextMark(
            FilePath(), pos.line, {::LanguageClient::Tr::tr("JSON Error"), jsonMarkId});
        mark->setLineAnnotation(error.errorString());
        mark->setColor(Theme::CodeModel_Error_TextMarkColor);
        mark->setIcon(Icons::CODEMODEL_ERROR.icon());
        document->addMark(mark);
    });
    return textEditor;
}

constexpr const char projectSettingsId[] = "LanguageClient.ProjectSettings";
constexpr const char enabledSettingsId[] = "LanguageClient.EnabledSettings";
constexpr const char disabledSettingsId[] = "LanguageClient.DisabledSettings";

ProjectSettings::ProjectSettings(ProjectExplorer::Project *project)
    : m_project(project)
{
    QTC_ASSERT(project, return);
    m_json = m_project->namedSettings(projectSettingsId).toByteArray();
    m_enabledSettings = m_project->namedSettings(enabledSettingsId).toStringList();
    m_disabledSettings = m_project->namedSettings(disabledSettingsId).toStringList();
}

QJsonValue ProjectSettings::workspaceConfiguration() const
{
    const auto doc = QJsonDocument::fromJson(m_json);
    if (doc.isObject())
        return doc.object();
    if (doc.isArray())
        return doc.array();
    return {};
}

QByteArray ProjectSettings::json() const
{
    return m_json;
}

void ProjectSettings::setJson(const QByteArray &json)
{
    QTC_ASSERT(m_project, return);
    const QJsonValue oldConfig = workspaceConfiguration();
    m_json = json;
    m_project->setNamedSettings(projectSettingsId, m_json);
    const QJsonValue newConfig = workspaceConfiguration();
    if (oldConfig != newConfig)
        LanguageClientManager::updateWorkspaceConfiguration(m_project, newConfig);
}

void ProjectSettings::enableSetting(const QString &id)
{
    QTC_ASSERT(m_project, return);
    if (m_disabledSettings.removeAll(id) > 0)
        m_project->setNamedSettings(disabledSettingsId, m_disabledSettings);
    if (m_enabledSettings.contains(id))
        return;
    m_enabledSettings << id;
    m_project->setNamedSettings(enabledSettingsId, m_enabledSettings);
    LanguageClientManager::applySettings(id);
}

void ProjectSettings::disableSetting(const QString &id)
{
    QTC_ASSERT(m_project, return);
    if (m_enabledSettings.removeAll(id) > 0)
        m_project->setNamedSettings(enabledSettingsId, m_enabledSettings);
    if (m_disabledSettings.contains(id))
        return;
    m_disabledSettings << id;
    m_project->setNamedSettings(disabledSettingsId, m_disabledSettings);
    LanguageClientManager::applySettings(id);
}

void ProjectSettings::clearOverride(const QString &id)
{
    QTC_ASSERT(m_project, return);
    const bool changedEnabled = m_enabledSettings.removeAll(id) > 0;
    if (changedEnabled)
        m_project->setNamedSettings(enabledSettingsId, m_enabledSettings);
    const bool changedDisabled = m_disabledSettings.removeAll(id) > 0;
    if (changedDisabled)
        m_project->setNamedSettings(disabledSettingsId, m_disabledSettings);
    if (changedEnabled || changedDisabled)
        LanguageClientManager::applySettings(id);
}

QStringList ProjectSettings::enabledSettings()
{
    return m_enabledSettings;
}

QStringList ProjectSettings::disabledSettings()
{
    return m_disabledSettings;
}

// What the panel shows. A client that has project settings of its own hands
// over a container rather than drawing into a layout, so the panel shows
// whatever it is given without knowing what any of them are.
class LanguageClientProjectPanel final : public Utils::AspectContainer
{
public:
    explicit LanguageClientProjectPanel(Project *project)
        : m_settings(project)
    {
        // Before registering: insertAspect() forces the container's own
        // auto-apply onto what it takes in.
        setAutoApply(true);
        setQmlSource(
            QUrl("qrc:/qt/qml/QtCreator/LanguageClient/LanguageClientProjectPanel.qml"));

        m_globalLink.setQmlName("GlobalLink");
        m_globalLink.setTextFormat(Utils::AspectControls::TextFormat::RichText);
        m_globalLink.setText("<a href=\"page\">" + Tr::tr("Global settings") + "</a>");
        connect(&m_globalLink, &Utils::TextDisplay::linkActivated, this, [] {
            Core::ICore::showSettings(Constants::LANGUAGECLIENT_SETTINGS_PAGE);
        });
        registerAspect(&m_globalLink);

        // One row per language server that needs a project, saying whether this
        // project turns it on, off, or leaves it to the global settings.
        m_overrides.setQmlName("Overrides");
        m_overrides.setLabelText(Tr::tr("Project Specific Language Servers"));
        for (BaseSettings * const settings : LanguageClientSettings::pageSettings()) {
            if (settings->startBehavior() != BaseSettings::RequiresProject)
                continue;
            auto choice = new Utils::SelectionAspect;
            choice->setLabelText(settings->name());
            choice->setDisplayStyle(Utils::SelectionAspect::DisplayStyle::ComboBox);
            choice->addOption(Tr::tr("Use Global Settings"));
            choice->addOption(Tr::tr("Enabled"));
            choice->addOption(Tr::tr("Disabled"));
            if (m_settings.enabledSettings().contains(settings->id()))
                choice->setValue(1);
            else if (m_settings.disabledSettings().contains(settings->id()))
                choice->setValue(2);
            else
                choice->setValue(0);
            choice->addOnChanged(this, [this, choice, id = settings->id()] {
                switch (choice->value()) {
                case 1: m_settings.enableSetting(id); break;
                case 2: m_settings.disableSetting(id); break;
                default: m_settings.clearOverride(id); break;
                }
            });
            m_overrides.registerAspect(choice, /*takeOwnership=*/true);
        }
        registerAspect(&m_overrides);

        m_workspaceNote.setQmlName("WorkspaceNote");
        m_workspaceNote.setWordWrap(true);
        m_workspaceNote.setText(Tr::tr(
            "Additional JSON configuration sent to all running language servers for this "
            "project.\nSee the documentation of the specific language server for valid "
            "settings."));
        registerAspect(&m_workspaceNote);

        m_json.setQmlName("Json");
        m_json.setDisplayStyle(Utils::StringAspect::TextEditDisplay);
        m_json.setValue(QString::fromUtf8(m_settings.json()));
        m_json.addOnChanged(this, [this] { m_settings.setJson(m_json().toUtf8()); });
        registerAspect(&m_json);

        // Whatever the clients add. No box of its own: each of them brings its
        // own title, and a box around the lot would be a group of groups.
        m_clientSettings.setQmlName("ClientSettings");
        m_clientSettings.setFlattened(true);
        for (BaseSettings * const settings : LanguageClientSettings::pageSettings()) {
            if (settings->startBehavior() != BaseSettings::RequiresProject)
                continue;
            if (Utils::AspectContainer * const own = settings->projectSpecificSettings(project))
                m_clientSettings.registerAspect(own, /*takeOwnership=*/true);
        }
        registerAspect(&m_clientSettings);
    }

    static Utils::Key extraDataKey() { return "LanguageClientProjectPanel"; }

private:
    ProjectSettings m_settings;
    Utils::TextDisplay m_globalLink;
    Utils::AspectContainer m_overrides;
    Utils::TextDisplay m_workspaceNote;
    Utils::StringAspect m_json;
    Utils::AspectContainer m_clientSettings;
};

static LanguageClientProjectPanel *languageClientProjectPanel(Project *project)
{
    const Utils::Key key = LanguageClientProjectPanel::extraDataKey();
    QVariant v = project->extraData(key);
    if (v.isNull()) {
        v = QVariant::fromValue(new LanguageClientProjectPanel(project));
        project->setExtraData(key, v);
    }
    return v.value<LanguageClientProjectPanel *>();
}

class LanguageClientProjectPanelFactory : public ProjectPanelFactory
{
public:
    LanguageClientProjectPanelFactory()
    {
        setPriority(35);
        setDisplayName(Tr::tr("Language Server"));
        setId(Constants::LANGUAGECLIENT_SETTINGS_PANEL);
        setSettingsProvider([](Project *project) {
            return languageClientProjectPanel(project);
        });
    }
};

void setupLanguageClientProjectPanel()
{
    static LanguageClientProjectPanelFactory theLanguageClientProjectPanelFactory;
}

#ifdef WITH_TESTS
class LanguageClientSettingsPageTest final : public QObject
{
    Q_OBJECT

private slots:
    void testTheRowsAreTheClientsOwnAndFollowTheSelection()
    {
        // The page draws whatever the current client asks for and knows about
        // no kind in particular. What each kind asks is its own list of
        // aspects, which is what createSettingsWidget() used to build by hand.
        LanguageClientSettingsModel model;
        QSet<QString> changed;
        LanguageClientSettingsPageWidget page(model, changed);

        auto clients = page.aspect<ClientTreeAspect>();
        QVERIFY(clients);
        auto current = page.aspect<ContainerAspect>();
        QVERIFY(current);
        // Nothing is selected to start with, so there is nothing to draw.
        QVERIFY(!current->container());

        StdIOSettings one;
        one.name.setValue("First");
        one.showInSettings.setValue(true);
        StdIOSettings two;
        two.name.setValue("Second");
        two.showInSettings.setValue(true);
        // reset() copies, so the settings the page shows are the model's own.
        clients->proxy().reset({&one, &two});
        QCOMPARE(clients->proxy().rowCount(), 2);

        const auto shown = [clients](int row) {
            return static_cast<StdIOSettings *>(
                clients->proxy().settingForIndex(clients->proxy().index(row, 0)));
        };
        StdIOSettings * const first = shown(0);
        StdIOSettings * const second = shown(1);
        QVERIFY(first);
        QVERIFY(second);

        clients->setCurrentIndex(clients->proxy().index(0, 0));
        AspectContainer * const firstRows = current->container();
        QVERIFY(firstRows);
        // A stdio client shows what every client does and its own two on top.
        QVERIFY(firstRows->aspects().contains(&first->name));
        QVERIFY(firstRows->aspects().contains(&first->executable));
        QVERIFY(firstRows->aspects().contains(&first->arguments));
        QVERIFY(!firstRows->aspects().contains(&second->name));

        // Listing them must not turn them into aspects that apply as they are
        // typed: this page has Apply and Cancel.
        for (BaseAspect * const row : firstRows->aspects())
            QVERIFY2(!row->isAutoApply(), qPrintable(row->labelText()));

        clients->setCurrentIndex(clients->proxy().index(1, 0));
        AspectContainer * const secondRows = current->container();
        QVERIFY(secondRows);
        QVERIFY(secondRows != firstRows);
        QVERIFY(secondRows->aspects().contains(&second->name));
        QVERIFY(!secondRows->aspects().contains(&first->name));

        // The rows go when the model's settings do, so the page holds no
        // pointer into them afterwards.
        clients->setCurrentIndex({});
        QVERIFY(!current->container());
        clients->proxy().reset({});
    }

    void testAddOffersOneEntryPerKindThatCanBeAdded()
    {
        // Adding a client is picking a kind, so the button offers rather than
        // does - what a QMenu built beside the button could not be asked.
        LanguageClientSettingsModel model;
        QSet<QString> changed;
        LanguageClientSettingsPageWidget page(model, changed);

        auto add = page.aspect<ActionAspect>();
        QVERIFY(add);
        QCOMPARE(add->qmlName(), QString("Add"));

        int addable = 0;
        for (const ClientType &type : clientTypes()) {
            if (type.userAddable)
                ++addable;
        }
        if (addable == 0)
            QSKIP("No client kind here can be added by hand");
        QCOMPARE(add->presentation().choices.size(), addable);
    }

    void testTheMimeTypesReadAsTheirOwnSummary()
    {
        // Several hundred MIME types cannot be listed in place, so the row is
        // a summary of what was picked plus the dialog that picks it. What the
        // summary says is the aspect's to answer: both backends draw
        // displayText() and neither knows what a MIME type is.
        StdIOSettings client;
        QCOMPARE(client.mimeTypes.presentation().control,
                 Utils::AspectControls::TextWithAction);
        QVERIFY(!client.mimeTypes.presentation().actionText.isEmpty());
        QVERIFY(client.mimeTypes.displayText().isEmpty());

        // The volatile value, not the applied one: on this page, which has
        // Apply and Cancel, the summary has to show what is about to be
        // applied. It used to be whatever text the label happened to hold,
        // which is also where the value was read back from.
        client.mimeTypes.setAutoApply(false);
        client.mimeTypes.setVolatileValue({"text/x-c++src", "text/x-chdr"});
        QCOMPARE(client.mimeTypes.displayText(), QString("text/x-c++src;text/x-chdr"));
        QVERIFY(client.mimeTypes().isEmpty());
    }
};

QObject *createLanguageClientSettingsPageTest()
{
    return new LanguageClientSettingsPageTest;
}
#endif // WITH_TESTS

} // namespace LanguageClient

#include "languageclientsettings.moc"
