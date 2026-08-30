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

#include <qtcquick/aspectcontainermodel.h>

#ifdef WITH_TESTS
#include <QScopeGuard>
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

KitChooserAspect::KitChooserAspect(AspectContainer *container)
    : AspectContainer(container)
{
    // One row: the choice, then the way to the kit settings.
    setInlineRow(true);

    kit.setQmlName("Kit");
    kit.setLabelText(Tr::tr("Kit:"));
    kit.setDisplayStyle(SelectionAspect::DisplayStyle::ComboBox);

    manage.setQmlName("ManageKits");
    manage.setActionText(KitAspect::msgManage());
    manage.setAction([] { Core::ICore::showSettings(Constants::KITS_SETTINGS_PAGE_ID); });

    connect(KitManager::instance(), &KitManager::kitsChanged, this, [this] { populate(); });
    populate();
}

void KitChooserAspect::setKitPredicate(const Kit::Predicate &predicate)
{
    m_kitPredicate = predicate;
    populate();
}

void KitChooserAspect::setShowIcons(bool showIcons)
{
    m_showIcons = showIcons;
    populate();
}

void KitChooserAspect::populate()
{
    m_choices = kitChoices(KitManager::sortedKits(), activeKitForActiveProject(), m_kitPredicate);
    m_hasStartupKit = !m_choices.isEmpty() && m_choices.first().isActiveProjectKit;

    kit.clearOptions();
    for (const KitChoice &choice : std::as_const(m_choices)) {
        SelectionAspect::Option option{choice.displayName, choice.toolTip,
                                       choice.kitId.toSetting()};
        if (m_showIcons) {
            if (const Kit *const k = KitManager::kit(choice.kitId))
                option.icon = k->displayIcon();
        }
        kit.addOption(option);
    }

    const Id lastKit = Id::fromSetting(Core::ICore::settings()->value(lastKitKey));
    kit.setValue(qMax(0, initialKitChoice(m_choices, lastKit)));

    // Nothing to choose between is nothing to choose.
    kit.setEnabled(m_choices.size() > 1);
}

Id KitChooserAspect::currentKitId() const
{
    const int index = kit.value();
    return index >= 0 && index < m_choices.size() ? m_choices.at(index).kitId : Id();
}

Kit *KitChooserAspect::currentKit() const
{
    return KitManager::kit(currentKitId());
}

void KitChooserAspect::setCurrentKitId(Id id)
{
    for (int i = 0; i < m_choices.size(); ++i) {
        // Never the active project's entry: it stands for the project, not for
        // the kit it currently has.
        if (!m_choices.at(i).isActiveProjectKit && m_choices.at(i).kitId == id) {
            kit.setValue(i);
            return;
        }
    }
}

void KitChooserAspect::rememberChoice()
{
    const Id id = rememberedKitId(m_choices, kit.value());
    Core::ICore::settings()->setValueWithDefault(lastKitKey, id.toSetting(), Id().toSetting());
}

