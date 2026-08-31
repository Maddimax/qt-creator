// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "pluginview.h"

#include "extensionsystemtr.h"
#include "pluginenabling.h"
#include "pluginmanager.h"
#include "pluginspec.h"

#include <utils/algorithm.h>
#include <utils/aspects.h>
#include <utils/aspectwidgets.h>
#include <utils/categorysortfiltermodel.h>
#include <utils/utilsicons.h>
#include <utils/qtcassert.h>

#include <QDebug>
#include <QDir>
#include <QGridLayout>
#include <QItemSelectionModel>
#include <QMessageBox>
#include <QSet>

/*!
    \class ExtensionSystem::PluginView
    \inheaderfile extensionsystem/pluginview.h
    \inmodule QtCreator

    \brief The PluginView class implements a widget that shows a list of all
    plugins and their state.

    This class can be embedded for example in a dialog in the application that
    uses the plugin manager.
    The class also provides notifications for interaction with the list.

    \sa ExtensionSystem::PluginDetailsView
    \sa ExtensionSystem::PluginErrorView
*/

/*!
    \fn void ExtensionSystem::PluginView::currentPluginChanged(ExtensionSystem::PluginSpec *spec)
    The current selection in the plugin list has changed to the
    plugin corresponding to \a spec.
*/

/*!
    \fn void ExtensionSystem::PluginView::pluginActivated(ExtensionSystem::PluginSpec *spec)
    The plugin list entry corresponding to \a spec has been activated,
    for example by a double-click.
*/

/*!
    \fn void ExtensionSystem::PluginView::pluginsChanged(const QSet<ExtensionSystem::PluginSpec *> &spec, bool enabled)
    The value of \a enabled for the plugin list entry corresponding to \a spec
    changed.
*/

using namespace Utils;

namespace ExtensionSystem {

namespace Internal {

enum Columns { NameColumn, LoadedColumn, VersionColumn, VendorColumn, };

enum IconIndex { OkIcon, ErrorIcon, NotLoadedIcon };

static const int SortRole = Qt::UserRole + 1;

static QIcon icon(IconIndex icon)
{
    switch (icon) {
    case OkIcon:
        return Utils::Icons::OK.icon();
    case ErrorIcon:
        return Utils::Icons::BROKEN.icon();
    default:
    case NotLoadedIcon:
        return Utils::Icons::NOTLOADED.icon();
    }
}

class PluginItem : public TreeItem
{
public:
    PluginItem(PluginSpec *spec, PluginData *data)
        : m_spec(spec), m_data(data)
    {}

    QVariant data(int column, int role) const override
    {
        switch (column) {
        case NameColumn: {
            const QString displayName = m_spec->displayName();
            if (role == Qt::DisplayRole) {
                QStringList decoration;
                if (m_spec->isDeprecated())
                    decoration.append(Tr::tr("deprecated"));
                if (m_spec->isExperimental())
                    decoration.append(Tr::tr("experimental"));

                if (decoration.isEmpty())
                    return displayName;
                return QString::fromLatin1("%1 (%2)")
                    .arg(displayName, decoration.join(QLatin1Char(',')));
            }
            if (role == SortRole)
                return displayName;
            if (role == Qt::ToolTipRole) {
                QString toolTip;
                if (!m_spec->isAvailableForHostPlatform())
                    toolTip = Tr::tr("Path: %1\nPlugin is not available on this platform.");
                else if (m_spec->isEnabledIndirectly())
                    toolTip = Tr::tr(
                        "Path: %1\nPlugin is enabled as dependency of an enabled plugin.");
                else if (m_spec->isForceEnabled())
                    toolTip = Tr::tr("Path: %1\nPlugin is enabled by command line argument.");
                else if (m_spec->isForceDisabled())
                    toolTip = Tr::tr("Path: %1\nPlugin is disabled by command line argument.");
                else
                    toolTip = Tr::tr("Path: %1");
                return toolTip.arg(m_spec->filePath().toUserOutput());
            }
            if (role == Qt::DecorationRole) {
                bool ok = !m_spec->hasError();
                QIcon i = icon(ok ? OkIcon : ErrorIcon);
                if (ok && m_spec->state() != PluginSpec::Running)
                    i = icon(NotLoadedIcon);
                return i;
            }
            break;
        }

        case LoadedColumn:
            if (!m_spec->isAvailableForHostPlatform()) {
                if (role == Qt::CheckStateRole || role == SortRole)
                    return Qt::Unchecked;
                if (role == Qt::ToolTipRole)
                    return Tr::tr("Plugin is not available on this platform.");
            } else if (m_spec->isRequired()) {
                if (role == Qt::CheckStateRole || role == SortRole)
                    return Qt::Checked;
                if (role == Qt::ToolTipRole)
                    return Tr::tr("Plugin is required.");
            } else {
                if (role == Qt::CheckStateRole || role == SortRole)
                    return m_spec->isEnabledBySettings() ? Qt::Checked : Qt::Unchecked;
                if (role == Qt::ToolTipRole)
                    return Tr::tr("Load on startup");
            }
            break;

        case VersionColumn:
            if (role == Qt::DisplayRole || role == SortRole)
                return QString::fromLatin1("%1 (%2)").arg(m_spec->version(), m_spec->compatVersion());
            break;

        case VendorColumn:
            if (role == Qt::DisplayRole || role == SortRole)
                return m_spec->vendor();
            break;
        }

        return quickRole(column, role);
    }

