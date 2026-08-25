// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "locatorsettingspage.h"

#include "directoryfilter.h"
#include "ilocatorfilter.h"
#include "locator.h"
#include "locatorconstants.h"
#include "urllocatorfilter.h"
#include "../coreconstants.h"
#include "../coreplugintr.h"

#include <utils/algorithm.h>
#include <utils/aspectpresentation.h>
#include <utils/guiutils.h>
#include <utils/shutdownguard.h>
#include <utils/qtcassert.h>
#include <utils/treemodel.h>

#include <QHash>

using namespace Utils;

static const int SortRole = Qt::UserRole + 1;

namespace Core::Internal {

enum FilterItemColumn
{
    FilterName = 0,
    FilterPrefix,
    FilterIncludedByDefault
};

class FilterItem : public TreeItem
{
public:
    FilterItem(ILocatorFilter *filter);

    QVariant data(int column, int role) const override;
    Qt::ItemFlags flags(int column) const override;
    bool setData(int column, const QVariant &data, int role) override;

    ILocatorFilter *filter() const;

private:
    ILocatorFilter *m_filter = nullptr;
};

class CategoryItem : public TreeItem
{
public:
    CategoryItem(const QString &name, int order);
    QVariant data(int column, int role) const override;
    Qt::ItemFlags flags(int column) const override { Q_UNUSED(column) return Qt::ItemIsEnabled; }

private:
    QString m_name;
    int m_order = 0;
};

FilterItem::FilterItem(ILocatorFilter *filter)
    : m_filter(filter)
{
}

QVariant FilterItem::data(int column, int role) const
{
    switch (column) {
    case FilterName:
        if (role == SortRole)
            return m_filter->displayName();
        if (role == Qt::DisplayRole) {
            if (m_filter->description().isEmpty())
                return m_filter->displayName();
            return QString("<html>%1<br/><span style=\"font-weight: 70\">%2</span>")
                .arg(m_filter->displayName(), m_filter->description().toHtmlEscaped());
        }
        break;
    case FilterPrefix:
        if (role == Qt::DisplayRole || role == SortRole || role == Qt::EditRole)
            return m_filter->shortcutString();
        break;
    case FilterIncludedByDefault:
        if (role == Qt::CheckStateRole || role == SortRole)
            return m_filter->isIncludedByDefault() ? Qt::Checked : Qt::Unchecked;
        break;
    default:
        break;
    }

    if (role == Qt::ToolTipRole) {
        const QString description = m_filter->description();
        return description.isEmpty() ? QString() : ("<html>" + description.toHtmlEscaped());
    }
    return QVariant();
}

Qt::ItemFlags FilterItem::flags(int column) const
{
    if (column == FilterPrefix)
        return Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsEditable;
    if (column == FilterIncludedByDefault)
        return Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsUserCheckable;
    return Qt::ItemIsSelectable | Qt::ItemIsEnabled;
}

bool FilterItem::setData(int column, const QVariant &data, int role)
{
    switch (column) {
    case FilterName:
        break;
    case FilterPrefix:
        if (role == Qt::EditRole && data.canConvert<QString>()) {
            if (m_filter->shortcutString() == data.toString())
                return false;
            m_filter->setShortcutString(data.toString());
            return true;
        }
        break;
    case FilterIncludedByDefault:
        if (role == Qt::CheckStateRole && data.canConvert<bool>()) {
            m_filter->setIncludedByDefault(data.toBool());
            return true;
        }
    }
    return false;
}

ILocatorFilter *FilterItem::filter() const
{
    return m_filter;
}

CategoryItem::CategoryItem(const QString &name, int order)
    : m_name(name), m_order(order)
{
}

QVariant CategoryItem::data(int column, int role) const
{
    if (role == SortRole)
        return m_order;
    // The name once, not once per column: a widget view spanned the heading
    // across the row, and a Qt Quick tree draws each cell where it is.
    if (role == Qt::DisplayRole && column == 0)
        return m_name;
    return QVariant();
}

// The filters, as a Qt Quick view reads them: which cells may be written to
// and which are check boxes are answered from the items' own flags, the same
// way a QTreeView reads them.
class FilterTreeModel : public TreeModel<>
{
public:
    using TreeModel::TreeModel;

