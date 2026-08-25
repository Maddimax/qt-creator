// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "toolchainconfigaspects.h"

#include "toolchain.h"
#include "projectexplorerconstants.h"
#include "projectexplorertr.h"
#include "toolchainmanager.h"

#include <utils/aspectwidgets.h>
#include <utils/commandline.h>
#include <utils/detailswidget.h>
#include <utils/pathchooser.h>
#include <utils/layoutbuilder.h>
#include <utils/qtcassert.h>

#include <QFormLayout>
#include <QScrollArea>

#ifdef WITH_TESTS
#include <QTest>
#endif

using namespace Utils;

namespace ProjectExplorer {

class ToolchainConfigAspects::Private
{
public:
    explicit Private(const ToolchainBundle &bundle)
        : bundle(bundle)
    {}

    ToolchainBundle bundle;
    QList<std::pair<const Toolchain *, FilePathAspect *>> commands;
    BoolAspect *manualCxxCompiler = nullptr;
};

ToolchainConfigAspects::ToolchainConfigAspects(const ToolchainBundle &bundle)
    : d(std::make_unique<Private>(bundle))
{
    setAutoApply(false);

    m_displayName.setQmlName("Name");
    m_displayName.setLabelText(Tr::tr("Name:"));
    m_displayName.setDisplayStyle(StringAspect::LineEditDisplay);
    m_displayName.setValue(bundle.displayName());

    m_errorMessage.setQmlName("Error");
    m_errorMessage.setIconType(InfoType::Error);
    m_errorMessage.setWordWrap(true);
    m_errorMessage.setVisible(false);

    // MSVC is found rather than pointed at, so it has no compiler commands to
    // ask about.
    if (bundle.type() == Constants::MSVC_TOOLCHAIN_TYPEID)
        return;

    const bool onlyOne = int(bundle.toolchains().size()) == 1;
    bundle.forEach<Toolchain>([&](const Toolchain &tc) {
        auto command = new FilePathAspect;
        registerAspect(command, /*takeOwnership=*/true);
        command->setQmlName(tc.language().toString());
        command->setLabelText(onlyOne
                                  ? Tr::tr("&Compiler path")
                                  //: %1 = programming language
                                  : Tr::tr("%1 compiler path")
                                        .arg(ToolchainManager::displayNameOfLanguageId(
                                            tc.language())));
        command->setExpectedKind(PathChooserKind::ExistingCommand);
        command->setHistoryCompleter("PE.ToolChainCommand.History");
        command->setAllowPathFromDevice(true);
        command->setValue(tc.compilerCommand());
        d->commands << std::make_pair(&tc, command);

        if (tc.language() == Constants::CXX_LANGUAGE_ID
            && bundle.factory()->supportedLanguages().contains(Constants::C_LANGUAGE_ID)) {
            d->manualCxxCompiler = new BoolAspect;
            registerAspect(d->manualCxxCompiler, /*takeOwnership=*/true);
            d->manualCxxCompiler->setQmlName("ManualCxxCompiler");
            d->manualCxxCompiler->setLabelText(Tr::tr("Provide manually"));
            d->manualCxxCompiler->setLabelPlacement(BoolAspect::LabelPlacement::AtCheckBox);
            d->manualCxxCompiler->setValue(tc.isManuallyProvidedCxxCompiler());
        }

        command->addOnVolatileValueChanged(this, [this, &tc] {
            if (tc.language() == Constants::C_LANGUAGE_ID)
                deriveCxxCompilerCommand();
        });
    });

    if (d->manualCxxCompiler) {
        const auto updateDerived = [this] {
            if (FilePathAspect *cxx = compilerCommand(Constants::CXX_LANGUAGE_ID))
                cxx->setEnabled(d->manualCxxCompiler->volatileValue());
            deriveCxxCompilerCommand();
        };
        d->manualCxxCompiler->addOnVolatileValueChanged(this, updateDerived);
        updateDerived();
    }
}

ToolchainConfigAspects::~ToolchainConfigAspects() = default;

ToolchainBundle ToolchainConfigAspects::bundle() const
{
    return d->bundle;
}

StringAspect &ToolchainConfigAspects::displayName()
{
    return m_displayName;
}

FilePathAspect *ToolchainConfigAspects::compilerCommand(Utils::Id language)
{
    for (const auto &[tc, command] : std::as_const(d->commands)) {
        if (tc->language() == language)
            return command;
    }
    return nullptr;
}

BoolAspect *ToolchainConfigAspects::manualCxxCompiler()
{
    return d->manualCxxCompiler;
}

QList<std::pair<const Toolchain *, FilePathAspect *>> ToolchainConfigAspects::compilerCommands() const
{
    return d->commands;
}

bool ToolchainConfigAspects::isDirty() const
{
    if (m_displayName.volatileValue() != d->bundle.displayName())
        return true;
    for (const auto &[tc, command] : std::as_const(d->commands)) {
        if (command->expandedVolatileValue() != d->bundle.compilerCommand(tc->language()))
            return true;
    }
    return false;
}

void ToolchainConfigAspects::apply()
{
    AspectContainer::apply();
    d->bundle.setDisplayName(m_displayName.volatileValue());
    if (d->bundle.detectionSource().isAutoDetected())
        return;
    for (const auto &[tc, command] : std::as_const(d->commands))
        d->bundle.setCompilerCommand(tc->language(), command->expandedVolatileValue());
    d->bundle.setCxxCompilerIsManuallyProvided(
        d->manualCxxCompiler && d->manualCxxCompiler->volatileValue());
}

void ToolchainConfigAspects::makeReadOnly()
{
    m_displayName.setEnabled(false);
    for (const auto &[_, command] : std::as_const(d->commands))
        command->setReadOnly(true);
    if (d->manualCxxCompiler)
        d->manualCxxCompiler->setEnabled(false);
}

void ToolchainConfigAspects::setFallbackBrowsePath(const FilePath &path)
{
    for (const auto &[_, command] : std::as_const(d->commands))
        command->setInitialBrowsePathBackup(path);
}

void ToolchainConfigAspects::setCommandVersionArguments(const QStringList &args)
{
    for (const auto &[_, command] : std::as_const(d->commands))
        command->setCommandVersionArguments(args);
}

bool ToolchainConfigAspects::hasAnyCompiler() const
{
    for (const auto &[_, command] : std::as_const(d->commands)) {
        if (command->expandedVolatileValue().isExecutableFile())
            return true;
    }
    return false;
}

void ToolchainConfigAspects::deriveCxxCompilerCommand()
{
    if (!d->manualCxxCompiler || d->manualCxxCompiler->volatileValue())
        return;

    using namespace Constants;
    FilePathAspect * const c = compilerCommand(C_LANGUAGE_ID);
    FilePathAspect * const cxx = compilerCommand(CXX_LANGUAGE_ID);
    QTC_ASSERT(c && cxx, return);
    if (!c->expandedVolatileValue().isExecutableFile())
        return;
    if (const FilePath cxxCmd = d->bundle.factory()->correspondingCompilerCommand(
            c->expandedVolatileValue(), CXX_LANGUAGE_ID);
        cxxCmd.isExecutableFile()) {
        cxx->setValue(cxxCmd);
    }
}

#ifdef WITH_TESTS
class ToolchainConfigAspectsTest final : public QObject
{
    Q_OBJECT

private slots:
    void testTheAspectsSayWhatTheBundleSays()
    {
        // What every toolchain is asked is the same, and it is asked of the
        // bundle rather than of the widgets that used to hold it.
        const std::optional<ToolchainBundle> bundle = anyBundle();
        if (!bundle)
            QSKIP("No toolchains are configured here");

        ToolchainConfigAspects aspects(*bundle);
        QCOMPARE(aspects.displayName().volatileValue(), bundle->displayName());
        QVERIFY(!aspects.isDirty());

        // A compiler command per language the bundle has one for, holding what
        // the bundle holds.
        QVERIFY(!aspects.compilerCommands().isEmpty());
        for (const auto &[tc, command] : aspects.compilerCommands()) {
            QVERIFY(command);
            QCOMPARE(command->expandedVolatileValue(),
                     bundle->compilerCommand(tc->language()));
        }

        // And a change is something the page can see without asking a widget.
        aspects.displayName().setValue(bundle->displayName() + " (edited)");
        QVERIFY(aspects.isDirty());
    }

