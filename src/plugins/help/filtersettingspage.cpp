// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "filtersettingspage.h"

#include "helptr.h"
#include "localhelpmanager.h"

#include <coreplugin/coreconstants.h>
#include <coreplugin/helpmanager.h>

#include <utils/algorithm.h>
#include <utils/aspects.h>

#include <QAbstractListModel>
#include <QHelpFilterData>
#include <QHelpFilterEngine>
#include <QVersionNumber>

#ifdef WITH_TESTS
#include <QTest>
#endif

namespace Help::Internal {

using namespace Utils;

// One named filter as the page edits it. The engine is not told anything until
// Apply, so this is a working copy and not a view onto it.
struct Filter
{
    QString name;
    QStringList components;
    QStringList versions;

    friend bool operator==(const Filter &, const Filter &) = default;
};

static QList<Filter> filtersInEngine()
{
    QHelpFilterEngine *engine = LocalHelpManager::filterEngine();
    QList<Filter> filters;
    const QStringList names = engine->filters();
    for (const QString &name : names) {
        const QHelpFilterData data = engine->filterData(name);
        filters.append({name,
                        data.components(),
                        Utils::transform(data.versions(),
                                         [](const QVersionNumber &v) { return v.toString(); })});
    }
    return filters;
}

// The filter names, one per row, renamed in place. A name has to be there and
// has to be unique: the engine keys on it, so a second "Qt" would silently
// replace the first at Apply.
class FilterNameModel final : public QAbstractListModel
{
    Q_OBJECT

public:
    using QAbstractListModel::QAbstractListModel;

    QList<Filter> filters() const { return m_filters; }

    void setFilters(const QList<Filter> &filters)
    {
        beginResetModel();
        m_filters = filters;
        endResetModel();
    }

    Filter filterAt(int row) const
    {
        return row >= 0 && row < m_filters.size() ? m_filters.at(row) : Filter{};
    }

    void setOptionsAt(int row, const QStringList &components, const QStringList &versions)
    {
        if (row < 0 || row >= m_filters.size())
            return;
        if (m_filters[row].components == components && m_filters[row].versions == versions)
            return;
        m_filters[row].components = components;
        m_filters[row].versions = versions;
        emit changed();
    }

    int rowCount(const QModelIndex &parent = {}) const override
    {
        return parent.isValid() ? 0 : m_filters.size();
    }

    QVariant data(const QModelIndex &index, int role) const override
    {
        if (!index.isValid() || index.row() >= m_filters.size())
            return {};
        if (role == Qt::DisplayRole || role == Qt::EditRole)
            return m_filters.at(index.row()).name;
        return {};
    }

    bool setData(const QModelIndex &index, const QVariant &value, int role) override
    {
        if (!index.isValid() || index.row() >= m_filters.size() || role != Qt::EditRole)
            return false;
        const QString name = value.toString().trimmed();
        if (name.isEmpty() || name == m_filters.at(index.row()).name)
            return false;
        if (indexOfName(name) >= 0)
            return false;
        m_filters[index.row()].name = name;
        emit dataChanged(index, index, {Qt::DisplayRole, Qt::EditRole});
        emit changed();
        return true;
    }

    Qt::ItemFlags flags(const QModelIndex &index) const override
    {
        return QAbstractListModel::flags(index) | Qt::ItemIsEditable;
    }

    QVariant headerData(int section, Qt::Orientation orientation, int role) const override
    {
        if (orientation == Qt::Horizontal && role == Qt::DisplayRole && section == 0)
            return Tr::tr("Filter");
        return QAbstractListModel::headerData(section, orientation, role);
    }

    QHash<int, QByteArray> roleNames() const override
    {
        return {{Qt::DisplayRole, "display"}, {Qt::EditRole, "edit"}};
    }

    bool insertRows(int row, int count, const QModelIndex &parent = {}) override
    {
        if (parent.isValid() || count != 1)
            return false;
        row = qBound(0, row, m_filters.size());
        beginInsertRows({}, row, row);
        m_filters.insert(row, Filter{unusedName(), {}, {}});
        endInsertRows();
        emit changed();
        return true;
    }

    bool removeRows(int row, int count, const QModelIndex &parent = {}) override
    {
        if (parent.isValid() || row < 0 || count < 1 || row + count > m_filters.size())
            return false;
        beginRemoveRows({}, row, row + count - 1);
        m_filters.remove(row, count);
        endRemoveRows();
        emit changed();
        return true;
    }

signals:
    void changed();

private:
    int indexOfName(const QString &name) const
    {
        return Utils::indexOf(m_filters, [&name](const Filter &f) { return f.name == name; });
    }

