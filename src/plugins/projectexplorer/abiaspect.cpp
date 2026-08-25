// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "abiaspect.h"

#include "projectexplorertr.h"

#include <utils/algorithm.h>
#include <utils/qtcassert.h>

#include <QScopeGuard>

#ifdef WITH_TESTS
#include <QSignalSpy>
#include <QTest>
#endif

using namespace Utils;

namespace ProjectExplorer {

// "unknown" last, everything else by name - the order the combo boxes had.
static bool pairLessThan(const QPair<QString, int> &lhs, const QPair<QString, int> &rhs)
{
    if (lhs.first == "unknown")
        return false;
    if (rhs.first == "unknown")
        return true;
    return lhs.first < rhs.first;
}

template<typename E>
static void addSorted(SelectionAspect &aspect, E last)
{
    QList<QPair<QString, int>> abis;
    for (int i = 0; i <= static_cast<int>(last); ++i)
        abis << qMakePair(Abi::toString(static_cast<E>(i)), i);

    Utils::sort(abis, &pairLessThan);

    aspect.clearOptions();
    for (const auto &[name, value] : std::as_const(abis))
        aspect.addOption({name, {}, value});
}

static void selectData(SelectionAspect &aspect, int data)
{
    const int index = aspect.indexForItemValue(data);
    aspect.setValue(index >= 0 ? index : qMax(0, aspect.optionCount() - 1));
}

static int selectedData(const SelectionAspect &aspect)
{
    return const_cast<SelectionAspect &>(aspect)
        .itemValueForIndex(aspect.volatileValue())
        .toInt();
}

AbiAspects::AbiAspects(AspectContainer *container)
    : AspectContainer(container)
{
    setAutoApply(false);
    // Six choices that read as one ABI, not six settings.
    setInlineRow(true);

    m_abi.setQmlName("Abi");
    m_abi.setDisplayStyle(SelectionAspect::DisplayStyle::ComboBox);
    m_abi.setUseDataAsSavedValue();

    m_architecture.setQmlName("Architecture");
    m_architecture.setDisplayStyle(SelectionAspect::DisplayStyle::ComboBox);
    m_architecture.setUseDataAsSavedValue();
    addSorted(m_architecture, Abi::UnknownArchitecture);

    m_os.setQmlName("Os");
    m_os.setDisplayStyle(SelectionAspect::DisplayStyle::ComboBox);
    m_os.setUseDataAsSavedValue();
    addSorted(m_os, Abi::UnknownOS);

    m_osFlavor.setQmlName("OsFlavor");
    m_osFlavor.setDisplayStyle(SelectionAspect::DisplayStyle::ComboBox);
    m_osFlavor.setUseDataAsSavedValue();

    m_binaryFormat.setQmlName("BinaryFormat");
    m_binaryFormat.setDisplayStyle(SelectionAspect::DisplayStyle::ComboBox);
    m_binaryFormat.setUseDataAsSavedValue();
    addSorted(m_binaryFormat, Abi::UnknownFormat);

    m_wordWidth.setQmlName("WordWidth");
    m_wordWidth.setDisplayStyle(SelectionAspect::DisplayStyle::ComboBox);
    m_wordWidth.setUseDataAsSavedValue();
    for (int width : {16, 32, 64, 0})
        m_wordWidth.addOption({Abi::toString(width), {}, width});
    m_wordWidth.setValue(m_wordWidth.optionCount() - 1);

    m_abi.addOnVolatileValueChanged(this, [this] { updateFromChoice(); });
    m_os.addOnVolatileValueChanged(this, [this] {
        if (m_updating)
            return;
        {
            m_updating = true;
            const QScopeGuard done([this] { m_updating = false; });
            refreshFlavors(static_cast<Abi::OS>(selectedData(m_os)));
        }
        updateFromParts();
    });
    for (SelectionAspect *part : {&m_architecture, &m_osFlavor, &m_binaryFormat, &m_wordWidth})
        part->addOnVolatileValueChanged(this, [this] { updateFromParts(); });

    setAbis({}, Abi::hostAbi());
}

QList<SelectionAspect *> AbiAspects::parts()
{
    return {&m_architecture, &m_os, &m_osFlavor, &m_binaryFormat, &m_wordWidth};
}

static Abi selectAbi(const Abi &current, const Abis &abiList)
{
    if (!current.isNull())
        return current;
    if (!abiList.isEmpty())
        return abiList.at(0);
    return Abi::hostAbi();
}

void AbiAspects::setAbis(const Abis &abis, const Abi &currentAbi)
{
    const Abi defaultAbi = selectAbi(currentAbi, abis);
    {
        m_updating = true;
        const QScopeGuard done([this] { m_updating = false; });

        m_abi.clearOptions();
        // The custom one first, carrying whatever the custom parts say. The
        // item value is the ABI itself, so that reading the choice back is
        // reading a string rather than an index.
        m_abi.addOption({Tr::tr("<custom>"), {}, defaultAbi.toString()});
        m_abi.setValue(0);
        m_abi.setVisible(!abis.isEmpty());

        for (const Abi &abi : abis) {
            const QString abiString = abi.toString();
            m_abi.addOption({abiString, {}, abiString});
            if (abi == defaultAbi)
                m_abi.setValue(m_abi.optionCount() - 1);
        }

        setPartsTo(defaultAbi);
    }
    updateFromChoice();
}

Abis AbiAspects::supportedAbis() const
{
    Abis result;
    auto &abi = const_cast<SelectionAspect &>(m_abi);
    result.reserve(abi.optionCount());
    for (int i = 1; i < abi.optionCount(); ++i)
        result << Abi::fromString(abi.itemValueForIndex(i).toString());
    return result;
}

bool AbiAspects::isCustomAbi() const
{
    return m_abi.volatileValue() == 0;
}

Abi AbiAspects::currentAbi() const
{
    return m_currentAbi;
}

void AbiAspects::refreshFlavors(Abi::OS os)
{
    QList<QPair<QString, int>> flavors
        = Utils::transform(Abi::flavorsForOs(os), [](Abi::OSFlavor flavor) {
              return QPair<QString, int>{Abi::toString(flavor), static_cast<int>(flavor)};
          });
    Utils::sort(flavors, pairLessThan);

    m_osFlavor.clearOptions();
    for (const auto &[name, value] : std::as_const(flavors))
        m_osFlavor.addOption({name, {}, value});
    m_osFlavor.setValue(0);
}

void AbiAspects::setPartsTo(const Abi &abi)
{
    selectData(m_architecture, abi.architecture());
    selectData(m_os, abi.os());
    refreshFlavors(abi.os());
    selectData(m_osFlavor, abi.osFlavor());
    selectData(m_binaryFormat, abi.binaryFormat());
    selectData(m_wordWidth, abi.wordWidth());
}

Abi AbiAspects::abiFromParts() const
{
    return Abi(static_cast<Abi::Architecture>(selectedData(m_architecture)),
               static_cast<Abi::OS>(selectedData(m_os)),
               static_cast<Abi::OSFlavor>(selectedData(m_osFlavor)),
               static_cast<Abi::BinaryFormat>(selectedData(m_binaryFormat)),
               static_cast<unsigned char>(selectedData(m_wordWidth)));
}

void AbiAspects::updateFromChoice()
{
    if (m_updating)
        return;

    const Abi chosen = Abi::fromString(
        const_cast<SelectionAspect &>(m_abi).itemValueForIndex(m_abi.volatileValue()).toString());
    const bool custom = isCustomAbi();

    for (SelectionAspect *part : parts())
        part->setEnabled(custom);

    {
        m_updating = true;
        const QScopeGuard done([this] { m_updating = false; });
        setPartsTo(chosen);
    }

    if (custom)
        updateFromParts();
    else if (chosen != m_currentAbi) {
        m_currentAbi = chosen;
        emit abiChanged();
    }
}

void AbiAspects::updateFromParts()
{
    if (m_updating)
        return;

    const Abi current = abiFromParts();
    // The custom entry remembers what was picked, so that going back to it
    // does not start from whatever was there before.
    m_abi.setOptionForIndex(0, {Tr::tr("<custom>"), {}, current.toString()});
    if (current == m_currentAbi)
        return;
    m_currentAbi = current;
    emit abiChanged();
}

#ifdef WITH_TESTS
class AbiAspectsTest final : public QObject
{
    Q_OBJECT

private slots:
    void testTheOfferedAbisComeBackOut()
    {
        // What a toolchain reported is what the page offers, and reading it
        // back is reading what was put in - the custom entry is not one of
        // them.
        AbiAspects aspects;
        const Abis offered{Abi::fromString("x86-linux-generic-elf-64bit"),
                           Abi::fromString("arm-linux-generic-elf-32bit")};
        aspects.setAbis(offered, offered.first());

        QCOMPARE(aspects.supportedAbis(), offered);
        QCOMPARE(aspects.currentAbi(), offered.first());
        QVERIFY(!aspects.isCustomAbi());
        QVERIFY(aspects.choice().isVisible());

        // Nothing to offer leaves only the custom one, and nothing to pick
        // from.
        aspects.setAbis({}, Abi::hostAbi());
        QVERIFY(aspects.supportedAbis().isEmpty());
        QVERIFY(aspects.isCustomAbi());
        QVERIFY(!aspects.choice().isVisible());
    }