    bool setData(int column, const QVariant &data, int role) override
    {
        if (column == LoadedColumn && role == Qt::CheckStateRole)
            return m_data->setPluginsEnabled({m_spec}, data.toBool());
        return false;
    }

    bool isEnabled() const
    {
        return m_spec->isAvailableForHostPlatform() && !m_spec->isRequired();
    }

    // A widget view reads flags() for these; a Qt Quick one cannot, so the
    // item answers both. Only the Load column is a check box, and only where
    // the plugin can be turned off at all.
    QVariant quickRole(int column, int role) const
    {
        if (role == Utils::AspectTable::CheckableRole)
            return column == LoadedColumn;
        if (role == Utils::AspectTable::EditableRole)
            return Utils::AspectTable::isWritable(flags(column));
        return {};
    }

    Qt::ItemFlags flags(int column) const override
    {
        Qt::ItemFlags ret = Qt::ItemIsSelectable;

        if (isEnabled())
            ret |= Qt::ItemIsEnabled;

        if (column == LoadedColumn) {
            if (m_spec->isAvailableForHostPlatform() && !m_spec->isRequired())
                ret |= Qt::ItemIsUserCheckable;
        }

        return ret;
    }

public:
    PluginSpec *m_spec; // Not owned.
    PluginData *m_data; // Not owned.
};

class CollectionItem : public TreeItem
{
public:
    CollectionItem(const QString &name, const PluginSpecs &plugins, PluginData *data)
        : m_name(name)
        , m_plugins(plugins)
        , m_data(data)
    {
        for (PluginSpec *spec : plugins)
            appendChild(new PluginItem(spec, data));
    }

    QVariant data(int column, int role) const override
    {
        if (column == NameColumn) {
            if (role == Qt::DisplayRole || role == SortRole)
                return m_name;
        }

        if (column == LoadedColumn) {
            if (role == Qt::ToolTipRole)
                return Tr::tr("Load on Startup");
            if (role == Qt::CheckStateRole || role == SortRole) {
                int checkedCount = 0;
                for (PluginSpec *spec : m_plugins) {
                    if (spec->isEnabledBySettings())
                        ++checkedCount;
                }

                if (checkedCount == 0)
                    return Qt::Unchecked;
                if (checkedCount == m_plugins.length())
                    return Qt::Checked;
                return Qt::PartiallyChecked;
            }
        }

        // The same two a Qt Quick view cannot read out of flags(). A category
        // is a check box in the Load column and nothing else.
        if (role == Utils::AspectTable::CheckableRole)
            return column == LoadedColumn;
        if (role == Utils::AspectTable::EditableRole)
            return Utils::AspectTable::isWritable(flags(column));
        return QVariant();
    }

