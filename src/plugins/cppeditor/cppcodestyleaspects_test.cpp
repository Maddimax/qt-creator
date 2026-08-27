// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "cppcodestyleaspects_test.h"

#include "cppcodestylesettingspage.h"
#include "cppeditorconstants.h"

#include <texteditor/codestyleeditor.h>
#include <texteditor/icodestylepreferencesfactory.h>

#include <utils/aspects.h>

#include <QTest>

using namespace TextEditor;
using namespace Utils;

namespace CppEditor::Internal {

class CppCodeStyleAspectsTest final : public QObject
{
    Q_OBJECT

private:
    // The aspects the Code Style form edits, as the page gets them.
    static AspectContainer *settingsAspectsFor(CppCodeStylePreferences *preferences,
                                               CodeStylePreviewAspect *preview = nullptr)
    {
        ICodeStylePreferencesFactory *factory
            = codeStyleFactory(CppEditor::Constants::CPP_SETTINGS_ID);
        return factory ? factory->createSettingsAspects(preferences, preview) : nullptr;
    }

    static BaseAspect *aspectNamed(const AspectContainer *container, const QString &name)
    {
        const QList<BaseAspect *> aspects = container->aspects();
        for (BaseAspect *aspect : aspects) {
            if (aspect->qmlName() == name)
                return aspect;
            if (auto nested = qobject_cast<const AspectContainer *>(aspect)) {
                if (BaseAspect *found = aspectNamed(nested, name))
                    return found;
            }
        }
        return nullptr;
    }

private slots:
    void testAspectsReadThePreferences()
    {
        CppCodeStylePreferences preferences;
        CppCodeStyleSettings settings;
        settings.indentBlockBraces = true;
        settings.bindStarToTypeName = true;
        settings.statementMacros = {"Q_FOREACH", "BOOST_FOREACH"};
        preferences.setCodeStyleSettings(settings);

        std::unique_ptr<AspectContainer> aspects(settingsAspectsFor(&preferences));
        QVERIFY(aspects);

        auto braces = qobject_cast<BoolAspect *>(aspectNamed(aspects.get(), "IndentBlockBraces"));
        auto typeName = qobject_cast<BoolAspect *>(aspectNamed(aspects.get(), "BindStarToTypeName"));
        auto macros = qobject_cast<StringAspect *>(aspectNamed(aspects.get(), "StatementMacros"));
        QVERIFY(braces);
        QVERIFY(typeName);
        QVERIFY(macros);

        // The settings live in the code style, not in settings keys of their
        // own, so the form is a view of whichever style the page is editing.
        QVERIFY(braces->value());
        QVERIFY(typeName->value());
        // One macro per line, which is what the field takes back apart.
        QCOMPARE(macros->value(), QString("Q_FOREACH\nBOOST_FOREACH"));
    }

    void testAspectsWriteBackToThePreferences()
    {
        CppCodeStylePreferences preferences;
        std::unique_ptr<AspectContainer> aspects(settingsAspectsFor(&preferences));
        QVERIFY(aspects);

        auto braces = qobject_cast<BoolAspect *>(aspectNamed(aspects.get(), "IndentBlockBraces"));
        auto macros = qobject_cast<StringAspect *>(aspectNamed(aspects.get(), "StatementMacros"));
        QVERIFY(braces);
        QVERIFY(macros);
        const bool before = preferences.codeStyleSettings().indentBlockBraces;

        // What the form writes when the user ticks the box.
        braces->setVolatileValue(!before);
        QCOMPARE(preferences.codeStyleSettings().indentBlockBraces, !before);

        macros->setVolatileValue(QString("  FOO  \n\nBAR\n"));
        // Blank lines and stray spaces are the field's, not the setting's.
        QCOMPARE(preferences.codeStyleSettings().statementMacros, QStringList({"FOO", "BAR"}));
    }