    // "Filter", then "Filter 2" and up: a new row is named before it is typed
    // over, and two rows called the same thing cannot both be stored.
    QString unusedName() const
    {
        const QString base = Tr::tr("Filter");
        if (indexOfName(base) < 0)
            return base;
        for (int i = 2;; ++i) {
            const QString candidate = QString("%1 %2").arg(base).arg(i);
            if (indexOfName(candidate) < 0)
                return candidate;
        }
    }

    QList<Filter> m_filters;
};

// What one filter picks out of everything the registered documentation offers.
// Everything available is listed; the filter's own choice is what is ticked.
class FilterOptionsModel final : public QAbstractListModel
{
    Q_OBJECT

public:
    using QAbstractListModel::QAbstractListModel;

    void setAvailable(const QStringList &available)
    {
        if (m_available == available)
            return;
        beginResetModel();
        m_available = available;
        endResetModel();
    }

    void setChecked(const QStringList &checked)
    {
        const QSet<QString> wanted = Utils::toSet(checked);
        if (m_checked == wanted)
            return;
        m_checked = wanted;
        if (!m_available.isEmpty()) {
            emit dataChanged(index(0), index(m_available.size() - 1), {Qt::CheckStateRole});
        }
    }

    // What the page calls when a box is clicked. An invokable rather than a
    // write through the delegate's model role, so that the page needs to know
    // neither the role number nor how a QML model write reaches setData.
    Q_INVOKABLE void toggle(int row, bool on)
    {
        setData(index(row), on ? Qt::Checked : Qt::Unchecked, Qt::CheckStateRole);
    }

    QStringList checked() const
    {
        // In the order the options are listed rather than the order they were
        // ticked, so that what is stored does not depend on how it was clicked.
        return Utils::filtered(m_available,
                               [this](const QString &o) { return m_checked.contains(o); });
    }

    int rowCount(const QModelIndex &parent = {}) const override
    {
        return parent.isValid() ? 0 : m_available.size();
    }

    QVariant data(const QModelIndex &index, int role) const override
    {
        if (!index.isValid() || index.row() >= m_available.size())
            return {};
        const QString option = m_available.at(index.row());
        if (role == Qt::DisplayRole)
            return option;
        if (role == Qt::CheckStateRole)
            return m_checked.contains(option) ? Qt::Checked : Qt::Unchecked;
        return {};
    }

    bool setData(const QModelIndex &index, const QVariant &value, int role) override
    {
        if (!index.isValid() || index.row() >= m_available.size() || role != Qt::CheckStateRole)
            return false;
        const QString option = m_available.at(index.row());
        const bool on = value.toInt() == Qt::Checked;
        if (on == m_checked.contains(option))
            return false;
        if (on)
            m_checked.insert(option);
        else
            m_checked.remove(option);
        emit dataChanged(index, index, {Qt::CheckStateRole});
        emit checkedChanged();
        return true;
    }

    Qt::ItemFlags flags(const QModelIndex &index) const override
    {
        return QAbstractListModel::flags(index) | Qt::ItemIsUserCheckable;
    }

    QVariant headerData(int section, Qt::Orientation orientation, int role) const override
    {
        if (orientation == Qt::Horizontal && role == Qt::DisplayRole && section == 0)
            return m_title;
        return QAbstractListModel::headerData(section, orientation, role);
    }

    void setTitle(const QString &title) { m_title = title; }

    // Prefixed, because a CheckBox already has a "display" and a "checkState"
    // and both are final: a delegate declaring either name would shadow the
    // control's own rather than read the model's.
    QHash<int, QByteArray> roleNames() const override
    {
        return {{Qt::DisplayRole, "optionText"}, {Qt::CheckStateRole, "optionChecked"}};
    }

signals:
    void checkedChanged();

private:
    QStringList m_available;
    QSet<QString> m_checked;
    QString m_title;
};

// The filters, as the page edits them. Not a TypedAspect: the value is three
// models the page drives directly, and Apply is the only thing the engine
// hears about.
class FiltersAspect final : public BaseAspect
{
    Q_OBJECT

