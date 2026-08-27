// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "cppcodestylesettingspage.h"

#include "cppcodeformatter.h"
#include "cppcodestylesnippets.h"
#include "cppeditorconstants.h"
#include "cppeditortr.h"
#include "cpppointerdeclarationformatter.h"
#include "cpptoolssettings.h"

#include <cppeditor/cppeditorconstants.h>

#include <coreplugin/dialogs/ioptionspage.h>

#include <cplusplus/Overview.h>
#include <cplusplus/PreprocessorEnvironment.h>
#include <cplusplus/pp-engine.h>

#include <texteditor/codestyleeditor.h>
#include <texteditor/icodestylepreferencesfactory.h>
#include <texteditor/tabsettings.h>

#include <utils/algorithm.h>
#include <utils/aspects.h>
#include <utils/guard.h>
#include <utils/layoutbuilder.h>
#include <utils/qtcassert.h>

#include <QTextDocument>

using namespace TextEditor;
using namespace Utils;

namespace CppEditor {

namespace Internal {

// \a text with its pointer and reference declarations written the way the
// settings ask for. Not the indenting: that is the indenter's, and the preview
// runs it separately.
static QString withPointersFormatted(const QString &text, const CppCodeStyleSettings &settings)
{
    // On the heap and not owned here: a RefactoringFile built over a document
    // deletes it, so the text has to be read back before the file goes away.
    auto textDocument = new QTextDocument(text);
    // Preprocess source
    CPlusPlus::Environment env;
    Preprocessor preprocess(nullptr, &env);
    FilePath noFileFile = FilePath::fromPathPart(u"<no-file>");
    const QByteArray preprocessedSource
        = preprocess.run(noFileFile, textDocument->toPlainText().toUtf8());

    Document::Ptr cppDocument = Document::create(noFileFile);
    cppDocument->setUtf8Source(preprocessedSource);
    cppDocument->parse(Document::ParseTranslationUnit);
    cppDocument->check();

    CppRefactoringFilePtr cppRefactoringFile
        = CppRefactoringChanges::file(textDocument, noFileFile, cppDocument);

    // Run the formatter
    Overview overview;
    overview.showReturnTypes = true;
    overview.starBindFlags = {};

    if (settings.bindStarToIdentifier)
        overview.starBindFlags |= Overview::BindToIdentifier;
    if (settings.bindStarToTypeName)
        overview.starBindFlags |= Overview::BindToTypeName;
    if (settings.bindStarToLeftSpecifier)
        overview.starBindFlags |= Overview::BindToLeftSpecifier;
    if (settings.bindStarToRightSpecifier)
        overview.starBindFlags |= Overview::BindToRightSpecifier;

    PointerDeclarationFormatter formatter(cppRefactoringFile, overview);
    Utils::ChangeSet change = formatter.format(cppDocument->translationUnit()->ast());

    // Apply change
    change.apply(textDocument);
    return textDocument->toPlainText();
}

// The snippet each category demonstrates, so that the preview shows code the
// settings on show actually change.
enum Category { General = 0, Content, Braces, Switch, Alignment, Types };

// CppCodeStyleAspects

// What the C++ Code Style form edits. The settings live in the code style
// rather than in settings keys of their own, so these aspects read from and
// write to the preferences the page handed over - its own editable copy.
class CppCodeStyleAspects final : public AspectContainer
{
public:
    CppCodeStyleAspects(CppCodeStylePreferences *preferences, CodeStylePreviewAspect *preview);

private:
    void readFromPreferences();
    void writeToPreferences();
    void updateState();
    void showCategory();
    void reformatPreview();
    CppCodeStyleSettings settingsFromAspects() const;

    CppCodeStylePreferences *m_preferences = nullptr;
    CodeStylePreviewAspect *m_preview = nullptr;
    Guard m_reading;

    SelectionAspect m_category{this};

    AspectContainer m_generalGroup{this};
    TabSettings m_tabSettings;
    StringAspect m_statementMacros{&m_generalGroup};

