// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "customtoolchain.h"

#include "abiaspect.h"
#include "gccparser.h"
#include "clangparser.h"
#include "gcctoolchain.h"
#include "linuxiccparser.h"
#include "msvcparser.h"
#include "customparser.h"
#include "projectexplorerconstants.h"
#include "projectexplorertr.h"
#include "projectmacro.h"
#include "toolchain.h"
#include "toolchainconfigaspects.h"

#include <utils/algorithm.h>
#include <utils/environment.h>
#include <utils/pathchooser.h>
#include <utils/stringutils.h>


using namespace Utils;

namespace ProjectExplorer::Internal {

const char makeCommandKeyC[] = "ProjectExplorer.CustomToolChain.MakePath";
const char predefinedMacrosKeyC[] = "ProjectExplorer.CustomToolChain.PredefinedMacros";
const char headerPathsKeyC[] = "ProjectExplorer.CustomToolChain.HeaderPaths";
const char cxx11FlagsKeyC[] = "ProjectExplorer.CustomToolChain.Cxx11Flags";
const char mkspecsKeyC[] = "ProjectExplorer.CustomToolChain.Mkspecs";
const char outputParserKeyC[] = "ProjectExplorer.CustomToolChain.OutputParser";

// --------------------------------------------------------------------------
// CustomToolchain
// --------------------------------------------------------------------------

class CustomToolchain : public Toolchain
{
public:
    CustomToolchain()
        : Toolchain(Constants::CUSTOM_TOOLCHAIN_TYPEID)
        , m_outputParserId(GccParser::id())
    {
        setTypeDisplayName(Tr::tr("Custom"));
        setTargetAbiKey("ProjectExplorer.CustomToolChain.TargetAbi");
        setCompilerCommandKey("ProjectExplorer.CustomToolChain.CompilerPath");
    }

    class Parser {
    public:
        Id parserId;   ///< A unique id identifying a parser
        QString displayName; ///< A translateable name to show in the user interface
    };

    bool isValid() const override;

    MacroInspectionRunner createMacroInspectionRunner() const override;
    LanguageExtensions languageExtensions(const QStringList &cxxflags) const override;
    WarningFlags warningFlags(const QStringList &cxxflags) const override;
    const Macros &rawPredefinedMacros() const;
    void setPredefinedMacros(const Macros &macros);

    BuiltInHeaderPathsRunner createBuiltInHeaderPathsRunner(const Environment &) const override;
    void addToEnvironment(Environment &env) const override;
    QStringList suggestedMkspecList() const override;
    QList<OutputLineParser *> createOutputParsers() const override;
    QStringList headerPathsList() const;
    void setHeaderPaths(const QStringList &list);

    void toMap(Store &data) const override;
    void fromMap(const Store &data) override;

    bool operator ==(const Toolchain &) const override;

    void setMakeCommand(const FilePath &);
    FilePath makeCommand(const Environment &environment) const override;

    void setCxx11Flags(const QStringList &);
    const QStringList &cxx11Flags() const;

    void setMkspecs(const QString &);
    QString mkspecs() const;

    Id outputParserId() const;
    void setOutputParserId(Id parserId);
    static QList<CustomToolchain::Parser> parsers();

    CustomParserSettings customParserSettings() const;

private:
    FilePath m_makeCommand;

    Macros m_predefinedMacros;
    HeaderPaths m_builtInHeaderPaths;
    QStringList m_cxx11Flags;
    QStringList m_mkspecs;

