// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "kitchooser.h"

#include "kitaspect.h"
#include "kitmanager.h"
#include "projectexplorerconstants.h"
#include "projectexplorertr.h"
#include "projectmanager.h"
#include "target.h"

#include <coreplugin/icore.h>

#ifdef WITH_TESTS
#include <QTest>
#endif

#include <QComboBox>
#include <QHBoxLayout>
#include <QPushButton>

using namespace Core;
using namespace Utils;

namespace ProjectExplorer {

const char lastKitKey[] = "LastSelectedKit";

KitChooser::KitChooser(QWidget *parent) :
    QWidget(parent),
    m_kitPredicate([](const Kit *k) { return k->isValid(); })
{
    m_chooser = new QComboBox(this);
    m_chooser->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Fixed);
    m_manageButton = new QPushButton(KitAspect::msgManage(), this);

    auto layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_chooser);
    layout->addWidget(m_manageButton);
    setFocusProxy(m_manageButton);

    connect(m_chooser, &QComboBox::currentIndexChanged, this, &KitChooser::onCurrentIndexChanged);
    connect(m_chooser, &QComboBox::activated, this, &KitChooser::onActivated);
    connect(m_manageButton, &QAbstractButton::clicked, this, &KitChooser::onManageButtonClicked);
    connect(KitManager::instance(), &KitManager::kitsChanged, this, &KitChooser::populate);
}

void KitChooser::onManageButtonClicked()
{
    Core::ICore::showSettings(Constants::KITS_SETTINGS_PAGE_ID);
}

void KitChooser::setShowIcons(bool showIcons)
{
    m_showIcons = showIcons;
    populate();
}

void KitChooser::onCurrentIndexChanged()
{
    const Id id = Id::fromSetting(m_chooser->currentData());
    Kit *kit = KitManager::kit(id);
    setToolTip(kit ? kitToolTip(kit) : QString());
    emit currentIndexChanged();
}

void KitChooser::onActivated()
{
    // Active user interaction.
    Id id = Id::fromSetting(m_chooser->currentData());
    if (m_hasStartupKit && m_chooser->currentIndex() == 0)
        id = Id(); // Special value to indicate startup kit.
    ICore::settings()->setValueWithDefault(lastKitKey, id.toSetting(), Id().toSetting());
    emit activated();
}

QString KitChooser::kitText(const Kit *k) const
{
    return k->displayName();
}

QString KitChooser::kitToolTip(Kit *k) const
{
    return k->toHtml();
}

QList<KitChoice> kitChoices(const QList<Kit *> &sortedKits, Kit *activeProjectKit,
                            const Kit::Predicate &predicate)
{
    QList<KitChoice> choices;

    if (activeProjectKit && (!predicate || predicate(activeProjectKit))) {
        choices.append({activeProjectKit->id(),
                        Tr::tr("Kit of Active Project: %1").arg(activeProjectKit->displayName()),
                        activeProjectKit->toHtml(),
                        true});
    }

    for (Kit *const kit : sortedKits) {
        if (!predicate || predicate(kit))
            choices.append({kit->id(), kit->displayName(), kit->toHtml(), false});
    }

    return choices;
}

int initialKitChoice(const QList<KitChoice> &choices, Utils::Id lastChosen)
{
    if (choices.isEmpty())
        return -1;

    if (lastChosen.isValid()) {
        for (int i = 0; i < choices.size(); ++i) {
            // The active project's entry is not what "last chosen" names: it
            // is remembered as an invalid id precisely so that it follows the
            // project rather than a kit.
            if (!choices.at(i).isActiveProjectKit && choices.at(i).kitId == lastChosen)
                return i;
        }
    }

    // Nothing chosen before: the active project's kit if it is offered, and
    // otherwise the first thing there is.
    return 0;
}

void KitChooser::populate()
{
    m_chooser->clear();
    m_hasStartupKit = false;

    const QList<KitChoice> choices = kitChoices(KitManager::sortedKits(),
                                                activeKitForActiveProject(),
                                                m_kitPredicate);
    const Id lastKit = Id::fromSetting(ICore::settings()->value(lastKitKey));
    const int chosen = initialKitChoice(choices, lastKit);

    for (int i = 0; i < choices.size(); ++i) {
        const KitChoice &choice = choices.at(i);
        m_chooser->addItem(choice.displayName, choice.kitId.toSetting());
        const int pos = m_chooser->count() - 1;
        m_chooser->setItemData(pos, choice.toolTip, Qt::ToolTipRole);
        if (m_showIcons) {
            if (Kit *const kit = KitManager::kit(choice.kitId))
                m_chooser->setItemData(pos, kit->displayIcon(), Qt::DecorationRole);
        }
        if (i == chosen)
            m_chooser->setCurrentIndex(pos);
        // A rule of its own rather than one more thing the list happens to
        // contain: what follows the active project's kit is a different kind
        // of entry.
        if (choice.isActiveProjectKit) {
            m_hasStartupKit = true;
            m_chooser->insertSeparator(m_chooser->count());
        }
    }

    const int n = m_chooser->count();
    m_chooser->setEnabled(n > 1);
    setFocusProxy(n > 1 ? static_cast<QWidget *>(m_chooser)
                        : static_cast<QWidget *>(m_manageButton));
}