    AspectContainer m_contentGroup{this};
    BoolAspect m_indentAccessSpecifiers{&m_contentGroup};
    BoolAspect m_indentDeclarationsRelativeToAccessSpecifiers{&m_contentGroup};
    BoolAspect m_indentFunctionBody{&m_contentGroup};
    BoolAspect m_indentBlockBody{&m_contentGroup};
    BoolAspect m_indentNamespaceBody{&m_contentGroup};

    AspectContainer m_bracesGroup{this};
    BoolAspect m_indentClassBraces{&m_bracesGroup};
    BoolAspect m_indentNamespaceBraces{&m_bracesGroup};
    BoolAspect m_indentEnumBraces{&m_bracesGroup};
    BoolAspect m_indentFunctionBraces{&m_bracesGroup};
    BoolAspect m_indentBlockBraces{&m_bracesGroup};

    AspectContainer m_switchGroup{this};
    BoolAspect m_indentSwitchLabels{&m_switchGroup};
    BoolAspect m_indentCaseStatements{&m_switchGroup};
    BoolAspect m_indentCaseBlocks{&m_switchGroup};
    BoolAspect m_indentCaseBreak{&m_switchGroup};

    AspectContainer m_alignmentGroup{this};
    BoolAspect m_alignAssignments{&m_alignmentGroup};
    BoolAspect m_extraPaddingConditions{&m_alignmentGroup};