    bool setData(int column, const QVariant &data, int role) override
    {
        if (column == LoadedColumn && role == Qt::CheckStateRole) {
            const PluginSpecs affectedPlugins
                = Utils::filtered(m_plugins, [](PluginSpec *spec) { return !spec->isRequired(); });
            if (m_data->setPluginsEnabled(toSet(affectedPlugins), data.toBool())) {
                update();
                return true;
            }
        }
        return false;
    }

    Qt::ItemFlags flags(int column) const override
    {
        Qt::ItemFlags ret = Qt::ItemIsSelectable | Qt::ItemIsEnabled;
        if (column == LoadedColumn)
            ret |= Qt::ItemIsUserCheckable;
        return ret;
    }

public:
    QString m_name;
    const PluginSpecs m_plugins;
    PluginData *m_data; // Not owned.
};

} // Internal

namespace Internal {

// The plugins, with the role names a Qt Quick view addresses its cells by.
// Not on BaseTreeModel: registerNamedRole() asserts a name is not already
// taken, so naming these for every model could break one of its own.
class PluginTreeModel : public TreeModel<TreeItem, CollectionItem, PluginItem>
{
public:
    using TreeModel::TreeModel;

    QHash<int, QByteArray> roleNames() const override
    {
        return Utils::AspectTable::withRoleNames(TreeModel::roleNames());
    }
};

// The tree of categories and the plugins under them. The model is the view's;
// the aspect hands it over and reports what the reader does with it.
class PluginTreeAspect final : public Utils::BaseAspect
{
public:
    PluginTreeAspect(Utils::AspectContainer *container, QAbstractItemModel *model)
        : BaseAspect(container), m_model(model)
    {}

    Utils::AspectPresentation presentation() const override
    {
        Utils::AspectPresentation p = BaseAspect::presentation();
        p.control = Utils::AspectControls::Tree;
        return p;
    }

    QAbstractItemModel *tableModel() override { return m_model; }

    void setCurrentIndex(const QModelIndex &index) override
    {
        m_current = index;
        if (m_onCurrent)
            m_onCurrent(index);
    }

    void activateIndex(const QModelIndex &index) override
    {
        if (m_onActivated)
            m_onActivated(index);
    }

