// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "qbsprofilessettingspage.h"

#include "qbsprofilemanager.h"
#include "qbsprojectmanagerconstants.h"
#include "qbsprojectmanagertr.h"

#include <projectexplorer/devicesupport/devicekitaspects.h>
#include <projectexplorer/devicesupport/idevice.h>
#include <projectexplorer/kit.h>
#include <projectexplorer/kitmanager.h>
#include <utils/algorithm.h>
#include <utils/aspects.h>
#include <utils/qtcassert.h>
#include <utils/shutdownguard.h>
#include <utils/treemodel.h>

#include <QHash>

#ifdef WITH_TESTS
#include <QTest>
#endif

using namespace ProjectExplorer;
using namespace Utils;

namespace QbsProjectManager::Internal {

class ProfileTreeItem : public Utils::TypedTreeItem<ProfileTreeItem, ProfileTreeItem>
{
public:
    ProfileTreeItem() = default;
    ProfileTreeItem(const QString &key, const QString &value) : m_key(key), m_value(value) { }

private:
    QVariant data(int column, int role) const override
    {
        if (role != Qt::DisplayRole)
            return {};
        if (column == 0)
            return m_key;
        if (column == 1)
            return m_value;
        return {};
    }

    const QString m_key;
    const QString m_value;
};

class ProfileModel : public Utils::TreeModel<ProfileTreeItem>
{
public:
    explicit ProfileModel(QObject *parent) : TreeModel(parent)
    {
        setHeader(QStringList{Tr::tr("Key"), Tr::tr("Value")});
    }

    // The properties of one kit's profile, and nothing else. The widget page
    // built every profile into one tree and pointed the view at a branch of
    // it; a Qt Quick TreeView has no root index, and one profile is all the
    // page ever showed anyway.
    void reload(const Kit *kit)
    {
        auto newRoot = new ProfileTreeItem(QString(), QString());
        if (!kit) {
            setRootItem(newRoot);
            return;
        }
        const IDeviceConstPtr dev = BuildDeviceKitAspect::device(kit);
        if (!dev) {
            setRootItem(newRoot);
            return;
        }
        const QString profileName = QbsProfileManager::profileNameForKit(kit);
        const QStringList output = QbsProfileManager::runQbsConfig(
                                       dev, QbsProfileManager::QbsConfigOp::Get, "profiles")
                                       .split('\n', Qt::SkipEmptyParts);
        QHash<QStringList, ProfileTreeItem *> itemMap;
        for (QString line : output) {
            line = line.trimmed();
            line = line.mid(QString("profiles.").size());
            const int colonIndex = line.indexOf(':');
            if (colonIndex == -1)
                continue;
            QStringList key = line.left(colonIndex).trimmed().split('.', Qt::SkipEmptyParts);
            if (key.isEmpty() || key.first() != profileName)
                continue;
            // The profile's own name is the branch the view was rooted at, so
            // it is not a row any more.
            key.removeFirst();
            if (key.isEmpty())
                continue;
            const QString value = line.mid(colonIndex + 1).trimmed();
            QStringList partialKey;
            ProfileTreeItem *parent = newRoot;
            for (const QString &keyComponent : key) {
                partialKey << keyComponent;
                ProfileTreeItem *&item = itemMap[partialKey];
                if (!item) {
                    item = new ProfileTreeItem(keyComponent,
                                               partialKey == key ? value : QString());
                    parent->appendChild(item);
                }
                parent = item;
            }
        }
        setRootItem(newRoot);
    }
};

// The profile properties, handed to whichever renderer is drawing the page.
class ProfilePropertiesAspect final : public BaseAspect
{
public:
    explicit ProfilePropertiesAspect(AspectContainer *container)
        : BaseAspect(container)
        , m_model(this)
    {}

    AspectPresentation presentation() const override
    {
        AspectPresentation p = BaseAspect::presentation();
        p.control = AspectControls::Tree;
        return p;
    }

    QAbstractItemModel *tableModel() override { return &m_model; }

    void reload(const Kit *kit) { m_model.reload(kit); }

private:
    ProfileModel m_model;
};

// What the Profiles page shows. Nothing on it is editable - a qbs profile is
// what the kit and the build device add up to - so there is nothing to apply.
class QbsProfilesAspects final : public AspectContainer
{
public:
    QbsProfilesAspects()
    {
        setAutoApply(false);
        setQmlSource(QUrl("qrc:/qt/qml/QtCreator/QbsProjectManager/QbsProfilesPage.qml"));

        kit.setQmlName("Kit");
        kit.setLabelText(Tr::tr("Kit:"));
        kit.setDisplayStyle(SelectionAspect::DisplayStyle::ComboBox);

        profile.setQmlName("Profile");
        profile.setLabelText(Tr::tr("Associated profile:"));

        properties.setQmlName("Properties");
        properties.setLabelText(Tr::tr("Profile properties:"));

        connect(&kit, &BaseAspect::volatileValueChanged,
                this, &QbsProfilesAspects::showCurrentProfile);
        connect(QbsProfileManager::instance(), &QbsProfileManager::qbsProfilesUpdated,
                this, &QbsProfilesAspects::refreshKits);
        refreshKits();
    }