    QVariant data(const QModelIndex &index, int role) const override
    {
        switch (role) {
        case AspectTable::EditableRole:
            return AspectTable::isWritable(flags(index));
        case AspectTable::CheckableRole:
            return flags(index).testFlag(Qt::ItemIsUserCheckable);
        default:
            return TreeModel::data(index, role);
        }
    }

    QHash<int, QByteArray> roleNames() const override
    {
        return AspectTable::withRoleNames(TreeModel::roleNames());
    }
};

// The filters, as the page shows them: two categories with the filters under
// them, a prefix to type in and a check box for each. Which of those cells may
// be written to is the model's answer - see FilterItem::flags().
class FilterTreeAspect final : public BaseAspect
{
    Q_OBJECT

public:
    using BaseAspect::BaseAspect;

    AspectPresentation presentation() const override
    {
        AspectPresentation p = BaseAspect::presentation();
        p.control = AspectControls::Tree;
        p.filterPlaceholderText = Tr::tr("Filter");
        return p;
    }

    QAbstractItemModel *tableModel() override { return &m_model; }

    // Which filter the buttons act on. The view says so; the page reads it.
    // Null where a category heading is current, or nothing is.
    Q_INVOKABLE void setCurrentIndex(const QModelIndex &index)
    {
        auto item = dynamic_cast<FilterItem *>(m_model.itemForIndex(index));
        ILocatorFilter *filter = item ? item->filter() : nullptr;
        if (filter == m_current)
            return;
        m_current = filter;
        m_currentItem = item;
        emit currentChanged();
    }

    ILocatorFilter *current() const { return m_current; }
    FilterItem *currentItem() const { return m_currentItem; }

    void clear()
    {
        m_model.clear();
        m_current = nullptr;
        m_currentItem = nullptr;
        emit currentChanged();
    }

    FilterTreeModel &model() { return m_model; }

signals:
    void currentChanged();

private:
    FilterTreeModel m_model{this};
    ILocatorFilter *m_current = nullptr;
    FilterItem *m_currentItem = nullptr;
};

class LocatorSettingsAspects final : public AspectContainer
{
public:
    LocatorSettingsAspects();

    void apply() override;
    void cancel() override;

private:
    void initializeModel();
    void updateButtonStates();
    void configureCurrentFilter();
    void addCustomFilter(ILocatorFilter *filter);
    void removeCustomFilter();
    void requestRefresh();
    void saveFilterStates();
    void restoreFilterStates();

    Locator *m_plugin = nullptr;
    TreeItem *m_customFilterRoot = nullptr;
    QList<ILocatorFilter *> m_filters;
    QList<ILocatorFilter *> m_addedFilters;
    QList<ILocatorFilter *> m_removedFilters;
    QList<ILocatorFilter *> m_customFilters;
    QList<ILocatorFilter *> m_refreshFilters;
    QHash<ILocatorFilter *, QByteArray> m_filterStates;

