// Copyright (C) 2024 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "kitaspect.h"

#include "devicesupport/devicekitaspects.h"
#include "devicesupport/idevice.h"
#include "kit.h"
#include "kitmanager.h"
#include "projectexplorertr.h"

#include <coreplugin/icore.h>

#include <utils/aspectwidgets.h>
#include <utils/algorithm.h>
#include <utils/environment.h>
#include <utils/guard.h>
#include <utils/guiutils.h>
#include <utils/layoutbuilder.h>
#include <utils/treemodel.h>

#include <QAction>
#include <QComboBox>
#include <QLabel>
#include <QPushButton>

#include <utility>

using namespace Core;
#ifdef WITH_TESTS
#include <QTest>
#endif

using namespace Utils;

static const char DETECTIONSOURCETYPE[] = "DetectionSource.type";
static const char DETECTIONSOURCEID[] = "DetectionSource.id";

namespace ProjectExplorer {

void DetectionSource::fromMap(const Store &store)
{
    type = static_cast<DetectionType>(store.value(DETECTIONSOURCETYPE).toInt());
    id = store.value(DETECTIONSOURCEID).toString();
}

void DetectionSource::toMap(Store &store) const
{
    store.insert(DETECTIONSOURCETYPE, static_cast<int>(type));
    store.insert(DETECTIONSOURCEID, id);
}

std::optional<DetectionSource> DetectionSource::createFromMap(const Store &store)
{
    if (store.contains(DETECTIONSOURCETYPE) && store.contains(DETECTIONSOURCEID)) {
        DetectionSource ds;
        ds.fromMap(store);
        return ds;
    }
    return {};
}

namespace {

class KitAspectComboBox final : public QComboBox
{
public:
    using QComboBox::QComboBox;

    bool event(QEvent *e) final
    {
        if (e->type() == QEvent::ToolTip)
            setToolTip(itemData(currentIndex(), Qt::ToolTipRole).toString());
        return QComboBox::event(e);
    }
};

// The selection a KitAspect offers. A SelectionAspect that also carries the
// aspect's "Mark as Mutable", which is about the setting rather than about
// which item is picked, so both renderers put it on the control's right-click.
class KitSelectionAspect final : public SelectionAspect
{
public:
    // Registered by the owner rather than here: a container frees only what it
    // was told to own, and a kit aspect is made afresh for every kit.
    explicit KitSelectionAspect(KitAspect *owner)
        : m_owner(owner)
    {}

    AspectPresentation presentation() const override
    {
        AspectPresentation p = SelectionAspect::presentation();
        if (m_owner->offersMutability()) {
            const QAction * const action = m_owner->mutableAction();
            p.contextActionText = action->text();
            p.contextActionChecked = action->isChecked();
            p.contextActionEnabled = action->isEnabled();
        }
        return p;
    }

    void triggerContextAction(bool checked) override { m_owner->triggerContextAction(checked); }

private:
    KitAspect * const m_owner;
};

class KitAspectSortModel final : public SortModel
{
public:
    using SortModel::SortModel;

private:
    bool lessThan(const QModelIndex &source_left, const QModelIndex &source_right) const override
    {
        const auto getValue = [&](const QModelIndex &index, KitAspect::ItemRole role) {
            return sourceModel()->data(index, role);
        };
        const auto getValues = [&]<typename T>(KitAspect::ItemRole role, const T & /* dummy */) {
            return std::make_pair(
                sourceModel()->data(source_left, role).value<T>(),
                sourceModel()->data(source_right, role).value<T>());
        };

        // Criterion 1: "None" item come last.
        if (getValue(source_left, KitAspect::IsNoneRole).toBool())
            return false;

        if (getValue(source_right, KitAspect::IsNoneRole).toBool())
            return true;

        // Criterion 2: "Type", which is is the name of some category by which the entries
        //              are supposed to get grouped together.
        if (const auto [type1, type2] = getValues(KitAspect::TypeRole, QString()); type1 != type2)
            return type1 < type2;

        // Criterion 3: "Quality", i.e. how likely is the respective entry to be usable.
        if (const auto [qual1, qual2] = getValues(KitAspect::QualityRole, int()); qual1 != qual2)
            return qual1 > qual2;

        // Criterion 4: Name.
        return SortModel::lessThan(source_left, source_right);
    }
};

class KitAspectFactories
{
public:
    void onKitsLoaded() const
    {
        for (KitAspectFactory *factory : m_aspectList)
            factory->onKitsLoaded();
    }