    // The row the page is showing the options of. A filter's components and
    // versions only mean anything next to the name they belong to, so the two
    // lists follow the name list's current row rather than a selection of
    // their own.
    Q_PROPERTY(int currentFilter READ currentFilter WRITE setCurrentFilter
                   NOTIFY currentFilterChanged)
    Q_PROPERTY(QAbstractItemModel *components READ componentModel CONSTANT)
    Q_PROPERTY(QAbstractItemModel *versions READ versionModel CONSTANT)

public:
    explicit FiltersAspect(AspectContainer *container)
        : BaseAspect(container)
        // Parented, so that QML cannot take ownership of a model and delete it.
        , m_filters(new FilterNameModel(this))
        , m_components(new FilterOptionsModel(this))
        , m_versions(new FilterOptionsModel(this))
    {
        setQmlName("Filters");
        m_components->setTitle(Tr::tr("Components"));
        m_versions->setTitle(Tr::tr("Versions"));

        reload();

        connect(m_filters, &FilterNameModel::changed, this, [this] {
            emit volatileValueChanged();
        });
        connect(m_filters, &QAbstractItemModel::rowsInserted, this, [this](
                                                                  const QModelIndex &, int first, int) {
            setCurrentFilter(first);
        });
        connect(m_filters, &QAbstractItemModel::rowsRemoved, this, [this] {
            // The row that is there now, or the last one, or none at all.
            setCurrentFilter(qMin(m_current, m_filters->rowCount() - 1), Force);
        });
        for (FilterOptionsModel *options : {m_components, m_versions}) {
            connect(options, &FilterOptionsModel::checkedChanged, this, [this] {
                m_filters->setOptionsAt(m_current, m_components->checked(), m_versions->checked());
                emit volatileValueChanged();
            });
        }
    }

    AspectPresentation presentation() const override
    {
        AspectPresentation p = BaseAspect::presentation();
        p.control = AspectControls::Table;
        p.allowAdding = true;
        p.allowRemoving = true;
        return p;
    }

    QAbstractItemModel *tableModel() override { return m_filters; }
    QAbstractItemModel *componentModel() const { return m_components; }
    QAbstractItemModel *versionModel() const { return m_versions; }

    int currentFilter() const { return m_current; }

    enum Announce { OnlyIfChanged, Force };

    void setCurrentFilter(int row, Announce announce = OnlyIfChanged)
    {
        row = row < 0 || row >= m_filters->rowCount() ? -1 : row;
        if (row == m_current && announce == OnlyIfChanged)
            return;
        m_current = row;
        const Filter filter = m_filters->filterAt(row);
        m_components->setChecked(filter.components);
        m_versions->setChecked(filter.versions);
        emit currentFilterChanged();
    }

    // What there is to filter on. Told rather than fetched: which documentation
    // is registered is the caller's business, and the two lists behave the same
    // whatever put the options in them.
    void setAvailableOptions(const QStringList &components, const QStringList &versions)
    {
        m_components->setAvailable(components);
        m_versions->setAvailable(versions);
        // A reset clears what was ticked, so the current filter has to say
        // again what it picks.
        setCurrentFilter(m_current, Force);
    }

    // What the registered documentation offers. It changes when documentation
    // is added or removed, which can happen while the page is open.
    void updateAvailableOptions()
    {
        QHelpFilterEngine *engine = LocalHelpManager::filterEngine();
        setAvailableOptions(engine->availableComponents(),
                            Utils::transform(engine->availableVersions(),
                                             [](const QVersionNumber &v) {
                                                 return v.toString();
                                             }));
    }

    bool isDirty() const override { return m_filters->filters() != filtersInEngine(); }

    void apply() override
    {
        if (!isDirty())
            return;

        QHelpFilterEngine *engine = LocalHelpManager::filterEngine();
        const QList<Filter> wanted = m_filters->filters();
        const QStringList names = Utils::transform(wanted, &Filter::name);

        // Removed first: a filter that was renamed is a new name plus an old
        // one that nothing refers to any more.
        const QStringList existing = engine->filters();
        for (const QString &name : existing) {
            if (!names.contains(name))
                engine->removeFilter(name);
        }
        for (const Filter &filter : wanted) {
            QHelpFilterData data;
            data.setComponents(filter.components);
            data.setVersions(Utils::transform(filter.versions, [](const QString &v) {
                return QVersionNumber::fromString(v);
            }));
            engine->setFilterData(filter.name, data);
        }
        reload();
        if (m_onChanged)
            m_onChanged();
    }

    void cancel() override { reload(); }

    void setOnChanged(const std::function<void()> &onChanged) { m_onChanged = onChanged; }

signals:
    void currentFilterChanged();

private:
    void reload()
    {
        m_filters->setFilters(filtersInEngine());
        updateAvailableOptions();
        setCurrentFilter(m_filters->rowCount() > 0 ? 0 : -1, Force);
    }