    void testAReadOnlyBundleCannotBeTypedInto()
    {
        const std::optional<ToolchainBundle> bundle = anyBundle();
        if (!bundle)
            QSKIP("No toolchains are configured here");

        ToolchainConfigAspects aspects(*bundle);
        QVERIFY(aspects.displayName().isEnabled());
        aspects.makeReadOnly();
        QVERIFY(!aspects.displayName().isEnabled());
        for (const auto &[_, command] : aspects.compilerCommands())
            QVERIFY(command->isReadOnly());
    }

    void testTheCxxCompilerFollowsTheCOneUntilItIsGivenByHand()
    {
        // Only a bundle with both languages has anything to derive, and only
        // the factory knows what goes with what.
        const std::optional<ToolchainBundle> bundle = bundleWithBothLanguages();
        if (!bundle)
            QSKIP("No toolchain here has both a C and a C++ compiler");

        ToolchainConfigAspects aspects(*bundle);
        Utils::BoolAspect * const manual = aspects.manualCxxCompiler();
        QVERIFY(manual);
        Utils::FilePathAspect * const c = aspects.compilerCommand(Constants::C_LANGUAGE_ID);
        Utils::FilePathAspect * const cxx = aspects.compilerCommand(Constants::CXX_LANGUAGE_ID);
        QVERIFY(c);
        QVERIFY(cxx);

        const Utils::FilePath expected = bundle->factory()->correspondingCompilerCommand(
            c->expandedVolatileValue(), Constants::CXX_LANGUAGE_ID);
        if (!expected.isExecutableFile())
            QSKIP("The C compiler here has no C++ one to go with it");

        // Derived: the C++ field follows the C one and is not the user's to
        // type in. Clobbered first, so that finding the right value there is
        // this deriving it rather than it never having moved.
        manual->setValue(false);
        QVERIFY(!cxx->isEnabled());
        cxx->setValue(Utils::FilePath::fromString("/nonexistent/clobbered"));
        aspects.deriveCxxCompilerCommand();
        QCOMPARE(cxx->expandedVolatileValue(), expected);

        // Given by hand: it stays where it is put, and the C one no longer
        // moves it.
        manual->setValue(true);
        QVERIFY(cxx->isEnabled());
        const Utils::FilePath byHand = Utils::FilePath::fromString("/nonexistent/by-hand");
        cxx->setValue(byHand);
        aspects.deriveCxxCompilerCommand();
        QCOMPARE(cxx->expandedVolatileValue(), byHand);
    }