    void addKitAspect(KitAspectFactory *factory)
    {
        QTC_ASSERT(!m_aspectList.contains(factory), return);
        m_aspectList.append(factory);
        m_aspectListIsSorted = false;
    }

    void removeKitAspect(KitAspectFactory *factory)
    {
        int removed = m_aspectList.removeAll(factory);
        QTC_CHECK(removed == 1);
    }

    const QList<KitAspectFactory *> kitAspectFactories()
    {
        if (!m_aspectListIsSorted) {
            Utils::sort(m_aspectList, [](const KitAspectFactory *a, const KitAspectFactory *b) {
                return a->priority() > b->priority();
            });
            m_aspectListIsSorted = true;
        }
        return m_aspectList;
    }

    // Sorted by priority, in descending order...
    QList<KitAspectFactory *> m_aspectList;
    // ... if this here is set:
    bool m_aspectListIsSorted = true;
};

static KitAspectFactories &kitAspectFactoriesStorage()
{
    static KitAspectFactories theKitAspectFactories;
    return theKitAspectFactories;
}

} // namespace

class KitAspect::Private
{
public:
    Private(Kit *kit, const KitAspectFactory *f)
        : kit(kit)
        , factory(f)
    {}

    Kit *kit;
    const KitAspectFactory * const factory;
    QAction *mutableAction = nullptr;
    Id managingPageId;
    ActionAspect *manageButton = nullptr;
    Guard ignoreChanges;
    QList<KitAspect *> aspectsToEmbed;

    struct ListAspect
    {
        ListAspect(const ListAspectSpec &spec, SelectionAspect *selection, SortModel *sorted)
            : spec(spec)
            , selection(selection)
            , sorted(sorted)
        {}
        ListAspectSpec spec;
        SelectionAspect *selection;
        // The model in the order it is shown in, which is not the order it
        // comes in: see KitAspectSortModel.
        SortModel *sorted;
    };
    QList<ListAspect> listAspects;

