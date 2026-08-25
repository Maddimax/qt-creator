// Copyright (C) 2016 Denis Shienkov <denis.shienkov@gmail.com>
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "debugserverproviderssettingspage.h"

#include "baremetalconstants.h"
#include "baremetaltr.h"
#include "debugserverprovidermanager.h"
#include "idebugserverprovider.h"

#include <coreplugin/dialogs/ioptionspage.h>
#include <coreplugin/icore.h>

#ifdef WITH_TESTS
#include <QTest>
#endif

#include <utils/shutdownguard.h>

#include <projectexplorer/projectexplorerconstants.h>

#include <utils/algorithm.h>
#include <utils/detailswidget.h>
#include <utils/guiutils.h>
#include <utils/layoutbuilder.h>
#include <utils/qtcassert.h>

#include <QAction>
#include <QApplication>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QTextStream>
#include <QTreeView>

using namespace Debugger;
using namespace Utils;

namespace BareMetal::Internal {

// DebugServerProviderNode

enum {
    ProviderNameColumn = 0,
    ProviderTypeColumn,
    ProviderEngineColumn
};

static QString engineTypeName(DebuggerEngineType engineType)
{
    switch (engineType) {
    case NoEngineType:
        return Tr::tr("Not recognized");
    case GdbEngineType:
        return Tr::tr("GDB");
    case UvscEngineType:
        return Tr::tr("UVSC");
    default:
        return {};
    }
}

static QString engineTypeDescription(DebuggerEngineType engineType)
{
    switch (engineType) {
    case NoEngineType:
        return Tr::tr("Not recognized");
    case GdbEngineType:
        return Tr::tr("GDB compatible provider engine\n" \
                      "(used together with the GDB debuggers).");
    case UvscEngineType:
        return Tr::tr("UVSC compatible provider engine\n" \
                      "(used together with the KEIL uVision).");
    default:
        return {};
    }
}

class DebugServerProviderNode final : public TreeItem
{
public:
    explicit DebugServerProviderNode(IDebugServerProvider *provider, bool changed = false)
        : provider(provider), changed(changed)
    {
    }

    QVariant data(int column, int role) const final
    {
        if (role == Qt::FontRole) {
            QFont f = QApplication::font();
            if (changed)
                f.setBold(true);
            return f;
        }

        if (role == Qt::DisplayRole) {
            if (column == ProviderNameColumn)
                return provider->displayName();
            if (column == ProviderTypeColumn)
                return provider->typeDisplayName();
            if (column == ProviderEngineColumn)
                return engineTypeName(provider->engineType());
        } else if (role == Qt::ToolTipRole) {
            if (column == ProviderEngineColumn)
                return engineTypeDescription(provider->engineType());
        }

        return {};
    }