    void testEveryKindOfToolchainAsksWithAspects()
    {
        // A Qt Quick page cannot hold a widget, so the Toolchains page can only
        // draw a kind that describes itself. Checked by making one toolchain of
        // each kind rather than by looking at what is installed: only two of
        // the nine kinds exist on any one machine.
        QStringList checked;
        for (ToolchainFactory * const factory : ToolchainFactory::allToolchainFactories()) {
            if (!factory->canCreate() || factory->supportedLanguages().isEmpty())
                continue;

            const Utils::Id bundleId = Utils::Id::generate();
            Toolchains toolchains;
            for (const Utils::Id language : factory->supportedLanguages()) {
                Toolchain * const tc = factory->create();
                QVERIFY(tc);
                tc->setDetectionSource(DetectionSource::Manual);
                tc->setLanguage(language);
                tc->setBundleId(bundleId);
                toolchains << tc;
            }
            const ToolchainBundle bundle(toolchains, ToolchainBundle::HandleMissing::CreateOnly);
            const std::unique_ptr<ToolchainConfigAspects> aspects
                = factory->createConfigurationAspects(bundle);
            QVERIFY2(aspects, qPrintable(factory->displayName()
                                         + " has nothing to configure it with"));

            // Nothing in it may be a control no renderer knows: that is a
            // control drawn as a placeholder, or as nothing at all.
            std::function<void(const Utils::AspectContainer *)> walk =
                [&](const Utils::AspectContainer *container) {
                    for (Utils::BaseAspect * const aspect : container->aspects()) {
                        if (auto nested = qobject_cast<Utils::AspectContainer *>(aspect)) {
                            walk(nested);
                            continue;
                        }
                        QVERIFY2(aspect->presentation().control != Utils::AspectControls::Custom,
                                 qPrintable(factory->displayName() + ": "
                                            + QString::fromLatin1(
                                                aspect->metaObject()->className())
                                            + " asks for no control"));
                    }
                };
            walk(aspects.get());

            // And what it holds is reachable from a page's QML by name, which
            // needs the names to be there and to differ.
            QSet<QString> names;
            for (Utils::BaseAspect * const aspect : aspects->aspects()) {
                if (aspect->qmlName().isEmpty())
                    continue;
                QVERIFY2(!names.contains(aspect->qmlName()),
                         qPrintable(factory->displayName() + " uses the QML name "
                                    + aspect->qmlName() + " twice"));
                names.insert(aspect->qmlName());
            }

            qDeleteAll(toolchains);
            checked << factory->displayName();
        }
        // Which kinds are registered depends on the platform and on which
        // plugins are loaded, so the list is reported rather than counted.
        qInfo().noquote() << "kinds checked:" << checked.join(", ");
        QVERIFY(!checked.isEmpty());
    }

private:
    static std::optional<ToolchainBundle> anyBundle()
    {
        const QList<ToolchainBundle> bundles = ToolchainBundle::collectBundles(
            ToolchainBundle::HandleMissing::CreateOnly);
        if (bundles.isEmpty())
            return {};
        return bundles.first();
    }