    bool readOnly = false;
};

KitAspect::KitAspect(Kit *k, const KitAspectFactory *factory)
    : d(new Private(k, factory))
{
    // One row: what it is called, what it holds, and the page that manages it.
    setInlineRow(true);
    setLabelText(factory->displayName() + ':');
    setToolTip(factory->description());

    connect(KitManager::instance(), &KitManager::kitRemoved, this, [this](Kit *k) {
        if (k == d->kit)
            d->kit = nullptr;
    });

    const Id id = factory->id();
    d->mutableAction = new QAction(Tr::tr("Mark as Mutable"));
    d->mutableAction->setCheckable(true);
    d->mutableAction->setChecked(k->isMutable(id));
    d->mutableAction->setEnabled(!k->isSticky(id));
    connect(d->mutableAction, &QAction::toggled, this, [this, id] {
        if (Kit *k = kit())
            k->setMutable(id, d->mutableAction->isChecked());
    });
}

KitAspect::~KitAspect()
{
    delete d->mutableAction;
    delete d;
}

void KitAspect::refresh()
{
    if (d->listAspects.isEmpty() || d->ignoreChanges.isLocked())
        return;
    const GuardLocker locker(d->ignoreChanges);

    Kit *k = kit();
    if (!k)
        return;

    for (const Private::ListAspect &la : std::as_const(d->listAspects)) {
        // Refilling the list and picking what the kit already says is not the
        // user changing anything.
        DirtySettingsGuard guard;

        la.spec.resetModel();
        la.sorted->sort(0);
        la.selection->clearOptions();
        for (int row = 0, n = la.sorted->rowCount(); row < n; ++row) {
            const QModelIndex index = la.sorted->index(row, 0);
            SelectionAspect::Option option{index.data(Qt::DisplayRole).toString(),
                                           index.data(Qt::ToolTipRole).toString(),
                                           index.data(IdRole)};
            option.icon = index.data(Qt::DecorationRole).value<QIcon>();
            la.selection->addOption(option);
        }

        const QVariant itemId = la.spec.getter(*k);
        int idx = la.selection->indexForItemValue(itemId);
        if (idx == -1) {
            idx = la.selection->optionCount() - 1;
            if (QTC_UNEXPECTED(itemId.isValid())) {
                qWarning() << factory()->displayName();
                const QVariant newId = idx < 0 ? QVariant() : la.selection->itemValueForIndex(idx);
                la.spec.setter(*kit(), newId);
            }
        }
        la.selection->setValue(qMax(0, idx));
        la.selection->setEnabled(!d->readOnly && la.selection->optionCount() > 1);
    }
}

void KitAspect::makeStickySubWidgetsReadOnly()
{
    auto kit = this->kit();
    QTC_ASSERT(kit, return);

    if (!kit->isSticky(d->factory->id()))
        return;

    if (d->manageButton)
        d->manageButton->setEnabled(false);

    d->readOnly = true;
    makeReadOnly(true);
}

void KitAspect::reload()
{
    if (d->readOnly) {
        d->readOnly = false;
        if (d->manageButton)
            d->manageButton->setEnabled(true);
        makeReadOnly(false);
    }
    const Id id = factory()->id();
    if (Kit *k = kit()) {
        d->mutableAction->setChecked(k->isMutable(id));
        d->mutableAction->setEnabled(!k->isSticky(id));
    }
    valueToVolatileValue();
    refresh();
}

void KitAspect::makeReadOnly(bool readOnly)
{
    for (const Private::ListAspect &la : std::as_const(d->listAspects))
        la.selection->setEnabled(!readOnly);
}

void KitAspect::addToInnerLayout(Layouting::Layout &layout)
{
    addListAspectsToLayout(layout);
}

void KitAspect::addListAspectSpec(const ListAspectSpec &listAspectSpec)
{
    const auto selection = new KitSelectionAspect(this);
    registerAspect(selection, /*takeOwnership=*/true);
    selection->setDisplayStyle(SelectionAspect::DisplayStyle::ComboBox);
    const auto sortModel = new KitAspectSortModel(this);
    sortModel->setSourceModel(listAspectSpec.model);
    d->listAspects.emplaceBack(listAspectSpec, selection, sortModel);

    refresh();

    selection->addOnVolatileValueChanged(this, [this, listAspectSpec, selection] {
        if (d->ignoreChanges.isLocked())
            return;

        if (Kit *k = kit())
            listAspectSpec.setter(*k, selection->itemValue());
    });
    connect(listAspectSpec.model, &QAbstractItemModel::modelAboutToBeReset,
            this, [this] { d->ignoreChanges.lock(); });
    connect(listAspectSpec.model, &QAbstractItemModel::modelReset,
            this, [this] { d->ignoreChanges.unlock(); });
}

QList<SelectionAspect *> KitAspect::listAspects() const
{
    return Utils::transform(d->listAspects, &Private::ListAspect::selection);
}

void KitAspect::addLabelToLayout(Layouting::Layout &layout)
{
    auto label = Utils::AspectWidgets::createSubWidget<QLabel>(this, d->factory->displayName() + ':');
    label->setToolTip(d->factory->description());
    connect(label, &QLabel::linkActivated, this, [this](const QString &link) {
        emit labelLinkActivated(link);
    });

    layout.addItem(label);
}

void KitAspect::addListAspectsToLayout(Layouting::Layout &layout)
{
    for (const Private::ListAspect &la : std::as_const(d->listAspects))
        layout.addItem(la.selection);
}

void KitAspect::addManageButtonToLayout(Layouting::Layout &layout)
{
    if (d->manageButton)
        layout.addItem(d->manageButton);
}

void KitAspect::addToLayoutImpl(Layouting::Layout &layout)
{
    addLabelToLayout(layout);
    addToInnerLayout(layout);
    addManageButtonToLayout(layout);

    layout.flush();
}

// The run device is the one thing a run configuration may not override.
bool KitAspect::offersMutability() const
{
    return factory()->id() != RunDeviceKitAspect::id() && d->mutableAction;
}

void KitAspect::addMutableAction(QWidget *child)
{
    QTC_ASSERT(child, return);
    if (!offersMutability())
        return;
    child->addAction(d->mutableAction);
    child->setContextMenuPolicy(Qt::ActionsContextMenu);
}

void KitAspect::triggerContextAction(bool checked)
{
    QTC_ASSERT(d->mutableAction, return);
    d->mutableAction->setChecked(checked);
}

void KitAspect::setManagingPage(Id pageId)
{
    d->managingPageId = pageId;
    if (!pageId.isValid())
        return;
    d->manageButton = new ActionAspect;
    registerAspect(d->manageButton, /*takeOwnership=*/true);
    d->manageButton->setActionText(msgManage());
    d->manageButton->setAction([this] {
        Core::ICore::showSettings(d->managingPageId, settingsPageItemToPreselect());
    });
}

void KitAspect::setAspectsToEmbed(const QList<KitAspect *> &aspects)
{
    d->aspectsToEmbed = aspects;
}

QList<KitAspect *> KitAspect::aspectsToEmbed() const
{
    return d->aspectsToEmbed;
}

QString KitAspect::msgManage()
{
    return Tr::tr("Manage...");
}

Kit *KitAspect::kit() const
{
    return d->kit;
}

const KitAspectFactory *KitAspect::factory() const
{
    return d->factory;
}

QAction *KitAspect::mutableAction() const
{
    return d->mutableAction;
}

KitAspectFactory::KitAspectFactory()
{
    kitAspectFactoriesStorage().addKitAspect(this);
}

KitAspectFactory::~KitAspectFactory()
{
    kitAspectFactoriesStorage().removeKitAspect(this);
}

int KitAspectFactory::weight(const Kit *k) const
{
    return k->value(id()).isValid() ? 1 : 0;
}

QVariant KitAspectFactory::getInfo(const Kit *k, Id request, const QVariant &input) const
{
    Q_UNUSED(k)
    Q_UNUSED(request)
    Q_UNUSED(input)
    return {};
}

void KitAspectFactory::addToBuildEnvironment(const Kit *k, Environment &env) const
{
    Q_UNUSED(k)
    Q_UNUSED(env)
}

void KitAspectFactory::addToRunEnvironment(const Kit *k, Environment &env) const
{
    Q_UNUSED(k)
    Q_UNUSED(env)
}

QList<OutputLineParser *> KitAspectFactory::createOutputParsers(const Kit *k) const
{
    Q_UNUSED(k)
    return {};
}

QSet<Id> KitAspectFactory::supportedPlatforms(const Kit *k) const
{
    Q_UNUSED(k)
    return {};
}

QSet<Id> KitAspectFactory::availableFeatures(const Kit *k) const
{
    Q_UNUSED(k)
    return {};
}

void KitAspectFactory::addToMacroExpander(Kit *k, MacroExpander *expander) const
{
    Q_UNUSED(k)
    Q_UNUSED(expander)
}

void KitAspectFactory::notifyAboutUpdate(Kit *k)
{
    if (k)
        k->kitUpdated();
}

void KitAspectFactory::handleKitsLoaded()
{
    kitAspectFactoriesStorage().onKitsLoaded();
}

const QList<KitAspectFactory *> KitAspectFactory::kitAspectFactories()
{
    return kitAspectFactoriesStorage().kitAspectFactories();
}

std::optional<QtTaskTree::ExecutableItem> KitAspectFactory::autoDetect(
    Kit *kit,
    const FilePaths &searchPaths,
    const DetectionSource &detectionSource,
    const LogCallback &logCallback) const
{
    Q_UNUSED(kit);
    Q_UNUSED(searchPaths);
    Q_UNUSED(detectionSource);
    Q_UNUSED(logCallback);

    return std::nullopt;
}

std::optional<QtTaskTree::ExecutableItem> KitAspectFactory::removeAutoDetected(
    const QString &detectionSourceId, const LogCallback &logCallback) const
{
    Q_UNUSED(detectionSourceId);
    Q_UNUSED(logCallback);
    return std::nullopt;
}

void KitAspectFactory::listAutoDetected(
    const QString &detectionSourceId, const LogCallback &logCallback) const
{
    Q_UNUSED(detectionSourceId)
    Q_UNUSED(logCallback)
}

Result<QtTaskTree::ExecutableItem> KitAspectFactory::createAspectFromJson(
    const DetectionSource &detectionSource,
    const FilePath &rootPath,
    Kit *kit,
    const QJsonValue &json,
    const LogCallback &logCallback) const
{
    Q_UNUSED(detectionSource);
    Q_UNUSED(kit);
    Q_UNUSED(json);
    Q_UNUSED(logCallback);
    Q_UNUSED(rootPath);
    return ResultError(
        Tr::tr("Kit aspect factory \"%1\" does not support creating aspects from JSON.")
            .arg(id().toString()));
}

using Group = QtTaskTree::Group; // trick lupdate, QTBUG-140636
Group kitDetectionRecipe(
    const IDeviceConstPtr &device,
    DetectionSource::DetectionType detectionType,
    const LogCallback &logCallback)
{
    using namespace QtTaskTree;

    Storage<GroupItems> detectorItems;
    Storage<Kit *> kit;

    const DetectionSource detectionSource{detectionType, device->id().toString()};

    const auto setup = [kit, detectorItems, device, detectionSource, logCallback] {
        const auto root = device->rootPath();

        const FilePaths searchPaths
            = Utils::transform(device->systemEnvironment().path(), [&root](const FilePath &path) {
                  return root.withNewPath(path.path());
              });

        const QString detectionSourceId = device->id().toString();

        logCallback(Tr::tr("Auto detecting kits for device: %1").arg(device->displayName()));

        *kit = KitManager::registerKit([detectionSourceId, device, detectionSource](Kit *k) {
            k->setDetectionSource(detectionSource);
            k->setUnexpandedDisplayName("%{Device:Name}");

            RunDeviceTypeKitAspect::setDeviceTypeId(k, device->type());
            RunDeviceKitAspect::setDevice(k, device);
            BuildDeviceTypeKitAspect::setDeviceTypeId(k, device->type());
            BuildDeviceKitAspect::setDevice(k, device);

            k->setSticky(BuildDeviceKitAspect::id(), true);
            k->setSticky(BuildDeviceTypeKitAspect::id(), true);
        });

        for (const auto &factory : KitAspectFactory::kitAspectFactories()) {
            const auto detector
                = factory->autoDetect(*kit, searchPaths, detectionSource, logCallback);
            if (detector)
                detectorItems->append({*detector});
        }
    };

    const auto setupDetectorTree = [detectorItems](QTaskTree &tree) {
        tree.setRecipe(Group(*detectorItems));
    };

    // clang-format off
    return Group {
        kit, detectorItems,
        QSyncTask(setup),
        QTaskTreeTask(setupDetectorTree),
        QSyncTask([kit,logCallback] {
            if (!(*kit)->isValid()) {
                KitManager::deregisterKit(*kit);
                return;
            }

            // The kit registered in "setup" needs to update the kit aspects
            // found by "setupDetectorTree"
            (*kit)->fix();

            logCallback(Tr::tr("Found kit: %1.").arg((*kit)->displayName()));
        }),
    };
    // clang-format on
}

Group removeDetectedKitsRecipe(const IDeviceConstPtr &device, const LogCallback &logCallback)
{
    using namespace QtTaskTree;

    const auto root = device->rootPath();
    const QString detectionSource = device->id().toString();

    GroupItems removerItems{};
    for (const auto &factory : KitAspectFactory::kitAspectFactories()) {
        const auto remover = factory->removeAutoDetected(detectionSource, logCallback);
        if (remover)
            removerItems.append({*remover});
    }

    const auto removeKits = [device, detectionSource, logCallback]() {
        logCallback(Tr::tr("Removing kits for device: %1.").arg(device->displayName()));

        const auto detectedKits = filtered(KitManager::kits(), [detectionSource](const Kit *k) {
            return k->detectionSource().id == detectionSource;
        });

        for (Kit *kit : detectedKits) {
            logCallback(Tr::tr("Removing kit: %1.").arg(kit->displayName()));
            KitManager::deregisterKit(kit);
        }
    };

    // clang-format off
    return Group {
        QSyncTask(removeKits),
        Group {
            parallelIdealThreadCountLimit,
            removerItems,
        }
    };
    // clang-format on
}

void listAutoDetected(const IDeviceConstPtr &device, const LogCallback &logCallback)
{
    const QString detectionSource = device->id().toString();

    for (const auto kit : KitManager::kits()) {
        if (kit->detectionSource().id == detectionSource)
            logCallback(Tr::tr("Kit: %1.").arg(kit->displayName()));
    }

    for (const auto &factory : KitAspectFactory::kitAspectFactories())
        factory->listAutoDetected(detectionSource, logCallback);
}

#ifdef WITH_TESTS
class KitAspectTest final : public QObject
{
    Q_OBJECT

private slots:
    void testAKitAspectOffersItsListAsAnAspect()
    {
        // What a kit aspect holds used to live in the QComboBox it built, so
        // only a widget page could show it. It is a SelectionAspect now, and
        // says what the kit says.
        Kit * const kit = anyKit();
        if (!kit)
            QSKIP("No kits are configured here");
        const std::unique_ptr<KitAspect> aspect = aspectWithAList(kit);
        if (!aspect)
            QSKIP("No kit aspect here offers a list");

        const QList<Utils::SelectionAspect *> lists = aspect->listAspects();
        QVERIFY(!lists.isEmpty());
        Utils::SelectionAspect * const list = lists.first();
        QVERIFY(list->optionCount() > 0);
        // Every entry carries the id it stands for, not just its text: what is
        // written back has to survive the list being refilled.
        for (int i = 0; i < list->optionCount(); ++i)
            QVERIFY(list->itemValueForIndex(i).isValid() || i == list->optionCount() - 1);

        // And the aspect is one row of the page, which is what makes a
        // renderer able to draw it without knowing what it is.
        const Utils::AspectPresentation p = aspect->presentation();
        QCOMPARE(p.control, Utils::AspectControls::Container);
        QVERIFY(p.inlineRow);
        QVERIFY(!p.labelText.isEmpty());
    }

