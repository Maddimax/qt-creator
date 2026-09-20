// Copyright (C) 2024 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "effectcodeeditorwidget.h"

#include "effectcomposertr.h"
#include "effectsautocomplete.h"
#include "syntaxhighlighter.h"

#include <qmljseditor/qmljsautocompleter.h>
#include <qmljseditor/qmljshoverhandler.h>
#include <qmljseditor/qmljssemantichighlighter.h>

#include <utils/mimeconstants.h>

namespace EffectComposer {

constexpr char EFFECTEDITOR_CONTEXT_ID[] = "EffectEditor.EffectEditorContext";

EffectDocument::EffectDocument()
    : QmlJSEditor::QmlJSEditorDocument(EFFECTEDITOR_CONTEXT_ID)
    , m_semanticHighlighter(new QmlJSEditor::SemanticHighlighter(this))
{}

EffectDocument::~EffectDocument()
{
    delete m_semanticHighlighter;
}

void EffectDocument::setUniformsCallback(const std::function<QStringList()> &callback)
{
    m_getUniforms = callback;
}

QStringList EffectDocument::uniforms() const
{
    if (m_getUniforms)
        return m_getUniforms();
    return {};
}

std::unique_ptr<TextEditor::AssistInterface> EffectDocument::createAssistInterface(
    const QTextCursor &cursor, [[maybe_unused]] TextEditor::AssistKind kind,
    TextEditor::AssistReason reason, [[maybe_unused]] Core::IEditor *editor) const
{
    // Whatever is asked for, the Effect Composer's completion: this document's
    // own scope and the effect's uniforms on top.
    return std::make_unique<EffectsCompletionAssistInterface>(
        cursor, Utils::FilePath(), reason, semanticInfo(), uniforms());
}

void EffectDocument::applyFontSettings()
{
    TextDocument::applyFontSettings();
    m_semanticHighlighter->updateFontSettings(fontSettings());
    if (!isSemanticInfoOutdated() && semanticInfo().isValid())
        m_semanticHighlighter->rerun(semanticInfo());
}

void EffectDocument::triggerPendingUpdates()
{
    TextDocument::triggerPendingUpdates(); // Calls applyFontSettings if necessary
    if (!isSemanticInfoOutdated() && semanticInfo().isValid())
        m_semanticHighlighter->rerun(semanticInfo());
}

EffectCodeEditorFactory::EffectCodeEditorFactory()
{
    setId(EFFECTEDITOR_CONTEXT_ID);
    setDisplayName(Tr::tr("Effect Code Editor"));
    addMimeType(EFFECTEDITOR_CONTEXT_ID);
    addMimeType(Utils::Constants::QML_MIMETYPE);
    addMimeType(Utils::Constants::QMLTYPES_MIMETYPE);
    addMimeType(Utils::Constants::JS_MIMETYPE);

    setDocumentCreator([]() { return new EffectDocument; });
    setAutoCompleterCreator([]() { return new QmlJSEditor::AutoCompleter; });
    setCommentDefinition(Utils::CommentDefinition::CppStyle);
    setParenthesesMatchingEnabled(true);
    setSyntaxHighlighterCreator([] { return new SyntaxHighlighter; });

    addHoverHandler(&QmlJSEditor::qmlJSHoverHandler());
    setCompletionAssistProvider(new EffectsCompeletionAssistProvider);

    // The Qt Quick editor, in a window of the Effect Composer's own: line
    // numbers and revisions, no marks, nothing to fold, no tool bar. The
    // document brings the QML indenter, and completion is the editor's own
    // Ctrl+Space, in its own context.
    setUsesQuickEditor(true);
    setMarksVisible(false);
    setCodeFoldingSupported(false);
    setRevisionsVisible(true);
    setToolBarVisible(false);
}

} // namespace EffectComposer

#ifdef WITH_TESTS

#include <coreplugin/editormanager/ieditor.h>

#include <QTest>

namespace EffectComposer {

// The code window's editor was a TextEditorWidget subclass: completion with
// the effect's uniforms in a createAssistInterface() override, a Ctrl+Space
// action of its own, the gutter and Tab set on the widget by hand. It is the
// Qt Quick editor over a document that completes the same way.
class EffectCodeEditorTest final : public QObject
{
    Q_OBJECT

private slots:
    void testTheEffectCodeEditorIsTheQtQuickEditor()
    {
        EffectCodeEditorFactory factory;
        QVERIFY2(factory.usesQuickEditor(), "the effect code editor is the widget editor");
        QVERIFY2(factory.lineNumbersVisible() && !factory.marksVisible(),
                 "a shader has no line numbers, or has marks");

        const std::unique_ptr<Core::IEditor> editor(factory.createEditor());
        QVERIFY2(editor.get(), "the factory built nothing");
        QVERIFY2(!TextEditor::TextEditorWidget::fromEditor(editor.get()),
                 "the effect code editor opened as a widget");
        auto * const document = qobject_cast<EffectDocument *>(editor->document());
        QVERIFY2(document, "the effect code editor's document is not an EffectDocument");
        QVERIFY2(document->indenter(), "the shader has no indenter");
        // The shared handle the shader data keeps and connects to.
        QCOMPARE(TextEditor::textDocumentPtr(editor.get()).get(), document);

        // The factory's GLSL highlighter: not the QML one the document's base
        // class builds, and not the generic one the Qt Quick editor gives a
        // document that has none of its own.
        QVERIFY2(qobject_cast<SyntaxHighlighter *>(document->syntaxHighlighter()),
                 "the shader is not highlighted as GLSL");

        // Completion is asked the Effect Composer's way, with the uniforms
        // from the callback the shader data sets.
        document->setUniformsCallback([] { return QStringList{"iTime", "iResolution"}; });
        QTextCursor cursor(document->document());
        const std::unique_ptr<TextEditor::AssistInterface> interface
            = document->createAssistInterface(cursor, TextEditor::Completion,
                                              TextEditor::ExplicitlyInvoked);
        // Not a QObject, and asked only here.
        auto * const effects = dynamic_cast<EffectsCompletionAssistInterface *>(interface.get());
        QVERIFY2(effects, "completion is not asked the Effect Composer's way");
        QCOMPARE(effects->uniformNames(), QStringList({"iTime", "iResolution"}));
    }
};

QObject *createEffectCodeEditorTest()
{
    return new EffectCodeEditorTest;
}

} // namespace EffectComposer

#include "effectcodeeditorwidget.moc"

#endif // WITH_TESTS