    Id m_outputParserId;
};

CustomParserSettings CustomToolchain::customParserSettings() const
{
    return findOrDefault(CustomParsers::get(), [this](const CustomParserSettings &s) {
        return s.id == outputParserId();
    });
}

bool CustomToolchain::isValid() const
{
    return true;
}

Toolchain::MacroInspectionRunner CustomToolchain::createMacroInspectionRunner() const
{
    const Macros theMacros = m_predefinedMacros;
    const Id lang = language();

    // This runner must be thread-safe!
    return [theMacros, lang](const QStringList &cxxflags){
        Macros macros = theMacros;
        for (const QString &cxxFlag : cxxflags) {
            if (cxxFlag.startsWith(QLatin1String("-D")))
                macros.append(Macro::fromKeyValue(cxxFlag.mid(2).trimmed()));
            else if (cxxFlag.startsWith(QLatin1String("-U")) && !cxxFlag.contains('='))
                macros.append({cxxFlag.mid(2).trimmed().toUtf8(), MacroType::Undefine});

        }
        return MacroInspectionReport{macros, Toolchain::languageVersion(lang, macros)};
    };
}

LanguageExtensions CustomToolchain::languageExtensions(const QStringList &) const
{
    return LanguageExtension::None;
}

WarningFlags CustomToolchain::warningFlags(const QStringList &cxxflags) const
{
    Q_UNUSED(cxxflags)
    return WarningFlag::Default;
}

const Macros &CustomToolchain::rawPredefinedMacros() const
{
    return m_predefinedMacros;
}

void CustomToolchain::setPredefinedMacros(const Macros &macros)
{
    if (m_predefinedMacros == macros)
        return;
    m_predefinedMacros = macros;
    toolChainUpdated();
}

Toolchain::BuiltInHeaderPathsRunner CustomToolchain::createBuiltInHeaderPathsRunner(
        const Environment &) const
{
    const HeaderPaths builtInHeaderPaths = m_builtInHeaderPaths;

    // This runner must be thread-safe!
    return [builtInHeaderPaths](const QStringList &cxxFlags, const FilePath &sysRoot, const QString &) {
        Q_UNUSED(sysRoot)
        HeaderPaths flagHeaderPaths;
        for (const QString &cxxFlag : cxxFlags) {
            if (cxxFlag.startsWith(QLatin1String("-I"))) {
                flagHeaderPaths.push_back(
                    HeaderPath::makeBuiltIn(FilePath::fromUserInput(cxxFlag.mid(2).trimmed())));
            }
        }

        return builtInHeaderPaths + flagHeaderPaths;
    };
}

void CustomToolchain::addToEnvironment(Environment &env) const
{
    const FilePath compiler = compilerCommand();
    if (compiler.isEmpty())
        return;
    const FilePath path = compiler.parentDir();
    env.prependOrSetPath(path);
    const FilePath makePath = m_makeCommand.parentDir();
    if (makePath != path)
        env.prependOrSetPath(makePath);
}

QStringList CustomToolchain::suggestedMkspecList() const
{
    return m_mkspecs;
}

QList<OutputLineParser *> CustomToolchain::createOutputParsers() const
{
    if (m_outputParserId == GccParser::id())
        return GccParser::gccParserSuite();
    if (m_outputParserId == ClangParser::id())
        return ClangParser::clangParserSuite();
    if (m_outputParserId == LinuxIccParser::id())
        return LinuxIccParser::iccParserSuite();
    if (m_outputParserId == MsvcParser::id())
        return {new MsvcParser};
    return {new CustomParser(customParserSettings())};
}

QStringList CustomToolchain::headerPathsList() const
{
    return Utils::transform<QList>(m_builtInHeaderPaths, [](const HeaderPath &header) {
        return header.path.path();
    });
}

void CustomToolchain::setHeaderPaths(const QStringList &list)
{
    HeaderPaths tmp = Utils::transform<QList>(list, [](const QString &headerPath) {
        return HeaderPath::makeBuiltIn(FilePath::fromUserInput(headerPath.trimmed()));
    });

    if (m_builtInHeaderPaths == tmp)
        return;
    m_builtInHeaderPaths = tmp;
    toolChainUpdated();
}

void CustomToolchain::setMakeCommand(const FilePath &path)
{
    if (path == m_makeCommand)
        return;
    m_makeCommand = path;
    toolChainUpdated();
}

FilePath CustomToolchain::makeCommand(const Environment &) const
{
    return m_makeCommand;
}

void CustomToolchain::setCxx11Flags(const QStringList &flags)
{
    if (flags == m_cxx11Flags)
        return;
    m_cxx11Flags = flags;
    toolChainUpdated();
}

const QStringList &CustomToolchain::cxx11Flags() const
{
    return m_cxx11Flags;
}

void CustomToolchain::setMkspecs(const QString &specs)
{
    const QStringList tmp = specs.split(',');
    if (tmp == m_mkspecs)
        return;
    m_mkspecs = tmp;
    toolChainUpdated();
}

QString CustomToolchain::mkspecs() const
{
    return m_mkspecs.join(',');
}

void CustomToolchain::toMap(Store &data) const
{
    Toolchain::toMap(data);
    data.insert(makeCommandKeyC, m_makeCommand.toUrlishString());
    QStringList macros = Utils::transform<QList>(m_predefinedMacros, [](const Macro &m) { return QString::fromUtf8(m.toByteArray()); });
    data.insert(predefinedMacrosKeyC, macros);
    data.insert(headerPathsKeyC, headerPathsList());
    data.insert(cxx11FlagsKeyC, m_cxx11Flags);
    data.insert(mkspecsKeyC, mkspecs());
    data.insert(outputParserKeyC, m_outputParserId.toSetting());
}

void CustomToolchain::fromMap(const Store &data)
{
    Toolchain::fromMap(data);
    if (hasError())
        return;

    m_makeCommand = FilePath::fromString(data.value(makeCommandKeyC).toString());
    const QStringList macros = data.value(predefinedMacrosKeyC).toStringList();
    m_predefinedMacros = Macro::toMacros(macros.join('\n').toUtf8());
    setHeaderPaths(data.value(headerPathsKeyC).toStringList());
    m_cxx11Flags = data.value(cxx11FlagsKeyC).toStringList();
    setMkspecs(data.value(mkspecsKeyC).toString());
    setOutputParserId(Id::fromSetting(data.value(outputParserKeyC)));
}

bool CustomToolchain::operator ==(const Toolchain &other) const
{
    if (!Toolchain::operator ==(other))
        return false;

    auto customTc = static_cast<const CustomToolchain *>(&other);
    return m_makeCommand == customTc->m_makeCommand
            && compilerCommand() == customTc->compilerCommand()
            && targetAbi() == customTc->targetAbi()
            && m_predefinedMacros == customTc->m_predefinedMacros
            && m_builtInHeaderPaths == customTc->m_builtInHeaderPaths;
}

Id CustomToolchain::outputParserId() const
{
    return m_outputParserId;
}

void CustomToolchain::setOutputParserId(Id parserId)
{
    if (m_outputParserId == parserId)
        return;
    m_outputParserId = parserId;
    toolChainUpdated();
}

QList<CustomToolchain::Parser> CustomToolchain::parsers()
{
    QList<CustomToolchain::Parser> result;
    result.append({GccParser::id(),      Tr::tr("GCC")});
    result.append({ClangParser::id(),    Tr::tr("Clang")});
    result.append({LinuxIccParser::id(), Tr::tr("ICC")});
    result.append({MsvcParser::id(),     Tr::tr("MSVC")});
    return result;
}

class CustomToolchainAspects final : public ToolchainConfigAspects
{
public:
    explicit CustomToolchainAspects(const ToolchainBundle &bundle)
        : ToolchainConfigAspects(bundle)
    {
        m_makeCommand.setQmlName("MakeCommand");
        m_makeCommand.setLabelText(Tr::tr("&Make path:"));
        m_makeCommand.setExpectedKind(PathChooserKind::ExistingCommand);
        m_makeCommand.setHistoryCompleter("PE.MakeCommand.History");

        m_abi.setQmlName("Abi");
        m_abi.setLabelText(Tr::tr("&ABI:"));

        m_predefinedMacros.setQmlName("PredefinedMacros");
        m_predefinedMacros.setLabelText(Tr::tr("&Predefined macros:"));
        m_predefinedMacros.setDisplayStyle(StringAspect::TextEditDisplay);
        m_predefinedMacros.setPlaceHolderText(Tr::tr("MACRO[=VALUE]"));
        m_predefinedMacros.setToolTip(Tr::tr("Each line defines a macro. Format is MACRO[=VALUE]."));

        m_headerPaths.setQmlName("HeaderPaths");
        m_headerPaths.setLabelText(Tr::tr("&Header paths:"));
        m_headerPaths.setDisplayStyle(StringAspect::TextEditDisplay);
        m_headerPaths.setToolTip(Tr::tr("Each line adds a global header lookup path."));

        m_cxx11Flags.setQmlName("Cxx11Flags");
        m_cxx11Flags.setLabelText(Tr::tr("C++11 &flags:"));
        m_cxx11Flags.setDisplayStyle(StringAspect::LineEditDisplay);
        m_cxx11Flags.setToolTip(Tr::tr("Comma-separated list of flags that turn on C++11 support."));

        m_mkspecs.setQmlName("Mkspecs");
        m_mkspecs.setLabelText(Tr::tr("&Qt mkspecs:"));
        m_mkspecs.setDisplayStyle(StringAspect::LineEditDisplay);
        m_mkspecs.setToolTip(Tr::tr("Comma-separated list of mkspecs."));

        m_errorParser.setQmlName("ErrorParser");
        m_errorParser.setLabelText(Tr::tr("&Error parser:"));
        m_errorParser.setDisplayStyle(SelectionAspect::DisplayStyle::ComboBox);
        m_errorParser.setUseDataAsSavedValue();
        for (const CustomToolchain::Parser &parser : CustomToolchain::parsers())
            m_errorParser.addOption({parser.displayName, {}, parser.parserId.toSetting()});
        for (const CustomParserSettings &settings : CustomParsers::get())
            m_errorParser.addOption({settings.displayName, {}, settings.id.toSetting()});

        showToolchain();
    }