    AspectContainer m_typesGroup{this};
    BoolAspect m_bindStarToIdentifier{&m_typesGroup};
    BoolAspect m_bindStarToTypeName{&m_typesGroup};
    BoolAspect m_bindStarToLeftSpecifier{&m_typesGroup};
    BoolAspect m_bindStarToRightSpecifier{&m_typesGroup};
};

CppCodeStyleAspects::CppCodeStyleAspects(CppCodeStylePreferences *preferences,
                                         CodeStylePreviewAspect *preview)
    : m_preferences(preferences)
    , m_preview(preview)
{
    // Which settings are on show. The widget page used tabs; the choice is the
    // same one, and it also decides which snippet the preview demonstrates.
    m_category.setQmlName("Category");
    m_category.setLabelText(Tr::tr("Category:"));
    m_category.setDisplayStyle(SelectionAspect::DisplayStyle::ComboBox);
    m_category.addOption(Tr::tr("General"));
    m_category.addOption(Tr::tr("Content"));
    m_category.addOption(Tr::tr("Braces"));
    m_category.addOption(Tr::tr("\"switch\""));
    m_category.addOption(Tr::tr("Alignment"));
    m_category.addOption(Tr::tr("Pointers and References"));

    m_generalGroup.setQmlName("GeneralSettings");
    m_tabSettings.setQmlName("TabSettings");
    m_generalGroup.registerAspect(&m_tabSettings);
    m_tabSettings.setPreferences(preferences);

    m_statementMacros.setQmlName("StatementMacros");
    m_statementMacros.setLabelText(Tr::tr("Statement macros:"));
    m_statementMacros.setDisplayStyle(StringAspect::TextEditDisplay);
    m_statementMacros.setToolTip(
        Tr::tr("Macros that can be used as statements without a trailing semicolon."));

    const auto setupBool = [](BoolAspect &aspect, const QString &qmlName, const QString &text,
                              const QString &toolTip = {}) {
        aspect.setQmlName(qmlName);
        aspect.setLabelText(text);
        aspect.setLabelPlacement(BoolAspect::LabelPlacement::AtCheckBox);
        aspect.setToolTip(toolTip);
    };

    m_contentGroup.setQmlName("ContentSettings");
    setupBool(m_indentAccessSpecifiers, "IndentAccessSpecifiers",
              Tr::tr("\"public\", \"protected\" and \"private\" within class body"));
    setupBool(m_indentDeclarationsRelativeToAccessSpecifiers,
              "IndentDeclarationsRelativeToAccessSpecifiers",
              Tr::tr("Declarations relative to \"public\", \"protected\" and \"private\""));
    setupBool(m_indentFunctionBody, "IndentFunctionBody",
              Tr::tr("Statements within function body"));
    setupBool(m_indentBlockBody, "IndentBlockBody", Tr::tr("Statements within blocks"));
    setupBool(m_indentNamespaceBody, "IndentNamespaceBody",
              Tr::tr("Declarations within \"namespace\" definition"));

    m_bracesGroup.setQmlName("BracesSettings");
    setupBool(m_indentClassBraces, "IndentClassBraces", Tr::tr("Class declarations"));
    setupBool(m_indentNamespaceBraces, "IndentNamespaceBraces", Tr::tr("Namespace declarations"));
    setupBool(m_indentEnumBraces, "IndentEnumBraces", Tr::tr("Enum declarations"));
    setupBool(m_indentFunctionBraces, "IndentFunctionBraces", Tr::tr("Function declarations"));
    setupBool(m_indentBlockBraces, "IndentBlockBraces", Tr::tr("Blocks"));

    m_switchGroup.setQmlName("SwitchSettings");
    setupBool(m_indentSwitchLabels, "IndentSwitchLabels", Tr::tr("\"case\" or \"default\""));
    setupBool(m_indentCaseStatements, "IndentCaseStatements",
              Tr::tr("Statements relative to \"case\" or \"default\""));
    setupBool(m_indentCaseBlocks, "IndentCaseBlocks",
              Tr::tr("Blocks relative to \"case\" or \"default\""));
    setupBool(m_indentCaseBreak, "IndentCaseBreak",
              Tr::tr("\"break\" statement relative to \"case\" or \"default\""));

    m_alignmentGroup.setQmlName("AlignmentSettings");
    setupBool(m_alignAssignments, "AlignAssignments", Tr::tr("Align after assignments"),
              Tr::tr("<html><head/><body>\n"
                     "Enables alignment to tokens after =, += etc. When the option is "
                     "disabled, regular continuation line indentation will be used.<br>\n"
                     "<br>\n"
                     "With alignment:\n"
                     "<pre>\n"
                     "a = a +\n"
                     "    b\n"
                     "</pre>\n"
                     "Without alignment:\n"
                     "<pre>\n"
                     "a = a +\n"
                     "        b\n"
                     "</pre>\n"
                     "</body></html>"));
    setupBool(m_extraPaddingConditions, "ExtraPaddingConditions",
              Tr::tr("Add extra padding to conditions if they would align to the next line"),
              Tr::tr("<html><head/><body>\n"
                     "Adds an extra level of indentation to multiline conditions in the "
                     "switch, if, while and foreach statements if they would otherwise "
                     "have the same or less indentation than a nested statement.\n"
                     "\n"
                     "For four-spaces indentation only if statement conditions are "
                     "affected. Without extra padding:\n"
                     "<pre>\n"
                     "if (a &&\n"
                     "    b)\n"
                     "    c;\n"
                     "</pre>\n"
                     "With extra padding:\n"
                     "<pre>\n"
                     "if (a &&\n"
                     "        b)\n"
                     "    c;\n"
                     "</pre>\n"
                     "</body></html>"));

    m_typesGroup.setQmlName("TypesSettings");
    setupBool(m_bindStarToIdentifier, "BindStarToIdentifier", Tr::tr("Identifier"),
              Tr::tr("<html><head/><body>This does not apply to the star and reference "
                     "symbol in pointer/reference to functions and arrays, e.g.:\n"
                     "<pre>   int (&rf)() = ...;\n"
                     "   int (*pf)() = ...;\n"
                     "\n"
                     "   int (&ra)[2] = ...;\n"
                     "   int (*pa)[2] = ...;\n"
                     "\n"
                     "</pre></body></html>"));
    setupBool(m_bindStarToTypeName, "BindStarToTypeName", Tr::tr("Type name"));
    setupBool(m_bindStarToLeftSpecifier, "BindStarToLeftSpecifier",
              Tr::tr("Left const/volatile"));
    setupBool(m_bindStarToRightSpecifier, "BindStarToRightSpecifier",
              Tr::tr("Right const/volatile"), Tr::tr("This does not apply to references."));

    readFromPreferences();

    connect(this, &AspectContainer::volatileValueChanged,
            this, &CppCodeStyleAspects::writeToPreferences);
    connect(&m_category, &BaseAspect::volatileValueChanged,
            this, &CppCodeStyleAspects::showCategory);
    connect(preferences, &CppCodeStylePreferences::currentValueChanged,
            this, &CppCodeStyleAspects::readFromPreferences);
    connect(preferences, &CppCodeStylePreferences::currentPreferencesChanged,
            this, &CppCodeStyleAspects::readFromPreferences);
}

CppCodeStyleSettings CppCodeStyleAspects::settingsFromAspects() const
{
    CppCodeStyleSettings set = m_preferences->currentCodeStyleSettings();
    set.statementMacros = Utils::transform(
        m_statementMacros.volatileValue().trimmed().split('\n', Qt::SkipEmptyParts),
        [](const QString &line) { return line.trimmed(); });
    set.indentBlockBraces = m_indentBlockBraces.volatileValue();
    set.indentBlockBody = m_indentBlockBody.volatileValue();
    set.indentClassBraces = m_indentClassBraces.volatileValue();
    set.indentEnumBraces = m_indentEnumBraces.volatileValue();
    set.indentNamespaceBraces = m_indentNamespaceBraces.volatileValue();
    set.indentNamespaceBody = m_indentNamespaceBody.volatileValue();
    set.indentAccessSpecifiers = m_indentAccessSpecifiers.volatileValue();
    set.indentDeclarationsRelativeToAccessSpecifiers
        = m_indentDeclarationsRelativeToAccessSpecifiers.volatileValue();
    set.indentFunctionBody = m_indentFunctionBody.volatileValue();
    set.indentFunctionBraces = m_indentFunctionBraces.volatileValue();
    set.indentSwitchLabels = m_indentSwitchLabels.volatileValue();
    set.indentStatementsRelativeToSwitchLabels = m_indentCaseStatements.volatileValue();
    set.indentBlocksRelativeToSwitchLabels = m_indentCaseBlocks.volatileValue();
    set.indentControlFlowRelativeToSwitchLabels = m_indentCaseBreak.volatileValue();
    set.bindStarToIdentifier = m_bindStarToIdentifier.volatileValue();
    set.bindStarToTypeName = m_bindStarToTypeName.volatileValue();
    set.bindStarToLeftSpecifier = m_bindStarToLeftSpecifier.volatileValue();
    set.bindStarToRightSpecifier = m_bindStarToRightSpecifier.volatileValue();
    set.extraPaddingForConditionsIfConfusingAlign = m_extraPaddingConditions.volatileValue();
    set.alignAssignments = m_alignAssignments.volatileValue();
    return set;
}

void CppCodeStyleAspects::readFromPreferences()
{
    const GuardLocker locker(m_reading);
    const CppCodeStyleSettings s = m_preferences->currentCodeStyleSettings();
    m_statementMacros.setValue(s.statementMacros.join('\n'));
    m_indentBlockBraces.setValue(s.indentBlockBraces);
    m_indentBlockBody.setValue(s.indentBlockBody);
    m_indentClassBraces.setValue(s.indentClassBraces);
    m_indentEnumBraces.setValue(s.indentEnumBraces);
    m_indentNamespaceBraces.setValue(s.indentNamespaceBraces);
    m_indentNamespaceBody.setValue(s.indentNamespaceBody);
    m_indentAccessSpecifiers.setValue(s.indentAccessSpecifiers);
    m_indentDeclarationsRelativeToAccessSpecifiers.setValue(
        s.indentDeclarationsRelativeToAccessSpecifiers);
    m_indentFunctionBody.setValue(s.indentFunctionBody);
    m_indentFunctionBraces.setValue(s.indentFunctionBraces);
    m_indentSwitchLabels.setValue(s.indentSwitchLabels);
    m_indentCaseStatements.setValue(s.indentStatementsRelativeToSwitchLabels);
    m_indentCaseBlocks.setValue(s.indentBlocksRelativeToSwitchLabels);
    m_indentCaseBreak.setValue(s.indentControlFlowRelativeToSwitchLabels);
    m_bindStarToIdentifier.setValue(s.bindStarToIdentifier);
    m_bindStarToTypeName.setValue(s.bindStarToTypeName);
    m_bindStarToLeftSpecifier.setValue(s.bindStarToLeftSpecifier);
    m_bindStarToRightSpecifier.setValue(s.bindStarToRightSpecifier);
    m_extraPaddingConditions.setValue(s.extraPaddingForConditionsIfConfusingAlign);
    m_alignAssignments.setValue(s.alignAssignments);
    updateState();
}

void CppCodeStyleAspects::writeToPreferences()
{
    if (m_reading.isLocked())
        return;

    auto current = dynamic_cast<CppCodeStylePreferences *>(m_preferences->currentPreferences());
    if (!current || current->isReadOnly())
        return;

    current->setCodeStyleSettings(settingsFromAspects());
    reformatPreview();
}

void CppCodeStyleAspects::reformatPreview()
{
    if (!m_preview)
        return;
    // The indenting happens on the QML side, off the value changing; this is
    // what an indenter would not do.
    m_preview->setValue(withPointersFormatted(m_preview->volatileValue(),
                                              m_preferences->currentCodeStyleSettings()));
}

void CppCodeStyleAspects::showCategory()
{
    const int category = m_category.volatileValue();

    // One category at a time, the way the widget page's tabs showed one.
    const auto show = [this](AspectContainer &group, bool selected) {
        group.setVisible(selected);
        auto current = dynamic_cast<CppCodeStylePreferences *>(m_preferences->currentPreferences());
        group.setEnabled(current && !current->isReadOnly());
    };
    show(m_generalGroup, category == General);
    show(m_contentGroup, category == Content);
    show(m_bracesGroup, category == Braces);
    show(m_switchGroup, category == Switch);
    show(m_alignmentGroup, category == Alignment);
    show(m_typesGroup, category == Types);

    if (m_preview && category >= 0
        && category < int(std::size(Constants::DEFAULT_CODE_STYLE_SNIPPETS))) {
        m_preview->setPreviewText(
            QString::fromLatin1(Constants::DEFAULT_CODE_STYLE_SNIPPETS[category]));
        reformatPreview();
    }
}

void CppCodeStyleAspects::updateState()
{
    showCategory();
}

} // namespace Internal

namespace Internal {

AspectContainer *createCppCodeStyleAspects(ICodeStylePreferences *codeStyle,
                                           CodeStylePreviewAspect *preview)
{
    return new CppCodeStyleAspects(static_cast<CppCodeStylePreferences *>(codeStyle), preview);
}

// What the preview shows beyond what the indenter does: the pointer and
// reference declarations, which is what the "Pointers and References" settings
// change and no indenter would.
Result<QString> formatCppPreview(ICodeStylePreferences *codeStyle, const QString &text)
{
    auto preferences = dynamic_cast<CppCodeStylePreferences *>(codeStyle);
    QTC_ASSERT(preferences, return text);

    return withPointersFormatted(text, preferences->currentCodeStyleSettings());
}

// CppCodeStyleSettingsPage

class CppCodeStyleSettingsPage : public Core::IOptionsPage
{
public:
    CppCodeStyleSettingsPage()
    {
        setId(Constants::CPP_CODE_STYLE_SETTINGS_ID);
        setDisplayName(Tr::tr("Code Style"));
        setCategory(Constants::CPP_SETTINGS_CATEGORY);
        setSettingsProvider([] {
            static CodeStyleAspect theSettings(cppCodeStyle(), CppEditor::Constants::CPP_SETTINGS_ID);
            return &theSettings;
        });
    }
};

void setupCppCodeStyleSettings()
{
    static CppCodeStyleSettingsPage theCppCodeStyleSettingsPage;
}

} // namespace Internal

} // namespace CppEditor