    FilterNameModel *m_filters = nullptr;
    FilterOptionsModel *m_components = nullptr;
    FilterOptionsModel *m_versions = nullptr;
    int m_current = -1;
    std::function<void()> m_onChanged;
};

// What the Filters page edits. The filters live in the help engine's collection
// file, so the models are a working copy of what is in there.
class FilterSettings final : public AspectContainer
{
public:
    explicit FilterSettings(const std::function<void()> &onChanged)
    {
        setAutoApply(false);
        setQmlSource(QUrl("qrc:/qt/qml/QtCreator/Help/FilterSettingsPage.qml"));

        m_filters.setToolTip(
            Tr::tr("Filters restrict the documentation shown to the components and versions "
                   "a filter selects."));
        m_filters.setOnChanged(onChanged);

        connect(Core::HelpManager::Signals::instance(),
                &Core::HelpManager::Signals::documentationChanged,
                &m_filters,
                [this] { m_filters.updateAvailableOptions(); });
    }

private:
    FiltersAspect m_filters{this};
};

FilterSettingsPage::FilterSettingsPage(const std::function<void()> &onChanged)
{
    setId("D.Filters");
    setDisplayName(Tr::tr("Filters"));
    setCategory(Core::Constants::HELP_CATEGORY);
    setSettingsProvider([onChanged] {
        // The engine has to be up before its filters can be read, and nothing
        // else has necessarily needed it yet.
        LocalHelpManager::setupGuiHelpEngine();
        static FilterSettings settings(onChanged);
        return &settings;
    });
}


#ifdef WITH_TESTS

// The filters live in the help engine's collection file, which a test run has
// its own copy of under -settingspath. Even so, every test here cleans up the
// filters it applied: the collection outlives the test function.
class FilterSettingsPageTest final : public QObject
{
    Q_OBJECT

private slots:
    void testAddingAFilterNamesItSomethingNotAlreadyTaken()
    {
        AspectContainer container;
        FiltersAspect filters(&container);
        auto *model = filters.tableModel();
        const int before = model->rowCount();

        QVERIFY(model->insertRows(model->rowCount(), 1));
        QCOMPARE(model->rowCount(), before + 1);
        const QString first = model->data(model->index(before, 0)).toString();
        QVERIFY(!first.isEmpty());

        // The new row is the one whose options the page shows, or adding a
        // filter would leave the two lists on somebody else's.
        QCOMPARE(filters.currentFilter(), before);

        // And a second one cannot be called the same thing, because the engine
        // keys on the name.
        QVERIFY(model->insertRows(model->rowCount(), 1));
        const QString second = model->data(model->index(before + 1, 0)).toString();
        QVERIFY2(first != second,
                 qPrintable(QString("two filters were both named '%1'").arg(first)));
    }

    void testAFilterCannotBeRenamedToNothingOrToATakenName()
    {
        AspectContainer container;
        FiltersAspect filters(&container);
        auto *model = filters.tableModel();
        const int first = model->rowCount();
        QVERIFY(model->insertRows(first, 1));
        QVERIFY(model->insertRows(first + 1, 1));
        const QString taken = model->data(model->index(first, 0)).toString();
        const QString original = model->data(model->index(first + 1, 0)).toString();

        QVERIFY2(!model->setData(model->index(first + 1, 0), QString(), Qt::EditRole),
                 "a filter was renamed to nothing");
        QVERIFY2(!model->setData(model->index(first + 1, 0), "   ", Qt::EditRole),
                 "a filter was renamed to whitespace");
        QVERIFY2(!model->setData(model->index(first + 1, 0), taken, Qt::EditRole),
                 "two filters were allowed the same name");
        QCOMPARE(model->data(model->index(first + 1, 0)).toString(), original);

        QVERIFY(model->setData(model->index(first + 1, 0), "Renamed", Qt::EditRole));
        QCOMPARE(model->data(model->index(first + 1, 0)).toString(), QString("Renamed"));
    }