    void testPickingFromTheListWritesToTheKit()
    {
        Kit * const kit = anyKit();
        if (!kit)
            QSKIP("No kits are configured here");
        const std::unique_ptr<KitAspect> aspect = aspectWithAList(kit, AtLeastTwoOptions);
        if (!aspect)
            QSKIP("No kit aspect here offers a choice");

        Utils::SelectionAspect * const list = aspect->listAspects().first();
        const int was = list->volatileValue();
        const int other = was == 0 ? 1 : 0;
        const QVariant otherId = list->itemValueForIndex(other);

        list->setValue(other);
        // The kit is what holds the value; the aspect is only how it is picked.
        QCOMPARE(aspect->listAspects().first()->itemValue(), otherId);
        aspect->refresh();
        QCOMPARE(aspect->listAspects().first()->itemValue(), otherId);

        list->setValue(was);
    }

    void testTheListOffersMarkAsMutable()
    {
        // "Mark as Mutable" is about the setting rather than about which item
        // is picked, so it is a right-click on the control. It used to be a
        // QAction put on a QComboBox, which no other renderer could see.
        Kit * const kit = anyKit();
        if (!kit)
            QSKIP("No kits are configured here");
        const std::unique_ptr<KitAspect> aspect = aspectWithAList(kit, AnyOptions, Mutable);
        if (!aspect)
            QSKIP("No kit aspect here can be marked mutable");

        Utils::SelectionAspect * const list = aspect->listAspects().first();
        const Utils::AspectPresentation p = list->presentation();
        QVERIFY(!p.contextActionText.isEmpty());

        const Utils::Id id = aspect->factory()->id();
        const bool was = kit->isMutable(id);
        list->triggerContextAction(!was);
        QCOMPARE(kit->isMutable(id), !was);
        list->triggerContextAction(was);
        QCOMPARE(kit->isMutable(id), was);
    }