    SelectionAspect kit{this};
    TextDisplay profile{this};
    ProfilePropertiesAspect properties{this};

private:
    static QList<Kit *> validKits()
    {
        return Utils::filtered(KitManager::kits(), [](const Kit *k) { return k->isValid(); });
    }

    // Which kits there are to choose from, remembered by id across a refresh:
    // a position would follow another kit as soon as one in front of it went.
    void refreshKits()
    {
        const QVariant wanted = kit.itemValue();
        kit.clearOptions();
        for (const Kit *k : validKits())
            kit.addOption(SelectionAspect::Option(k->displayName(), {}, k->id().toSetting()));
        const int index = kit.indexForItemValue(wanted);
        kit.setValue(index < 0 ? 0 : index);
        showCurrentProfile();
    }

    void showCurrentProfile()
    {
        const Kit *current = KitManager::kit(Utils::Id::fromSetting(kit.itemValue()));
        profile.setText(current ? QbsProfileManager::ensureProfileForKit(current) : QString());
        properties.reload(current);
    }
};

#ifdef WITH_TESTS

// The page's kit list lived in a QComboBox's item data and the properties in a
// QTreeView rooted at a branch, so what it was showing could only be read back
// out of widgets.

class QbsProfilesSettingsTest : public QObject
{
    Q_OBJECT

private slots:
    void testTheKitsAreOfferedInOrder();
    void testTheKitIsRememberedByIdWhenTheListIsRefreshed();
    void testThePropertiesAreOneProfilesAndNotEveryones();
};

void QbsProfilesSettingsTest::testTheKitsAreOfferedInOrder()
{
    const QList<Kit *> kits
        = Utils::filtered(KitManager::kits(), [](const Kit *k) { return k->isValid(); });
    if (kits.isEmpty())
        QSKIP("No valid kit is configured, so there is nothing to offer.");

    QbsProfilesAspects page;
    QCOMPARE(page.kit.optionCount(), kits.size());
    QCOMPARE(page.kit.displayForIndex(0), kits.first()->displayName());
    // The id, not the position: what the page shows is looked up by it.
    QCOMPARE(Utils::Id::fromSetting(page.kit.itemValue()), kits.first()->id());
}

void QbsProfilesSettingsTest::testTheKitIsRememberedByIdWhenTheListIsRefreshed()
{
    const QList<Kit *> kits
        = Utils::filtered(KitManager::kits(), [](const Kit *k) { return k->isValid(); });
    if (kits.size() < 2)
        QSKIP("Fewer than two valid kits, so a position would do just as well.");

    QbsProfilesAspects page;
    page.kit.setValue(kits.size() - 1);
    const Utils::Id chosen = Utils::Id::fromSetting(page.kit.itemValue());
    QCOMPARE(chosen, kits.last()->id());

    // The list is rebuilt whenever qbs says the profiles changed.
    emit QbsProfileManager::instance()->qbsProfilesUpdated();

    QCOMPARE(Utils::Id::fromSetting(page.kit.itemValue()), chosen);
}

void QbsProfilesSettingsTest::testThePropertiesAreOneProfilesAndNotEveryones()
{
    QbsProfilesAspects page;
    QAbstractItemModel *properties = page.properties.tableModel();
    QVERIFY(properties);
    QCOMPARE(properties->columnCount({}), 2);
    QCOMPARE(properties->headerData(0, Qt::Horizontal).toString(), Tr::tr("Key"));

    // The widget page built every profile into one tree and pointed the view
    // at a branch of it. What is in the model now is the selected profile's
    // properties, so its own name is not a row: a tree that still had it would
    // have exactly one top-level row, whatever the profile holds.
    const Kit *current = KitManager::kit(Utils::Id::fromSetting(page.kit.itemValue()));
    if (!current)
        QSKIP("No valid kit is configured, so there is no profile to read.");
    if (properties->rowCount({}) == 0)
        QSKIP("qbs did not report any profile properties on this machine.");
    const QString profileName = QbsProfileManager::profileNameForKit(current);
    for (int row = 0; row < properties->rowCount({}); ++row)
        QVERIFY(properties->index(row, 0).data().toString() != profileName);
}

QObject *createQbsProfilesSettingsTest()
{
    return new QbsProfilesSettingsTest;
}

#endif // WITH_TESTS

QbsProfilesSettingsPage::QbsProfilesSettingsPage()
{
    setId("Y.QbsProfiles");
    setDisplayName(Tr::tr("Profiles"));
    setCategory(Constants::QBS_SETTINGS_CATEGORY);
    setSettingsProvider([] {
        static GuardedObject<QbsProfilesAspects> theProfilesAspects;
        return theProfilesAspects.get();
    });
}

} // QbsProjectManager::Internal

#include "qbsprofilessettingspage.moc"