    IDebugServerProvider *provider = nullptr;
    bool changed = false;
};

// DebugServerProviderModel

DebugServerProviderModel::DebugServerProviderModel()
{
    setHeader({Tr::tr("Name"), Tr::tr("Type"), Tr::tr("Engine")});

    const DebugServerProviderManager *manager = DebugServerProviderManager::instance();

    connect(manager, &DebugServerProviderManager::providerAdded,
            this, &DebugServerProviderModel::addProvider);
    connect(manager, &DebugServerProviderManager::providerRemoved,
            this, &DebugServerProviderModel::removeProvider);

    for (IDebugServerProvider *p : DebugServerProviderManager::providers())
        addProvider(p);
}

IDebugServerProvider *DebugServerProviderModel::provider(const QModelIndex &index) const
{
    if (const DebugServerProviderNode *node = nodeForIndex(index))
        return node->provider;

    return nullptr;
}

DebugServerProviderNode *DebugServerProviderModel::nodeForIndex(const QModelIndex &index) const
{
    if (!index.isValid())
        return nullptr;

    return static_cast<DebugServerProviderNode *>(itemForIndex(index));
}

void DebugServerProviderModel::apply()
{
    // Remove unused providers
    for (IDebugServerProvider *provider : std::as_const(m_providersToRemove))
        DebugServerProviderManager::deregisterProvider(provider);
    QTC_ASSERT(m_providersToRemove.isEmpty(), m_providersToRemove.clear());

    // Update providers
    for (TreeItem *item : *rootItem()) {
        const auto n = static_cast<DebugServerProviderNode *>(item);
        if (!n->changed)
            continue;

        QTC_CHECK(n->provider);
        n->provider->apply();

        n->changed = false;
        n->update();
    }

    // Add new (and already updated) providers
    QStringList skippedProviders;
    for (IDebugServerProvider *provider: std::as_const(m_providersToAdd)) {
        if (!DebugServerProviderManager::registerProvider(provider))
            skippedProviders << provider->displayName();
    }

    m_providersToAdd.clear();

    if (!skippedProviders.isEmpty()) {
        QMessageBox::warning(Core::ICore::dialogParent(),
                             Tr::tr("Duplicate Providers Detected"),
                             Tr::tr("The following providers were already configured:<br>"
                                "&nbsp;%1<br>"
                                "They were not configured again.")
                             .arg(skippedProviders.join(",<br>&nbsp;")));
    }
}

DebugServerProviderNode *DebugServerProviderModel::findNode(const IDebugServerProvider *provider) const
{
    auto test = [provider](TreeItem *item) {
        return static_cast<DebugServerProviderNode *>(item)->provider == provider;
    };

    return static_cast<DebugServerProviderNode *>(Utils::findOrDefault(*rootItem(), test));
}

QModelIndex DebugServerProviderModel::indexForProvider(IDebugServerProvider *provider) const
{
    const DebugServerProviderNode *n = findNode(provider);
    return n ? indexForItem(n) : QModelIndex();
}

void DebugServerProviderModel::markForRemoval(IDebugServerProvider *provider)
{
    DebugServerProviderNode *n = findNode(provider);
    QTC_ASSERT(n, return);
    destroyItem(n);

    if (m_providersToAdd.contains(provider)) {
        m_providersToAdd.removeOne(provider);
        delete provider;
    } else {
        m_providersToRemove.append(provider);
    }
}

void DebugServerProviderModel::markForAddition(IDebugServerProvider *provider)
{
    DebugServerProviderNode *n = createNode(provider, true);
    rootItem()->appendChild(n);
    m_providersToAdd.append(provider);
}

DebugServerProviderNode *DebugServerProviderModel::createNode(
        IDebugServerProvider *provider, bool changed)
{
    const auto node = new DebugServerProviderNode(provider, changed);
    // What the page used to hear from the configuration widget it built: the
    // provider's own aspects say it now.
    connect(provider, &BaseAspect::volatileValueChanged, this, [node] {
        node->changed = true;
        node->update();
    });
    return node;
}

void DebugServerProviderModel::addProvider(IDebugServerProvider *provider)
{
    if (findNode(provider))
        m_providersToAdd.removeOne(provider);
    else
        rootItem()->appendChild(createNode(provider, false));

    emit providerStateChanged();
}

void DebugServerProviderModel::removeProvider(IDebugServerProvider *provider)
{
    m_providersToRemove.removeAll(provider);
    if (DebugServerProviderNode *n = findNode(provider))
        destroyItem(n);

    emit providerStateChanged();
}

// The providers, as the page lists them: name, kind and engine. All of that
// is the model's answer already.
class ProviderTreeAspect final : public BaseAspect
{
    Q_OBJECT

public:
    ProviderTreeAspect(AspectContainer *container, DebugServerProviderModel &model)
        : BaseAspect(container)
        , m_model(model)
    {}

    AspectPresentation presentation() const override
    {
        AspectPresentation p = BaseAspect::presentation();
        p.control = AspectControls::Tree;
        return p;
    }

    QAbstractItemModel *tableModel() override { return &m_model; }
    DebugServerProviderModel &model() { return m_model; }

    // Which provider the form beside the tree is about. The view says so; the
    // page reads it.
    Q_INVOKABLE void setCurrentIndex(const QModelIndex &index)
    {
        if (index == m_current)
            return;
        m_current = index;
        emit currentChanged();
    }

    QModelIndex currentIndex() const { return m_current; }

signals:
    void currentChanged();

private:
    DebugServerProviderModel &m_model;
    QPersistentModelIndex m_current;
};

class DebugServerProvidersSettingsWidget final : public AspectContainer
{
public:
    DebugServerProvidersSettingsWidget();