    ContainerAspect m_settings{this};
    FilterTreeAspect m_tree{this};
    ActionAspect m_addDirectory{this};
    ActionAspect m_addUrl{this};
    ActionAspect m_remove{this};
    ActionAspect m_edit{this};
};

LocatorSettingsAspects::LocatorSettingsAspects()
{
    setAutoApply(false);
    setQmlSource(QUrl("qrc:/qt/qml/QtCreator/Core/LocatorSettingsPage.qml"));

    m_plugin = Locator::instance();
    m_filters = Locator::filters();
    m_customFilters = m_plugin->customFilters();

    // The Locator's own settings, shown here rather than copied: see
    // Utils::ContainerAspect.
    m_settings.setQmlName("Settings");
    m_settings.setContainer(&locatorSettings());

    m_tree.setQmlName("Filters");

    // What a new filter is made of is a dialog's business, and there are two
    // kinds of them, so there are two buttons rather than a menu.
    m_addDirectory.setQmlName("AddDirectory");
    m_addDirectory.setActionText(Tr::tr("Files in Directories..."));
    m_addDirectory.setAction([this] {
        addCustomFilter(new DirectoryFilter(
            Utils::Id(Constants::CUSTOM_DIRECTORY_FILTER_BASEID)
                .withSuffix(m_customFilters.size() + 1)));
    });

    m_addUrl.setQmlName("AddUrl");
    m_addUrl.setActionText(Tr::tr("URL Template..."));
    m_addUrl.setAction([this] {
        auto filter = new UrlLocatorFilter(
            Utils::Id(Constants::CUSTOM_URL_FILTER_BASEID).withSuffix(m_customFilters.size() + 1));
        filter->setIsCustomFilter(true);
        addCustomFilter(filter);
    });

    m_remove.setQmlName("Remove");
    m_remove.setActionText(Tr::tr("Remove"));
    m_remove.setAction([this] { removeCustomFilter(); });

    m_edit.setQmlName("Edit");
    m_edit.setActionText(Tr::tr("Edit..."));
    m_edit.setAction([this] { configureCurrentFilter(); });

    // Behaviour, not layout.
    connect(&m_tree, &FilterTreeAspect::currentChanged, this, [this] { updateButtonStates(); });
    connect(&m_tree.model(), &QAbstractItemModel::dataChanged, this, &markSettingsDirty);

    initializeModel();
    updateButtonStates();
    saveFilterStates();
}

void LocatorSettingsAspects::initializeModel()
{
    FilterTreeModel &model = m_tree.model();
    model.setHeader({Tr::tr("Name"), Tr::tr("Prefix"), Tr::tr("Default")});
    model.setHeaderToolTip({
        QString(),
        ILocatorFilter::msgPrefixToolTip(),
        ILocatorFilter::msgIncludeByDefaultToolTip()
    });
    m_tree.clear();
    QSet<ILocatorFilter *> customFilterSet = Utils::toSet(m_customFilters);
    auto builtIn = new CategoryItem(Tr::tr("Built-in"), 0/*order*/);
    for (ILocatorFilter *filter : std::as_const(m_filters))
        if (!filter->isHidden() && !customFilterSet.contains(filter))
            builtIn->appendChild(new FilterItem(filter));
    m_customFilterRoot = new CategoryItem(Tr::tr("Custom"), 1/*order*/);
    for (ILocatorFilter *customFilter : std::as_const(m_customFilters))
        m_customFilterRoot->appendChild(new FilterItem(customFilter));

    model.rootItem()->appendChild(builtIn);
    model.rootItem()->appendChild(m_customFilterRoot);
}

void LocatorSettingsAspects::updateButtonStates()
{
    ILocatorFilter *filter = m_tree.current();
    m_edit.setEnabled(filter && filter->isConfigurable());
    m_remove.setEnabled(filter && m_customFilters.contains(filter));
}

void LocatorSettingsAspects::configureCurrentFilter()
{
    ILocatorFilter *filter = m_tree.current();
    FilterItem *item = m_tree.currentItem();
    QTC_ASSERT(filter && item, return);
    QTC_ASSERT(filter->isConfigurable(), return);
    const bool includedByDefault = filter->isIncludedByDefault();
    const QString shortcutString = filter->shortcutString();
    bool needsRefresh = false;
    filter->openConfigDialog(Utils::dialogParent(), needsRefresh);
    if (needsRefresh && !m_refreshFilters.contains(filter)) {
        m_refreshFilters.append(filter);
        markSettingsDirty();
    }
    if (filter->isIncludedByDefault() != includedByDefault)
        item->updateColumn(FilterIncludedByDefault);
    if (filter->shortcutString() != shortcutString)
        item->updateColumn(FilterPrefix);
}

void LocatorSettingsAspects::addCustomFilter(ILocatorFilter *filter)
{
    bool needsRefresh = false;
    if (filter->openConfigDialog(Utils::dialogParent(), needsRefresh)) {
        m_filters.append(filter);
        m_addedFilters.append(filter);
        m_customFilters.append(filter);
        m_refreshFilters.append(filter);
        m_customFilterRoot->appendChild(new FilterItem(filter));
        markSettingsDirty();
    } else {
        delete filter;
    }
}

void LocatorSettingsAspects::removeCustomFilter()
{
    ILocatorFilter *filter = m_tree.current();
    FilterItem *item = m_tree.currentItem();
    QTC_ASSERT(filter && item, return);
    QTC_ASSERT(m_customFilters.contains(filter), return);
    m_tree.setCurrentIndex({});
    m_tree.model().destroyItem(item);
    m_filters.removeAll(filter);
    m_customFilters.removeAll(filter);
    m_refreshFilters.removeAll(filter);
    if (m_addedFilters.contains(filter)) {
        m_addedFilters.removeAll(filter);
        delete filter;
    } else {
        m_removedFilters.append(filter);
    }
    markSettingsDirty();
}

void LocatorSettingsAspects::requestRefresh()
{
    if (!m_refreshFilters.isEmpty())
        m_plugin->refresh(m_refreshFilters);
}

void LocatorSettingsAspects::saveFilterStates()
{
    m_filterStates.clear();
    for (ILocatorFilter *filter : std::as_const(m_filters))
        m_filterStates.insert(filter, filter->saveState());
}

void LocatorSettingsAspects::restoreFilterStates()
{
    for (auto it = m_filterStates.cbegin(); it != m_filterStates.cend(); ++it)
        it.key()->restoreState(*it);
}

void LocatorSettingsAspects::apply()
{
    // Delete removed filters and clear added filters
    qDeleteAll(m_removedFilters);
    m_removedFilters.clear();
    m_addedFilters.clear();

    // Pass the new configuration on to the plugin
    m_plugin->setFilters(m_filters);
    m_plugin->setCustomFilters(m_customFilters);
    AspectContainer::apply();
    locatorSettings().apply();
    requestRefresh();
    m_plugin->saveSettings();
    saveFilterStates();
}

void LocatorSettingsAspects::cancel()
{
    // If settings were applied, this shouldn't change anything. Otherwise it
    // makes sure the filter states aren't changed permanently.
    restoreFilterStates();

    // Delete added filters and clear removed filters
    qDeleteAll(m_addedFilters);
    m_addedFilters.clear();
    m_removedFilters.clear();

    AspectContainer::cancel();
    locatorSettings().cancel();

    // The page is built once, so it starts again from what the plugin has
    // rather than from what was being edited.
    m_filters = Locator::filters();
    m_customFilters = m_plugin->customFilters();
    m_refreshFilters.clear();
    initializeModel();
    updateButtonStates();
    saveFilterStates();
}


// LocatorSettingsPage

LocatorSettingsPage::LocatorSettingsPage()
{
    setId(Constants::FILTER_OPTIONS_PAGE);
    setDisplayName(Tr::tr(Constants::FILTER_OPTIONS_PAGE));
    setCategory(Constants::SETTINGS_CATEGORY_CORE);
    setSettingsProvider([] {
        static GuardedObject<LocatorSettingsAspects> theAspects;
        return theAspects.get();
    });
    // Building the page means building the filter list, and the filters are
    // in a model that the search does not see anyway.
    setFixedKeywords({Tr::tr("Refresh interval:"),
                      Tr::tr("Show Paths in Relation to Active Project"),
                      Tr::tr("Add..."),
                      Tr::tr("Remove"),
                      Tr::tr("Edit...")});
}

} // Core::Internal

#include "locatorsettingspage.moc"
