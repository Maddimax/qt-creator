// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "toolchainconfigwidget.h"

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
        auto command = new FilePathAspect(this);
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
            d->manualCxxCompiler = new BoolAspect(this);
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

TextDisplay &ToolchainConfigAspects::errorMessage()
{
    return m_errorMessage;
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

ToolchainConfigWidget::ToolchainConfigWidget(const ToolchainBundle &bundle)
    : m_aspects(bundle)
{
    auto centralWidget = new Utils::DetailsWidget;
    centralWidget->setState(Utils::DetailsWidget::NoSummary);

    setFrameShape(QFrame::NoFrame);
    setWidgetResizable(true);
    setFocusPolicy(Qt::NoFocus);

    setWidget(centralWidget);

    auto detailsBox = new QWidget();

    m_mainLayout = new QFormLayout(detailsBox);
    m_mainLayout->setContentsMargins(0, 0, 0, 0);
    centralWidget->setWidget(detailsBox);
    m_mainLayout->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow); // for the Macs...

    Layouting::Form form;
    form.addItem(&m_aspects.displayName());
    form.addItem(Layouting::br);
    form.attachTo(detailsBox);

    setupCompilerPathChoosers();

    connect(&m_aspects, &Utils::BaseAspect::volatileValueChanged,
            this, &ToolchainConfigWidget::dirty);
    for (const auto &[tc, command] : m_aspects.compilerCommands()) {
        command->addOnVolatileValueChanged(this, [this, tc = tc] {
            emit compilerCommandChanged(tc->language());
        });
    }
}

ToolchainConfigWidget::~ToolchainConfigWidget() = default;

ToolchainConfigAspects &ToolchainConfigWidget::aspects()
{
    return m_aspects;
}

ToolchainBundle ToolchainConfigWidget::bundle() const
{
    return m_aspects.bundle();
}

QString ToolchainConfigWidget::currentDisplayName() const
{
    return const_cast<ToolchainConfigAspects &>(m_aspects).displayName().volatileValue();
}

bool ToolchainConfigWidget::isDirty() const
{
    return m_aspects.isDirty();
}

void ToolchainConfigWidget::apply()
{
    m_aspects.apply();
    applyImpl();
}

void ToolchainConfigWidget::makeReadOnly()
{
    m_aspects.makeReadOnly();
    makeReadOnlyImpl();
}

void ToolchainConfigWidget::setFallbackBrowsePath(const Utils::FilePath &path)
{
    m_aspects.setFallbackBrowsePath(path);
}

void ToolchainConfigWidget::addErrorLabel()
{
    if (m_errorLabelAdded)
        return;
    m_errorLabelAdded = true;
    Layouting::Form form;
    form.addItem(&m_aspects.errorMessage());
    form.addItem(Layouting::br);
    form.attachTo(m_mainLayout->parentWidget());
}

void ToolchainConfigWidget::setErrorMessage(const QString &m)
{
    m_aspects.errorMessage().setText(m);
    m_aspects.errorMessage().setVisible(!m.isEmpty());
}

void ToolchainConfigWidget::clearErrorMessage()
{
    setErrorMessage({});
}

QStringList ToolchainConfigWidget::splitString(const QString &s)
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

void ToolchainConfigWidget::setupCompilerPathChoosers()
{
    using namespace Layouting;
    for (const auto &[tc, command] : m_aspects.compilerCommands()) {
        Form form;
        if (tc->language() == Constants::CXX_LANGUAGE_ID && m_aspects.manualCxxCompiler()) {
            form.addItem(Row{command, m_aspects.manualCxxCompiler(), noMargin});
        } else {
            form.addItem(command);
        }
        form.addItem(br);
        form.attachTo(m_mainLayout->parentWidget());
    }
}

FilePath ToolchainConfigWidget::compilerCommand(Utils::Id language)
{
    if (FilePathAspect * const command = m_aspects.compilerCommand(language))
        return command->expandedVolatileValue();
    return {};
}

bool ToolchainConfigWidget::hasAnyCompiler() const
{
    return m_aspects.hasAnyCompiler();
}

void ToolchainConfigWidget::setCommandVersionArguments(const QStringList &args)
{
    m_aspects.setCommandVersionArguments(args);
}

void ToolchainConfigWidget::deriveCxxCompilerCommand()
{
    m_aspects.deriveCxxCompilerCommand();
}

} // namespace ProjectExplorer

#include "toolchainconfigwidget.moc"