    void apply() final { m_model.apply(); }

private:
    void showCurrentProvider();
    void updateState();
    void addProviderToModel(IDebugServerProvider *provider);

    DebugServerProviderModel m_model;

    AspectContainer m_group{this};
    ProviderTreeAspect m_providers{&m_group, m_model};
    ActionAspect m_add{&m_group};
    ActionAspect m_clone{&m_group};
    ActionAspect m_remove{&m_group};
    // What the selected provider asks for. Rebuilt per selection, and owned so
    // that the previous one goes when the next arrives.
    ContainerAspect m_current{&m_group};

    IDebugServerProvider *m_shown = nullptr;
};

DebugServerProvidersSettingsWidget::DebugServerProvidersSettingsWidget()
{
    setAutoApply(false);
    setQmlSource(QUrl("qrc:/qt/qml/QtCreator/BareMetal/DebugServerProvidersPage.qml"));

    m_group.setQmlName("Providers");
    m_group.setLabelText(Tr::tr("Debug Server Providers"));

    m_providers.setQmlName("List");

    // Adding a provider is picking a kind, so the button offers rather than
    // does. The kinds are fixed once the plugins are loaded.
    m_add.setQmlName("Add");
    m_add.setActionText(Tr::tr("Add"));
    QList<AspectPresentation::Choice> kinds;
    for (const IDebugServerProviderFactory *f : IDebugServerProviderFactory::factories())
        kinds.append({f->displayName(), {}, true, f->id()});
    m_add.setChoices(kinds);
    m_add.setOnChoice([this](const QVariant &id) {
        for (const IDebugServerProviderFactory *f : IDebugServerProviderFactory::factories()) {
            if (f->id() == id.toString()) {
                addProviderToModel(f->create());
                return;
            }
        }
    });

    m_clone.setQmlName("Clone");
    m_clone.setActionText(Tr::tr("Clone"));
    m_clone.setAction([this] {
        const IDebugServerProvider *old = m_model.provider(m_providers.currentIndex());
        QTC_ASSERT(old, return);
        for (const IDebugServerProviderFactory *f : IDebugServerProviderFactory::factories()) {
            if (!old->id().startsWith(f->id()))
                continue;
            IDebugServerProvider *p = f->create();
            Store map;
            old->toMap(map);
            p->fromMap(map);
            p->setDisplayName(Tr::tr("Clone of %1").arg(old->displayName()));
            p->resetId();
            addProviderToModel(p);
            return;
        }
    });

    m_remove.setQmlName("Remove");
    m_remove.setActionText(Tr::tr("Remove"));
    m_remove.setAction([this] {
        IDebugServerProvider * const p = m_model.provider(m_providers.currentIndex());
        QTC_ASSERT(p, return);
        showCurrentProvider();
        m_providers.setCurrentIndex({});
        m_model.markForRemoval(p);
    });

    m_current.setQmlName("Current");

    // Behaviour, not layout.
    connect(&m_providers, &ProviderTreeAspect::currentChanged, this, [this] {
        showCurrentProvider();
        updateState();
    });
    connect(&m_model, &DebugServerProviderModel::providerStateChanged,
            this, &DebugServerProvidersSettingsWidget::updateState);
    connect(DebugServerProviderManager::instance(),
            &DebugServerProviderManager::providersChanged,
            this, &DebugServerProvidersSettingsWidget::updateState);

    updateState();
}

void DebugServerProvidersSettingsWidget::showCurrentProvider()
{
    IDebugServerProvider * const provider = m_model.provider(m_providers.currentIndex());
    if (provider == m_shown)
        return;
    m_shown = provider;

    if (!provider) {
        m_current.setOwnedContainer(nullptr);
        return;
    }

    const auto rows = new AspectContainer;
    // An ordering container has no opinion about applying, and the default is
    // to apply at once - which on a page with Apply and Cancel is wrong.
    rows->setAutoApply(false);
    provider->addSettingsRows(*rows);
    m_current.setOwnedContainer(rows);
}

void DebugServerProvidersSettingsWidget::addProviderToModel(IDebugServerProvider *provider)
{
    QTC_ASSERT(provider, return);
    m_model.markForAddition(provider);
    m_providers.setCurrentIndex(m_model.indexForProvider(provider));
    showCurrentProvider();
    updateState();
}

void DebugServerProvidersSettingsWidget::updateState()
{
    bool canCopy = false;
    bool canDelete = false;
    if (const IDebugServerProvider *p = m_model.provider(m_providers.currentIndex())) {
        canCopy = p->isValid();
        canDelete = true;
    }

    m_clone.setEnabled(canCopy);
    m_remove.setEnabled(canDelete);
}

// DebugServerProvidersSettingsPage

class DebugServerProvidersSettingsPage final : public Core::IOptionsPage
{
public:
    DebugServerProvidersSettingsPage()
    {
        setId(Constants::DEBUG_SERVER_PROVIDERS_SETTINGS_ID);
        setDisplayName(Tr::tr("Bare Metal"));
        setCategory(ProjectExplorer::Constants::DEVICE_SETTINGS_CATEGORY);
        setSettingsProvider([] {
            static GuardedObject<DebugServerProvidersSettingsWidget> theAspects;
            return theAspects.get();
        });
    }
};

static const DebugServerProvidersSettingsPage settingsPage;

#ifdef WITH_TESTS
class DebugServerProvidersPageTest final : public QObject
{
    Q_OBJECT

private slots:
    void testEveryKindOfProviderSaysWhatItAsks()
    {
        // The configuration widget was the extension point, with eleven
        // classes behind it. A provider says which of its aspects a page shows
        // instead, and every kind has to answer - a kind that says nothing
        // draws an empty box, which is what the iOS device used to do.
        const QList<IDebugServerProviderFactory *> factories
            = IDebugServerProviderFactory::factories();
        QVERIFY(!factories.isEmpty());

        for (const IDebugServerProviderFactory *factory : factories) {
            std::unique_ptr<IDebugServerProvider> provider(factory->create());
            QVERIFY2(provider, qPrintable(factory->displayName()));

            AspectContainer rows;
            rows.setAutoApply(false);
            provider->addSettingsRows(rows);

            const QList<BaseAspect *> aspects = rows.aspects();
            QVERIFY2(!aspects.isEmpty(), qPrintable(factory->displayName()));
            // Every provider is asked its name, whatever kind it is.
            QVERIFY2(Utils::anyOf(aspects, [](BaseAspect *a) {
                         return a->settingsKey() == Key("DisplayName");
                     }), qPrintable(factory->displayName()));
            // And listing them must not turn them into aspects that apply as
            // they are typed: this page has Apply and Cancel.
            for (BaseAspect * const row : aspects) {
                QVERIFY2(!row->isAutoApply(),
                         qPrintable(factory->displayName() + ' ' + row->labelText()));
            }
        }
    }