    void apply() override
    {
        ToolchainConfigAspects::apply();
        if (bundle().detectionSource().isAutoDetected())
            return;

        bundle().setTargetAbi(m_abi.currentAbi());
        const Macros macros = Utils::transform<QList>(
            lines(m_predefinedMacros), [](const QString &m) { return Macro::fromKeyValue(m); });
        bundle().forEach<CustomToolchain>([&](CustomToolchain &tc) {
            tc.setMakeCommand(m_makeCommand.expandedVolatileValue());
            tc.setPredefinedMacros(macros);
            tc.setHeaderPaths(lines(m_headerPaths));
            tc.setCxx11Flags(m_cxx11Flags.volatileValue().split(','));
            tc.setMkspecs(m_mkspecs.volatileValue());
            tc.setOutputParserId(Id::fromSetting(m_errorParser.itemValue()));
        });

        // What the toolchain made of the input - the macro parser rewrites it.
        showToolchain();
    }

    void makeReadOnly() override
    {
        ToolchainConfigAspects::makeReadOnly();
        for (BaseAspect *aspect : std::initializer_list<BaseAspect *>{
                 &m_makeCommand, &m_abi, &m_predefinedMacros, &m_headerPaths,
                 &m_cxx11Flags, &m_mkspecs, &m_errorParser}) {
            aspect->setEnabled(false);
        }
    }

private:
    static QStringList lines(const StringAspect &aspect)
    {
        return const_cast<StringAspect &>(aspect).volatileValue().split('\n', Qt::SkipEmptyParts);
    }