    void testThePartsAreOnlyTheUsersInCustomMode()
    {
        // Picking one of the offered ABIs is picking all five parts at once,
        // so they are not the user's to change.
        AbiAspects aspects;
        const Abis offered{Abi::fromString("x86-linux-generic-elf-64bit")};
        aspects.setAbis(offered, offered.first());

        QVERIFY(!aspects.isCustomAbi());
        for (Utils::SelectionAspect *part : aspects.parts())
            QVERIFY(!part->isEnabled());

        aspects.choice().setValue(0);
        QVERIFY(aspects.isCustomAbi());
        for (Utils::SelectionAspect *part : aspects.parts())
            QVERIFY(part->isEnabled());
    }

    void testPickingAnOfferedAbiSetsThePartsToIt()
    {
        AbiAspects aspects;
        const Abis offered{Abi::fromString("x86-linux-generic-elf-64bit"),
                           Abi::fromString("arm-linux-generic-elf-32bit")};
        aspects.setAbis(offered, offered.first());

        aspects.choice().setValue(2); // the second offered one
        QCOMPARE(aspects.currentAbi(), offered.at(1));

        // The five parts show what was picked, even though they are not the
        // user's to change while an offered ABI is chosen.
        const auto partValue = [&aspects](int index) {
            Utils::SelectionAspect * const part = aspects.parts().at(index);
            return part->itemValueForIndex(part->volatileValue()).toInt();
        };
        QCOMPARE(partValue(0), int(offered.at(1).architecture()));
        QCOMPARE(partValue(1), int(offered.at(1).os()));
        QCOMPARE(partValue(4), int(offered.at(1).wordWidth()));

        // The custom entry keeps its own ABI rather than the last one picked,
        // so going back to it goes back to what was being built there. That is
        // what the widget did, and changing it is not this conversion's to do.
        aspects.choice().setValue(0);
        QCOMPARE(aspects.currentAbi(), offered.first());
    }