    void testTheOptionListsBelongToTheFilterThatIsCurrent()
    {
        AspectContainer container;
        FiltersAspect filters(&container);
        auto *model = filters.tableModel();
        auto *components = qobject_cast<FilterOptionsModel *>(filters.componentModel());
        QVERIFY(components);

        // Options of the test's own, because a test run registers no
        // documentation and the engine would offer nothing to tick.
        filters.setAvailableOptions({"qtcore", "qtgui", "qtquick"}, {"6.5", "6.8"});
        QCOMPARE(components->rowCount(), 3);

        const int first = model->rowCount();
        QVERIFY(model->insertRows(first, 1));
        QVERIFY(model->insertRows(first + 1, 1));

        filters.setCurrentFilter(first);
        components->toggle(1, true);
        QCOMPARE(components->checked(), QStringList{"qtgui"});
        const QString picked = components->checked().first();

        // The second filter picked nothing, so it shows nothing ticked.
        filters.setCurrentFilter(first + 1);
        QVERIFY2(components->checked().isEmpty(),
                 "one filter's components were shown against another filter");

        // And going back shows what the first one picked, which is what makes
        // the list the filter's rather than the page's.
        filters.setCurrentFilter(first);
        QCOMPARE(components->checked(), QStringList{picked});
    }

    void testWhatAFilterPicksIsStoredInTheOrderItIsListed()
    {
        AspectContainer container;
        FiltersAspect filters(&container);
        auto *model = filters.tableModel();
        auto *components = qobject_cast<FilterOptionsModel *>(filters.componentModel());
        QVERIFY(components);
        filters.setAvailableOptions({"qtcore", "qtgui", "qtquick"}, {});

        const int row = model->rowCount();
        QVERIFY(model->insertRows(row, 1));
        filters.setCurrentFilter(row);

        // Ticked last to first: what is stored must not depend on the order
        // the boxes happened to be clicked in, or the same filter compares
        // unequal to itself and the page looks dirty when nothing changed.
        components->toggle(2, true);
        components->toggle(0, true);
        QCOMPARE(components->checked(), QStringList({"qtcore", "qtquick"}));
    }

    void testTheOptionRoleNamesDoNotShadowACheckBoxsOwn()
    {
        // A QML CheckBox has a final "display" and a final "checkState". A
        // delegate declaring either name would shadow the control's rather
        // than read the model's, and the boxes would all draw unticked.
        AspectContainer container;
        FiltersAspect filters(&container);
        const QHash<int, QByteArray> roles = filters.componentModel()->roleNames();
        QVERIFY(!roles.values().contains(QByteArray("display")));
        QVERIFY(!roles.values().contains(QByteArray("checkState")));
        QCOMPARE(roles.value(Qt::DisplayRole), QByteArray("optionText"));
        QCOMPARE(roles.value(Qt::CheckStateRole), QByteArray("optionChecked"));
    }

    void testApplyWritesToTheEngineAndCancelPutsItBack()
    {
        QHelpFilterEngine *engine = LocalHelpManager::filterEngine();
        const QStringList before = engine->filters();
        const QString name = "QtcFilterSettingsTest";
        QVERIFY2(!before.contains(name), "the fixture's name was already in use");

        AspectContainer container;
        FiltersAspect filters(&container);
        auto *model = filters.tableModel();
        QVERIFY2(!filters.isDirty(), "a freshly read page was already dirty");

        const int row = model->rowCount();
        QVERIFY(model->insertRows(row, 1));
        QVERIFY(model->setData(model->index(row, 0), name, Qt::EditRole));
        QVERIFY2(filters.isDirty(), "adding a filter did not make the page dirty");
        QVERIFY2(!engine->filters().contains(name),
                 "the engine heard about a filter before Apply");

        bool told = false;
        filters.setOnChanged([&told] { told = true; });
        filters.apply();
        QVERIFY2(engine->filters().contains(name), "Apply did not write the filter");
        QVERIFY2(told, "Apply changed the filters without saying so");
        QVERIFY2(!filters.isDirty(), "the page was still dirty after Apply");

        // Cancel throws away what was typed since the last Apply.
        const int added = model->rowCount();
        QVERIFY(model->insertRows(added, 1));
        QVERIFY(filters.isDirty());
        filters.cancel();
        QVERIFY2(!filters.isDirty(), "Cancel left the page dirty");
        QCOMPARE(model->rowCount(), added);

        // Remove the fixture's filter through the page, which is also the
        // test for removal reaching the engine.
        for (int i = 0; i < model->rowCount(); ++i) {
            if (model->data(model->index(i, 0)).toString() == name) {
                QVERIFY(model->removeRows(i, 1));
                break;
            }
        }
        filters.apply();
        QVERIFY2(!engine->filters().contains(name), "removing a filter did not reach the engine");
        QCOMPARE(engine->filters(), before);
    }
};

QObject *createFilterSettingsPageTest()
{
    return new FilterSettingsPageTest;
}

#endif // WITH_TESTS

} // Help::Internal

#include "filtersettingspage.moc"