    QModelIndex currentIndex() const { return m_current; }
    void setOnCurrent(const std::function<void(const QModelIndex &)> &f) { m_onCurrent = f; }
    void setOnActivated(const std::function<void(const QModelIndex &)> &f) { m_onActivated = f; }

private:
    QAbstractItemModel *const m_model;
    QPersistentModelIndex m_current;
    std::function<void(const QModelIndex &)> m_onCurrent;
    std::function<void(const QModelIndex &)> m_onActivated;
};

} // Internal

using namespace ExtensionSystem::Internal;

PluginData::PluginData(QWidget *parent, PluginView *owner)
    : m_parent(parent), m_pluginView(owner)
{
    m_model = new PluginTreeModel(parent);
    m_model->setHeader({ Tr::tr("Name"), Tr::tr("Load"), Tr::tr("Version"), Tr::tr("Vendor") });

    m_sortModel = new CategorySortFilterModel(parent);
    m_sortModel->setSourceModel(m_model);
    m_sortModel->setSortRole(SortRole);
    m_sortModel->setFilterKeyColumn(-1/*all*/);
}

/*!
    Constructs a plugin view with \a parent that displays a list of plugins
    from a plugin manager.
*/
PluginView::PluginView(QWidget *parent)
    : QWidget(parent)
    , m_data(this, this)
    , m_aspects(new AspectContainer)
{
    m_aspects->setAutoApply(true);
    m_tree = new PluginTreeAspect(m_aspects.get(), m_data.m_sortModel);
    m_tree->setQmlName("Plugins");
    // Sorted by name from the start, the way the header's indicator did.
    m_data.m_sortModel->sort(NameColumn, Qt::AscendingOrder);

    // The generic form: this container is one tree and needs no page of its
    // own. It is null where Qt Quick is not there to draw it, and then the
    // container's own layouter is what is left - which no more draws a tree
    // than it does for the other ported ones, but leaves something in the
    // layout rather than nothing.
    QWidget *form = Utils::AspectWidgets::createGenericAspectForm(m_aspects.get());
    if (!form)
        form = Utils::AspectWidgets::createAspectForm(m_aspects.get());

    auto *gridLayout = new QGridLayout(this);
    gridLayout->setContentsMargins(2, 2, 2, 2);
    if (form)
        gridLayout->addWidget(form, 1, 0, 1, 1);

    connect(PluginManager::instance(), &PluginManager::pluginsChanged,
            this, &PluginView::updatePlugins);

    m_tree->setOnActivated([this](const QModelIndex &idx) {
        emit pluginActivated(pluginForIndex(idx));
    });
    m_tree->setOnCurrent([this](const QModelIndex &idx) {
        emit currentPluginChanged(pluginForIndex(idx));
    });

    updatePlugins();
}

/*!
    \internal
*/
PluginView::~PluginView() = default;

/*!
    Returns the current selection in the list of plugins.
*/
PluginSpec *PluginView::currentPlugin() const
{
    return pluginForIndex(m_tree->currentIndex());
}

/*!
    Sets the \a filter for listing plugins.
*/
void PluginView::setFilter(const QString &filter)
{
    m_data.m_sortModel->setFilterRegularExpression(
        QRegularExpression(QRegularExpression::escape(filter),
                           QRegularExpression::CaseInsensitiveOption));
    m_tree->expandControl();
}

PluginSpec *PluginView::pluginForIndex(const QModelIndex &index) const
{
    const QModelIndex &sourceIndex = m_data.m_sortModel->mapToSource(index);
    PluginItem *item = m_data.m_model->itemForIndexAtLevel<2>(sourceIndex);
    return item ? item->m_spec: nullptr;
}

void PluginView::updatePlugins()
{
    // Model.
    m_data.m_model->clear();

    const QHash<QString, PluginSpecs> pluginCollections
        = PluginManager::pluginCollections();
    std::vector<CollectionItem *> collections;
    const auto end = pluginCollections.cend();
    for (auto it = pluginCollections.cbegin(); it != end; ++it) {
        const QString name = it.key().isEmpty() ? Tr::tr("Utilities") : it.key();
        collections.push_back(new CollectionItem(name, it.value(), &m_data));
    }
    Utils::sort(collections, &CollectionItem::m_name);

    for (CollectionItem *collection : std::as_const(collections))
        m_data.m_model->rootItem()->appendChild(collection);

    emit m_data.m_model->layoutChanged();
    m_tree->expandControl();
}

PluginData &PluginView::data()
{
    return m_data;
}

#ifdef WITH_TESTS
QAbstractItemModel *PluginView::modelForTest() const
{
    return m_data.m_sortModel;
}
#endif

bool PluginData::setPluginsEnabled(const QSet<PluginSpec *> &plugins, bool enable)
{
    std::optional<QSet<PluginSpec *>> additionalPlugins
        = askForEnablingPlugins(m_parent, plugins, enable);
    if (!additionalPlugins) // canceled
        return false;

    const QSet<PluginSpec *> affectedPlugins = plugins + *additionalPlugins;
    for (PluginSpec *spec : affectedPlugins) {
        PluginItem *item = m_model->findItemAtLevel<2>([spec](PluginItem *item) {
                return item->m_spec == spec;
        });
        QTC_ASSERT(item, continue);
        if (m_affectedPlugins.find(spec) == m_affectedPlugins.end())
            m_affectedPlugins[spec] = spec->isEnabledBySettings();
        spec->setEnabledBySettings(enable);
        item->updateColumn(LoadedColumn);
        item->parent()->updateColumn(LoadedColumn);
    }

    if (m_pluginView)
        emit m_pluginView->pluginsChanged(affectedPlugins, enable);

    return true;
}

void PluginView::cancelChanges()
{
    for (auto element : m_data.m_affectedPlugins)
        element.first->setEnabledBySettings(element.second);
}

} // namespace ExtensionSystem