    // The widget page had a tab per category with a snippet each; the category
    // is a setting of the form now, and it still decides both.
    void testCategoryChoosesTheGroupAndTheSnippet()
    {
        CppCodeStylePreferences preferences;
        CodeStylePreviewAspect *preview = nullptr;
        std::unique_ptr<CodeStyleAspect> page;
        // The preview belongs to the page, so build one to get at it.
        page.reset(new CodeStyleAspect(&preferences, CppEditor::Constants::CPP_SETTINGS_ID));
        for (BaseAspect *aspect : page->aspects()) {
            if (auto found = qobject_cast<CodeStylePreviewAspect *>(aspect))
                preview = found;
        }
        QVERIFY(preview);

        AspectContainer *settings = nullptr;
        for (BaseAspect *aspect : page->aspects()) {
            if (auto container = qobject_cast<AspectContainer *>(aspect))
                settings = container;
        }
        QVERIFY(settings);

        auto category = qobject_cast<SelectionAspect *>(aspectNamed(settings, "Category"));
        BaseAspect *general = aspectNamed(settings, "GeneralSettings");
        BaseAspect *types = aspectNamed(settings, "TypesSettings");
        QVERIFY(category);
        QVERIFY(general);
        QVERIFY(types);

        category->setVolatileValue(0);
        QVERIFY(general->isVisible());
        QVERIFY(!types->isVisible());
        const QString generalSnippet = preview->value();

        category->setVolatileValue(5);
        QVERIFY(!general->isVisible());
        QVERIFY(types->isVisible());

        // And the preview is showing something else - the snippet that
        // demonstrates what this category changes.
        QVERIFY(!preview->value().isEmpty());
        QVERIFY(preview->value() != generalSnippet);
    }

    // A built-in style is read-only, and writing over one would change it for
    // every project delegating to it. The category groups carry that: shown
    // either way, editable only when the style behind them is.
    void testAStyleDelegatingToABuiltInIsShownButNotEditable()
    {
        ICodeStylePreferencesFactory * const factory
            = codeStyleFactory(CppEditor::Constants::CPP_SETTINGS_ID);
        QVERIFY(factory);
        ICodeStylePreferences * const global = factory->globalCodeStyle();
        QVERIFY2(global, "the C++ language has no global style, so this proves nothing");

        // The page edits a *copy*, which is editable by design; what makes it
        // read-only is the built-in the copy delegates to.
        ICodeStylePreferences * const delegate = global->currentPreferences();
        QVERIFY2(delegate && delegate != global, "the global style delegates to nothing here");
        QVERIFY2(delegate->isReadOnly(), "the delegated-to style is not a built-in");

        auto * const preferences = dynamic_cast<CppCodeStylePreferences *>(global);
        QVERIFY(preferences);
        const std::unique_ptr<CodeStyleAspect> page(
            new CodeStyleAspect(preferences, CppEditor::Constants::CPP_SETTINGS_ID));
        AspectContainer *settings = nullptr;
        for (BaseAspect *aspect : page->aspects()) {
            if (auto container = qobject_cast<AspectContainer *>(aspect))
                settings = container;
        }
        QVERIFY(settings);
        BaseAspect * const general = aspectNamed(settings, "GeneralSettings");
        QVERIFY(general);

        // Shown, so the reader can see what the style does - just not typed into.
        QVERIFY2(general->isVisible(), "a read-only style was hidden rather than locked");
        QVERIFY2(!general->isEnabled(), "a style delegating to a built-in could be edited");
    }

    // The preview shows what the pointer settings do, which no indenter would.
    void testPreviewBindsPointersAsAsked()
    {
        CppCodeStylePreferences preferences;
        const QString source = "int *foo(const Bar &b1)\n{\n    int *pi = 0;\n    return pi;\n}\n";

        CppCodeStyleSettings toIdentifier;
        toIdentifier.bindStarToIdentifier = true;
        toIdentifier.bindStarToTypeName = false;
        preferences.setCodeStyleSettings(toIdentifier);
        const Result<QString> boundToIdentifier = formatCppPreview(&preferences, source);
        QVERIFY(boundToIdentifier);

        CppCodeStyleSettings toTypeName;
        toTypeName.bindStarToIdentifier = false;
        toTypeName.bindStarToTypeName = true;
        preferences.setCodeStyleSettings(toTypeName);
        const Result<QString> boundToTypeName = formatCppPreview(&preferences, source);
        QVERIFY(boundToTypeName);

        QVERIFY(boundToIdentifier->contains("int *pi"));
        QVERIFY(boundToTypeName->contains("int* pi"));
    }
};

QObject *createCppCodeStyleAspectsTest()
{
    return new CppCodeStyleAspectsTest;
}

} // namespace CppEditor::Internal

#include "cppcodestyleaspects_test.moc"