    void testTheCustomEntryRemembersWhatWasBuiltThere()
    {
        // Wandering off to one of the offered ABIs and back must not throw
        // away what the user was assembling by hand.
        AbiAspects aspects;
        const Abis offered{Abi::fromString("x86-linux-generic-elf-64bit")};
        aspects.setAbis(offered, offered.first());

        aspects.choice().setValue(0);
        QVERIFY(aspects.isCustomAbi());
        Utils::SelectionAspect * const wordWidth = aspects.parts().at(4);
        int index32 = -1;
        for (int i = 0; i < wordWidth->optionCount(); ++i) {
            if (wordWidth->itemValueForIndex(i).toInt() == 32)
                index32 = i;
        }
        QVERIFY(index32 >= 0);
        wordWidth->setValue(index32);
        QCOMPARE(aspects.currentAbi().wordWidth(), 32);

        aspects.choice().setValue(1);
        QCOMPARE(aspects.currentAbi(), offered.first());

        aspects.choice().setValue(0);
        QCOMPARE(aspects.currentAbi().wordWidth(), 32);
    }

    void testTheFlavoursOfferedFollowTheOs()
    {
        // Which flavours there are is the OS's answer, and it changes while
        // the page is open.
        AbiAspects aspects;
        aspects.setAbis({}, Abi::fromString("x86-linux-generic-elf-64bit"));
        QVERIFY(aspects.isCustomAbi());

        Utils::SelectionAspect * const os = aspects.parts().at(1);
        Utils::SelectionAspect * const flavor = aspects.parts().at(2);

        const auto flavourNames = [flavor] {
            QStringList names;
            for (int i = 0; i < flavor->optionCount(); ++i)
                names << flavor->displayForIndex(i);
            return names;
        };

        const auto selectOs = [os](Abi::OS wanted) {
            for (int i = 0; i < os->optionCount(); ++i) {
                if (os->itemValueForIndex(i).toInt() == int(wanted)) {
                    os->setValue(i);
                    return true;
                }
            }
            return false;
        };

        QVERIFY(selectOs(Abi::LinuxOS));
        const QStringList linuxFlavours = flavourNames();
        QVERIFY(!linuxFlavours.isEmpty());

        QVERIFY(selectOs(Abi::WindowsOS));
        const QStringList windowsFlavours = flavourNames();
        QVERIFY(!windowsFlavours.isEmpty());
        QVERIFY2(windowsFlavours != linuxFlavours,
                 "the flavours did not follow the operating system");

        // And what the aspect says the ABI is followed too.
        QCOMPARE(aspects.currentAbi().os(), Abi::WindowsOS);
    }

    void testACustomAbiIsWhateverThePartsSay()
    {
        AbiAspects aspects;
        aspects.setAbis({}, Abi::fromString("x86-linux-generic-elf-64bit"));

        Utils::SelectionAspect * const wordWidth = aspects.parts().at(4);
        int index32 = -1;
        for (int i = 0; i < wordWidth->optionCount(); ++i) {
            if (wordWidth->itemValueForIndex(i).toInt() == 32)
                index32 = i;
        }
        QVERIFY(index32 >= 0);

        QSignalSpy changed(&aspects, &AbiAspects::abiChanged);
        wordWidth->setValue(index32);
        QCOMPARE(aspects.currentAbi().wordWidth(), 32);
        QCOMPARE(changed.count(), 1);

        // Setting it to what it already is is not a change.
        wordWidth->setValue(index32);
        QCOMPARE(changed.count(), 1);
    }
};

QObject *createAbiAspectsTest()
{
    return new AbiAspectsTest;
}
#endif // WITH_TESTS

} // namespace ProjectExplorer

#include "abiaspect.moc"