Id rememberedKitId(const QList<KitChoice> &choices, int index)
{
    if (index < 0 || index >= choices.size())
        return {};
    // The active project's entry stands for the project, not for the kit it
    // currently has, so there is no kit id to remember for it.
    return choices.at(index).isActiveProjectKit ? Id() : choices.at(index).kitId;
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

    void testTheChooserIsOneRowOnAForm()
    {
        // The widget put the combo box and the Manage button in a QHBoxLayout.
        // On a form that is an inline row, which is the only reason this is a
        // container rather than a bare SelectionAspect.
        KitChooserAspect chooser;

        // Asked of the model the renderer reads, not of the presentation:
        // inlineRow is what the container says, and InlineGroup is what that
        // is turned into - a delegate is chosen from the second.
        QCOMPARE(QtcQuick::AspectContainerModel::kindOf(&chooser),
                 QtcQuick::AspectContainerModel::InlineGroup);

        QCOMPARE(chooser.kit.presentation().control, Utils::AspectControls::ComboBox);
        QCOMPARE(chooser.manage.presentation().control, Utils::AspectControls::Button);
    }

    void testWhatTheChooserAnswers()
    {
        KitChooserAspect chooser;

        // Whatever kits this instance of Qt Creator has; the chooser is asked
        // rather than told, so the test works with none as well as with some.
        const int offered = chooser.kit.optionCount();
        // Said rather than passed quietly: with no kits the assertions below
        // hold for the wrong reason, and a green run would claim to have
        // checked something it did not.
        if (offered == 0)
            QSKIP("this Qt Creator has no kits, so the chooser has nothing to answer about");

        // What it answers is the kit behind the row it is on, not the row.
        QVERIFY(chooser.currentKitId().isValid() || chooser.hasStartupKit());

        // Nothing to choose between is nothing to choose. Made to happen
        // rather than hoped for: on a machine with several kits the rule and
        // "always enabled" give the same answer, and the check says nothing.
        const Id only = chooser.currentKitId();
        if (only.isValid()) {
            chooser.setKitPredicate([only](const Kit *k) { return k->id() == only; });
            QCOMPARE(chooser.kit.optionCount(), 1);
            QVERIFY2(!chooser.kit.isEnabled(), "a chooser with one entry offered a choice");
        }
    }

    void testWhatIsRememberedForWhichEntry()
    {
        // Asked of the rule rather than of a running chooser: whether there is
        // an active project is not something a test can arrange, and the
        // interesting entry only exists when there is one.
        const auto kit = makeKit("kit.a", "Desktop");
        const QList<KitChoice> withActive = kitChoices({kit.get()}, kit.get(), {});
        QCOMPARE(withActive.size(), 2);
        QVERIFY(withActive.at(0).isActiveProjectKit);

        QVERIFY2(!rememberedKitId(withActive, 0).isValid(),
                 "the active project's entry was remembered as the kit it currently has");
        QCOMPARE(rememberedKitId(withActive, 1), kit->id());

        // Nothing chosen is nothing remembered.
        QVERIFY(!rememberedKitId(withActive, -1).isValid());
        QVERIFY(!rememberedKitId({}, 0).isValid());
    }

    void testWhatIsRememberedAboutTheChoice()
    {
        // Written to the running instance's own settings, so what was there is
        // put back.
        const QVariant had = Core::ICore::settings()->value(lastKitKey);
        const QScopeGuard restore([had] {
            Core::ICore::settings()->setValue(lastKitKey, had);
        });

        KitChooserAspect chooser;
        if (chooser.kit.optionCount() == 0)
            QSKIP("this Qt Creator has no kits, so there is no choice to remember");

        if (chooser.hasStartupKit()) {
            // The active project's entry is remembered as an invalid id, so
            // that restoring it follows the project rather than whichever kit
            // the project had at the time.
            chooser.kit.setValue(0);
            chooser.rememberChoice();
            const Id remembered
                = Id::fromSetting(Core::ICore::settings()->value(lastKitKey));
            QVERIFY2(!remembered.isValid(),
                     "the active project's entry was remembered as the kit it currently has");
        }

        // An ordinary entry is remembered as itself.
        const int ordinary = chooser.hasStartupKit() ? 1 : 0;
        if (ordinary < chooser.kit.optionCount()) {
            chooser.kit.setValue(ordinary);
            chooser.rememberChoice();
            QCOMPARE(Id::fromSetting(Core::ICore::settings()->value(lastKitKey)),
                     chooser.currentKitId());
        }
    }

    void testAskingForAKitThatIsNotOffered()
    {
        KitChooserAspect chooser;
        if (chooser.kit.optionCount() < 2)
            QSKIP("this Qt Creator has fewer than two kits, so nothing can be moved off");

        // Moved off the entry populate() picked first: asking for a missing
        // kit while sitting on row 0 cannot tell "left alone" from "reset".
        chooser.kit.setValue(1);
        QCOMPARE(chooser.kit.value(), 1);

        // A kit the chooser does not offer leaves the choice alone rather than
        // clearing it: the caller is restoring a remembered id, and a kit that
        // has gone should not blank the box.
        chooser.setCurrentKitId(Id::fromString(QString("kit.that.is.not.there")));
        QCOMPARE(chooser.kit.value(), 1);
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