    void testWhatAProviderHoldsSurvivesASaveAndReload()
    {
        // The values used to live in the provider's own members and the
        // controls in a widget; they are aspects now, and toMap()/fromMap()
        // go through them. The settings keys have to be the ones already on
        // disk, or every configured provider comes back empty.
        for (const IDebugServerProviderFactory *factory :
             IDebugServerProviderFactory::factories()) {
            std::unique_ptr<IDebugServerProvider> original(factory->create());
            QVERIFY(original);
            original->setDisplayName("A name nobody would pick");
            original->setChannel("192.0.2.1", 4711);

            Store store;
            original->toMap(store);
            QVERIFY2(!store.isEmpty(), qPrintable(factory->displayName()));

            std::unique_ptr<IDebugServerProvider> restored(factory->restore(store));
            QVERIFY2(restored, qPrintable(factory->displayName()));
            QCOMPARE(restored->displayName(), QString("A name nobody would pick"));
            QCOMPARE(restored->channel().host(), QString("192.0.2.1"));
            QCOMPARE(restored->channel().port(), 4711);
            // Same id, so a restored provider is the same provider.
            QCOMPARE(restored->id(), original->id());
        }
    }
};

QObject *createDebugServerProvidersPageTest()
{
    return new DebugServerProvidersPageTest;
}
#endif // WITH_TESTS

} // BareMetal::Internal

#include "debugserverproviderssettingspage.moc"