    void showToolchain()
    {
        m_makeCommand.setValue(bundle().makeCommand(Environment()));
        m_abi.setAbis(Abis(), bundle().targetAbi());
        const QStringList macroLines = Utils::transform<QList>(
            bundle().get(&CustomToolchain::rawPredefinedMacros),
            [](const Macro &m) { return QString::fromUtf8(m.toKeyValue(QByteArray())); });
        m_predefinedMacros.setValue(macroLines.join('\n'));
        m_headerPaths.setValue(bundle().get(&CustomToolchain::headerPathsList).join('\n'));
        m_cxx11Flags.setValue(bundle().get(&CustomToolchain::cxx11Flags).join(','));
        m_mkspecs.setValue(bundle().get(&CustomToolchain::mkspecs));
        m_errorParser.setValue(m_errorParser.indexForItemValue(
            bundle().get(&CustomToolchain::outputParserId).toSetting()));
    }

    FilePathAspect m_makeCommand{this};
    AbiAspects m_abi{this};
    StringAspect m_predefinedMacros{this};
    StringAspect m_headerPaths{this};
    StringAspect m_cxx11Flags{this};
    StringAspect m_mkspecs{this};
    SelectionAspect m_errorParser{this};
};

// CustomToolchainFactory

class CustomToolchainFactory final : public ToolchainFactory
{
public:
    CustomToolchainFactory()
    {
        setDisplayName(Tr::tr("Custom"));
        setSupportedToolchainType(Constants::CUSTOM_TOOLCHAIN_TYPEID);
        setSupportedLanguages({Constants::C_LANGUAGE_ID, Constants::CXX_LANGUAGE_ID});
        setToolchainConstructor([] { return new CustomToolchain; });
        setUserCreatable(true);
    }

private:
    std::unique_ptr<ToolchainConfigAspects> createConfigurationAspects(
        const ToolchainBundle &bundle) const override
    {
        return std::make_unique<CustomToolchainAspects>(bundle);
    }

    FilePath correspondingCompilerCommand(const FilePath &srcPath, Id targetLang) const override
    {
        static const std::pair<QString, QString> patternPairs[]
            = {{"gcc", "g++"}, {"clang", "clang++"}, {"icc", "icpc"}};
        for (const auto &[cPattern, cxxPattern] : patternPairs) {
            if (const FilePath &targetPath = GccToolchain::correspondingCompilerCommand(
                    srcPath, targetLang, cPattern, cxxPattern);
                targetPath != srcPath) {
                return targetPath;
            }
        }
        return srcPath;
    }
};

void setupCustomToolchain()
{
    static CustomToolchainFactory theCustomToolchainFactory;
}

} // ProjectExplorer::Internal