    void testADeviceTypeIsToldApartByMoreThanItsName()
    {
        // The device types are drawn with an icon each. That travelled as a
        // model role into a QComboBox; a descriptor with no icon in it would
        // have lost it silently.
        Kit * const kit = anyKit();
        if (!kit)
            QSKIP("No kits are configured here");

        for (KitAspectFactory * const factory : KitManager::kitAspectFactories()) {
            const std::unique_ptr<KitAspect> aspect(factory->createKitAspect(kit));
            if (!aspect || aspect->listAspects().isEmpty())
                continue;
            const Utils::AspectPresentation p = aspect->listAspects().first()->presentation();
            const bool anyIcon = Utils::anyOf(p.choices, [](const auto &c) {
                return !c.icon.isNull();
            });
            if (anyIcon)
                return;
        }
        QFAIL("No kit aspect offers a choice with an icon");
    }

private:
    enum OptionCount { AnyOptions, AtLeastTwoOptions };
    enum Mutability { AnyMutability, Mutable };

    static Kit *anyKit()
    {
        const QList<Kit *> kits = KitManager::kits();
        return kits.isEmpty() ? nullptr : kits.first();
    }

    static std::unique_ptr<KitAspect> aspectWithAList(
        Kit *kit, OptionCount count = AnyOptions, Mutability mutability = AnyMutability)
    {
        for (KitAspectFactory * const factory : KitManager::kitAspectFactories()) {
            std::unique_ptr<KitAspect> aspect(factory->createKitAspect(kit));
            if (!aspect || aspect->listAspects().isEmpty())
                continue;
            if (count == AtLeastTwoOptions && aspect->listAspects().first()->optionCount() < 2)
                continue;
            if (mutability == Mutable && !aspect->offersMutability())
                continue;
            return aspect;
        }
        return {};
    }
};

QObject *createKitAspectTest()
{
    return new KitAspectTest;
}
#endif // WITH_TESTS

QDebug operator<<(QDebug dbg, const DetectionSource &source)
{
    dbg.nospace() << "DetectionSource(";
    dbg.nospace() << "type: " << source.type;
    if (!source.id.isEmpty())
        dbg.nospace() << ", id: " << source.id;
    dbg.nospace() << ")";
    return dbg;
}

} // namespace ProjectExplorer

#ifdef WITH_TESTS
#include "kitaspect.moc"
#endif