    static std::optional<ToolchainBundle> bundleWithBothLanguages()
    {
        const QList<ToolchainBundle> bundles = ToolchainBundle::collectBundles(
            ToolchainBundle::HandleMissing::CreateOnly);
        for (const ToolchainBundle &bundle : bundles) {
            if (bundle.type() == Constants::MSVC_TOOLCHAIN_TYPEID)
                continue;
            if (!bundle.factory())
                continue;
            const QList<Utils::Id> languages = bundle.factory()->supportedLanguages();
            if (languages.contains(Constants::C_LANGUAGE_ID)
                && languages.contains(Constants::CXX_LANGUAGE_ID)) {
                return bundle;
            }
        }
        return {};
    }
};

QObject *createToolchainConfigAspectsTest()
{
    return new ToolchainConfigAspectsTest;
}
#endif // WITH_TESTS

QStringList ToolchainConfigAspects::splitString(const QString &s)
{
    ProcessArgs::SplitError splitError;
    const OsType osType = HostOsInfo::hostOs();
    QStringList res = ProcessArgs::splitArgs(s, osType, false, &splitError);
    if (splitError != ProcessArgs::SplitOk) {
        res = ProcessArgs::splitArgs(s + '\\', osType, false, &splitError);
        if (splitError != ProcessArgs::SplitOk) {
            res = ProcessArgs::splitArgs(s + '"', osType, false, &splitError);
            if (splitError != ProcessArgs::SplitOk)
                res = ProcessArgs::splitArgs(s + '\'', osType, false, &splitError);
        }
    }
    return res;
}

} // namespace ProjectExplorer

#include "toolchainconfigaspects.moc"