Kit *KitChooser::currentKit() const
{
    const Id id = Id::fromSetting(m_chooser->currentData());
    return KitManager::kit(id);
}

Kit *KitChooser::lastKit()
{
    const Id id = Id::fromSetting(ICore::settings()->value(lastKitKey));
    if (!id.isValid())
        return activeKitForActiveProject();
    return KitManager::kit(id);
}

void KitChooser::setCurrentKitId(Utils::Id id)
{
    QVariant v = id.toSetting();
    for (int i = 0, n = m_chooser->count(); i != n; ++i) {
        if (m_chooser->itemData(i) == v) {
            m_chooser->setCurrentIndex(i);
            break;
        }
    }
}

Utils::Id KitChooser::currentKitId() const
{
    Kit *kit = currentKit();
    return kit ? kit->id() : Utils::Id();
}

void KitChooser::setKitPredicate(const Kit::Predicate &predicate)
{
    m_kitPredicate = predicate;
    populate();
}

#ifdef WITH_TESTS

class KitChooserTest final : public QObject
{
    Q_OBJECT

    static std::unique_ptr<Kit> makeKit(const QString &id, const QString &name)
    {
        auto kit = std::make_unique<Kit>(Id::fromString(id));
        kit->setUnexpandedDisplayName(name);
        return kit;
    }

private slots:
    void testWhatAChooserOffers()
    {
        const auto first = makeKit("kit.a", "Desktop");
        const auto second = makeKit("kit.b", "Embedded");

        const QList<KitChoice> plain = kitChoices({first.get(), second.get()}, nullptr, {});
        QCOMPARE(plain.size(), 2);
        QCOMPARE(plain.at(0).displayName, QString("Desktop"));
        QVERIFY(!plain.at(0).isActiveProjectKit);

        // The active project's kit comes first and is named as such, so it can
        // be told from the same kit appearing again in the list below.
        const QList<KitChoice> withActive
            = kitChoices({first.get(), second.get()}, second.get(), {});
        QCOMPARE(withActive.size(), 3);
        QVERIFY(withActive.at(0).isActiveProjectKit);
        QCOMPARE(withActive.at(0).kitId, second->id());
        QVERIFY2(withActive.at(0).displayName.contains(QString("Embedded")),
                 "the active project's kit was offered without saying which kit it is");
        QVERIFY2(withActive.at(0).displayName != withActive.at(2).displayName,
                 "the same kit appears twice under the same name");
    }

    void testAKitTheDialogCannotUse()
    {
        const auto usable = makeKit("kit.a", "Desktop");
        const auto other = makeKit("kit.b", "Embedded");
        const auto predicate = [&](const Kit *k) { return k == usable.get(); };

        const QList<KitChoice> choices
            = kitChoices({usable.get(), other.get()}, nullptr, predicate);
        QCOMPARE(choices.size(), 1);
        QCOMPARE(choices.first().kitId, usable->id());

        // Including the active project's, which is not exempt: a dialog that
        // cannot use it must not offer it.
        const QList<KitChoice> withActive
            = kitChoices({usable.get(), other.get()}, other.get(), predicate);
        QCOMPARE(withActive.size(), 1);
        QVERIFY2(!withActive.first().isActiveProjectKit,
                 "the active project's kit was offered although it was refused");
    }

    void testWhichEntryItOpensOn()
    {
        const auto first = makeKit("kit.a", "Desktop");
        const auto second = makeKit("kit.b", "Embedded");
        const QList<KitChoice> choices
            = kitChoices({first.get(), second.get()}, second.get(), {});

        // What was chosen last, found among the ordinary entries.
        QCOMPARE(initialKitChoice(choices, second->id()), 2);
        QCOMPARE(initialKitChoice(choices, first->id()), 1);

        // Nothing chosen before opens on the active project's kit, which is
        // the first entry when there is one.
        QCOMPARE(initialKitChoice(choices, {}), 0);
        QVERIFY(choices.at(0).isActiveProjectKit);

        // A kit that is no longer there falls back rather than choosing
        // nothing.
        QCOMPARE(initialKitChoice(choices, Id::fromString(QString("kit.gone"))), 0);

        // And the active project's entry is never what "last chosen" names: it
        // is remembered as an invalid id so that it follows the project.
        QVERIFY2(initialKitChoice(choices, second->id()) != 0,
                 "the active project's entry was matched by a kit id");

        QCOMPARE(initialKitChoice({}, first->id()), -1);
    }
};

QObject *createKitChooserTest()
{
    return new KitChooserTest;
}

#endif // WITH_TESTS

} // namespace ProjectExplorer

#ifdef WITH_TESTS
#include "kitchooser.moc"
#endif
