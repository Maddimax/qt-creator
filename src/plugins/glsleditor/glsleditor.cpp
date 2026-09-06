// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "glsleditor.h"

#include "glslautocompleter.h"
#include "glslcompletionassist.h"
#include "glsleditorconstants.h"
#include "glsleditortr.h"
#include "glslhighlighter.h"
#include "glslindenter.h"

#include <glsl/glsllexer.h>
#include <glsl/glslparser.h>
#include <glsl/glslengine.h>
#include <glsl/glslsemantic.h>
#include <glsl/glslsymbols.h>

#include <coreplugin/actionmanager/actionmanager.h>
#include <coreplugin/actionmanager/command.h>
#include <coreplugin/icore.h>

#include <cplusplus/SimpleLexer.h>

#include <extensionsystem/pluginspec.h>

#include <texteditor/fontsettings.h>
#include <texteditor/refactoroverlay.h>
#include <texteditor/textdocument.h>
#include <texteditor/syntaxhighlighter.h>
#include <texteditor/texteditor.h>
#include <texteditor/texteditorconstants.h>

#include <utils/algorithm.h>
#include <utils/changeset.h>
#include <utils/icon.h>
#include <utils/mimeconstants.h>
#include <utils/qtcassert.h>
#include <utils/tooltip/tooltip.h>
#include <utils/uncommentselection.h>

#include <QCoreApplication>
#include <QComboBox>
#include <QFileInfo>
#include <QHeaderView>
#include <QTextBlock>
#include <QTimer>
#include <QTreeView>

#ifdef WITH_TESTS
#include <QTest>
#endif

using namespace TextEditor;
using namespace GLSL;

namespace GlslEditor::Internal {

static int versionFor(const QString &source)
{
    CPlusPlus::SimpleLexer lexer;
    lexer.setPreprocessorMode(false);
    const CPlusPlus::Tokens tokens = lexer(source);

    int version = -1;
//    QString profile;
    const int end = tokens.size();
    for (int it = 0; it + 2 < end; ++it) {
        const CPlusPlus::Token &token = tokens.at(it);
        if (token.isComment())
            continue;
        if (token.kind() == CPlusPlus::T_POUND) {
            const int line = token.lineno;
            const CPlusPlus::Token &successor = tokens.at(it + 1);
            if (line != successor.lineno)
                break;
            if (successor.kind() != CPlusPlus::T_IDENTIFIER)
                break;
            if (source.mid(successor.bytesBegin(), successor.bytes()) != "version")
                break;

            const CPlusPlus::Token &versionToken = tokens.at(it + 2);
            if (line != versionToken.lineno)
                break;
            if (versionToken.kind() != CPlusPlus::T_NUMERIC_LITERAL)
                break;
            version = source.mid(versionToken.bytesBegin(), versionToken.bytes()).toInt();

//            if (version >= 150 && it + 3 < end) {
//                const CPlusPlus::Token &profileToken = tokens.at(it + 3);
//                if (line != profileToken.lineno)
//                    break;
//                if (profileToken.kind() != CPlusPlus::T_IDENTIFIER)
//                    break;
//                profile = source.mid(profileToken.bytesBegin(), profileToken.bytes());
//            }
            break;
        }
        break;
    }
    return version;
}

enum {
    UPDATE_DOCUMENT_DEFAULT_INTERVAL = 150
};

class InitFile final
{
public:
    explicit InitFile(const QString &fileName, bool cutVulkanRemoved = false)
        : m_fileName(fileName), m_cutVulkanRemoved(cutVulkanRemoved) {}

    ~InitFile() { delete m_engine; }

    GLSL::Engine *engine() const
    {
        if (!m_engine)
            initialize();
        return m_engine;
    }

    GLSL::TranslationUnitAST *ast() const
    {
        if (!m_ast)
            initialize();
        return m_ast;
    }

private:
    void initialize() const
    {
        // Parse the builtins for any language variant so we can use all keywords.
        const int variant = m_cutVulkanRemoved
                       ? GLSL::Lexer::Variant_All
                       : (GLSL::Lexer::Variant_All & ~GLSL::Lexer::Variant_Vulkan);

        QByteArray code;
        QFile file(Core::ICore::resourcePath("glsl").pathAppended(m_fileName).toUrlishString());
        if (file.open(QFile::ReadOnly))
            code = file.readAll();

        if (m_cutVulkanRemoved) {
            const int index = code.indexOf("//// Vulkan removed variables and functions");
            if (index != -1)
                code.truncate(index);
        }

        m_engine = new GLSL::Engine();
        GLSL::Parser parser(m_engine, code.constData(), code.size(), variant);
        m_ast = parser.parse();
        QTC_CHECK(m_ast);
    }

    QString m_fileName;
    mutable GLSL::Engine *m_engine = nullptr;
    mutable GLSL::TranslationUnitAST *m_ast = nullptr;
    bool m_cutVulkanRemoved = false;
};

static const InitFile *fragmentShaderInit(int variant)
{
    static InitFile glsl_es_100_frag{"glsl_es_100.frag"};
    static InitFile glsl_120_frag{"glsl_120.frag"};
    static InitFile glsl_330_frag{"glsl_330.frag"};
    static InitFile glsl_460_frag{"glsl_460.frag"};

    if (variant & GLSL::Lexer::Variant_GLSL_460)
        return &glsl_460_frag;

    if (variant & GLSL::Lexer::Variant_GLSL_400)
        return &glsl_330_frag;

    if (variant & GLSL::Lexer::Variant_GLSL_120)
        return  &glsl_120_frag;

    return &glsl_es_100_frag;
}

static const InitFile *vertexShaderInit(int variant)
{
    static InitFile glsl_es_100_vert{"glsl_es_100.vert"};
    static InitFile glsl_120_vert{"glsl_120.vert"};
    static InitFile glsl_330_vert{"glsl_330.vert"};
    static InitFile glsl_330_vert_modified{"glsl_330.vert", true};
    static InitFile glsl_460_vert{"glsl_460.vert"};
    static InitFile glsl_460_vert_modified{"glsl_460.vert", true};

    if (variant & GLSL::Lexer::Variant_GLSL_460) {
        if (variant & GLSL::Lexer::Variant_Vulkan)
            return &glsl_460_vert_modified;
        return &glsl_460_vert;
    }

    if (variant & GLSL::Lexer::Variant_GLSL_400) {
        if (variant & GLSL::Lexer::Variant_Vulkan)
            return &glsl_330_vert_modified;
        return &glsl_330_vert;
    }

    if (variant & GLSL::Lexer::Variant_GLSL_120)
        return &glsl_120_vert;

    return &glsl_es_100_vert;
}

static const InitFile *shaderInit(int variant)
{
    static InitFile glsl_es_100_common{"glsl_es_100_common.glsl"};
    static InitFile glsl_120_common{"glsl_120_common.glsl"};
    static InitFile glsl_330_common{"glsl_330_common.glsl"};
    static InitFile glsl_330_common_modified{"glsl_330_common.glsl", true};
    static InitFile glsl_460_common{"glsl_460_common.glsl"};
    static InitFile glsl_460_common_modified{"glsl_460_common.glsl", true};

    if (variant & GLSL::Lexer::Variant_GLSL_460) {
        if (variant & GLSL::Lexer::Variant_Vulkan)
            return &glsl_460_common_modified;
        return &glsl_460_common;
    }

    if (variant & GLSL::Lexer::Variant_GLSL_400) {
        if (variant & GLSL::Lexer::Variant_Vulkan)
            return &glsl_330_common_modified;
        return &glsl_330_common;
    }

    if (variant & GLSL::Lexer::Variant_GLSL_120)
        return &glsl_120_common;

    return &glsl_es_100_common;
}

static const InitFile *vulkanInit(int /*variant*/)
{
    static InitFile glsl_vulkan{"glsl_vulkan.glsl"};
    return &glsl_vulkan;
}

class CreateRangesMarkSemanticDetails: protected Visitor
{
    QTextDocument *textDocument;
    Document::Ptr glslDocument;
    Namespace *globalNamespace;
    QList<TextDocument::ExtraSelection> marked;
    QTextCharFormat functionCallFormat;
    QTextCharFormat memberFormat;
    QTextCharFormat globalVarFormat;
    QTextCharFormat layoutIdFormat;

public:
    CreateRangesMarkSemanticDetails(QTextDocument *textDocument, Document::Ptr glslDocument,
                                    Namespace *global)
        : textDocument(textDocument), glslDocument(glslDocument), globalNamespace(global)
    {
        const TextEditor::FontSettingsData &fontSettings
                = TextEditor::globalFontSettings().data();
        functionCallFormat = fontSettings.toTextCharFormat(TextEditor::C_FUNCTION);
        memberFormat = fontSettings.toTextCharFormat(TextEditor::C_FIELD);
        globalVarFormat = fontSettings.toTextCharFormat(TextEditor::C_GLOBAL);
        layoutIdFormat = fontSettings.toTextCharFormat(TextEditor::C_ENUMERATION);
    }

    void operator()(AST *ast) { accept(ast); }

    const QList<TextDocument::ExtraSelection> markedSemantics() const { return marked; }

protected:
    using GLSL::Visitor::visit;

    void endVisit(CompoundStatementAST *ast) override
    {
        if (ast->symbol) {
            QTC_ASSERT(ast->length != -1, return);
            QTextCursor tc(textDocument);
            tc.setPosition(ast->position);
            tc.setPosition(ast->position + ast->length, QTextCursor::KeepAnchor);
            glslDocument->addRange(tc, ast->symbol);
        }
    }

    void endVisit(FunctionCallExpressionAST *ast) override
    {
        if (auto id = ast->id) {
            if (auto name = id->name) {
                if (globalNamespace->find(*name))
                    createGlobalMemberEntry(id, functionCallFormat);
            }
        }
    }

    void endVisit(MemberAccessExpressionAST *ast) override
    {
        if (auto member = ast->field) {
            QTC_ASSERT(ast->length != -1, return);
            QTextCursor tc(textDocument);
            const int position = ast->position + ast->length + - member->length();
            tc.setPosition(position);
            tc.setPosition(position + member->length(), QTextCursor::KeepAnchor);
            TextDocument::ExtraSelection sel;
            sel.cursor = tc;
            sel.format = memberFormat;
            marked.append(sel);
        }
    }

    void endVisit(IdentifierExpressionAST *ast) override
    {
        if (auto name = ast->name; name && name->startsWith("gl_")) {
            if (globalNamespace->find(*name))
                createGlobalMemberEntry(ast, globalVarFormat);
        }
    }

    void endVisit(StructTypeAST::Field *ast) override
    {
        if (auto name = ast->name; name && name->startsWith("gl_")) {
            if (globalNamespace->find(*name))
                createGlobalMemberEntry(ast, globalVarFormat);
        }
    }

    void endVisit(InterfaceBlockAST *ast) override
    {
        if (auto name = ast->name; name && name->startsWith("gl_")) {
            if (globalNamespace->find(*name))
                createGlobalMemberEntry(ast, globalVarFormat);
        }
    }

    void endVisit(LayoutQualifierAST *ast) override
    {
        if (ast->name) {
            QTC_ASSERT(ast->length != -1, return);
            QTextCursor tc(textDocument);
            tc.setPosition(ast->position);
            tc.setPosition(ast->position + ast->name->length(), QTextCursor::KeepAnchor);
            TextDocument::ExtraSelection sel;
            sel.cursor = tc;
            sel.format = layoutIdFormat;
            marked.append(sel);
        }
    }

private:
    void createGlobalMemberEntry(AST *ast, const QTextCharFormat &format)
    {
        QTC_ASSERT(ast->length != -1, return);
        QTextCursor tc(textDocument);
        tc.setPosition(ast->position);
        tc.setPosition(ast->position + ast->length, QTextCursor::KeepAnchor);
        TextDocument::ExtraSelection sel;
        sel.cursor = tc;
        sel.format = format;
        marked.append(sel);
    }
};

static Utils::Icon vulkanIcon({{":/glsleditor/images/vulkan.png", Utils::Theme::IconsBaseColor}});

//
//  GlslEditorWidget
//

// What the GLSL in a document turns out to be: the parse, the diagnostics it
// produced, and the ranges the semantic pass marked. Kept on the document
// rather than on an editor, because none of it is about how the file is drawn
// - and because a view that is not a widget has to be able to show it too.
//
// A child of the document, so whoever has the document can find it.
class GlslSemantics final : public QObject
{
    Q_OBJECT

public:
    explicit GlslSemantics(TextDocument *document)
        : QObject(document)
        , m_document(document)
    {
        m_timer.setInterval(UPDATE_DOCUMENT_DEFAULT_INTERVAL);
        m_timer.setSingleShot(true);
        connect(&m_timer, &QTimer::timeout, this, &GlslSemantics::updateNow);
        connect(m_document->document(), &QTextDocument::contentsChanged,
                &m_timer, qOverload<>(&QTimer::start));
    }

    static GlslSemantics *of(TextDocument *document)
    {
        return document ? document->findChild<GlslSemantics *>() : nullptr;
    }

    // Whether the file may use Vulkan keywords. The editor's tool bar says so;
    // re-reading is the caller's business, which is why this does not do it.
    void setVulkanEnabled(bool enabled) { m_vulkanEnabled = enabled; }
    bool isVulkanEnabled() const { return m_vulkanEnabled; }

    Document::Ptr glslDocument() const { return m_glslDocument; }

    void scheduleUpdate() { m_timer.start(); }
    void updateNow();

private:
    TextDocument * const m_document;
    QTimer m_timer;
    bool m_vulkanEnabled = true;
    Document::Ptr m_glslDocument;
};

class GlslEditorWidget : public TextEditorWidget
{
public:
    GlslEditorWidget();

    int editorRevision() const;
    bool isOutdated() const;

    QSet<QString> identifiers() const;

    std::unique_ptr<AssistInterface> createAssistInterface(AssistKind assistKind,
                                                           AssistReason reason) const override;

private:
    void setSelectedElements();
    void onTooltipRequested(const QPoint &point, int pos);
    QString wordUnderCursor() const;

    QComboBox *m_outlineCombo = nullptr;
    QToolButton *m_vulkanSupport = nullptr;
};

GlslEditorWidget::GlslEditorWidget()
{
    setAutoCompleter(new GlslCompleter);

    m_outlineCombo = new QComboBox;
    m_outlineCombo->setMinimumContentsLength(22);

    // ### m_outlineCombo->setModel(m_outlineModel);

    auto treeView = new QTreeView;
    treeView->header()->hide();
    treeView->setItemsExpandable(false);
    treeView->setRootIsDecorated(false);
    m_outlineCombo->setView(treeView);
    treeView->expandAll();

    //m_outlineCombo->setSizeAdjustPolicy(QComboBox::AdjustToContents);

    // Make the combo box prefer to expand
    QSizePolicy policy = m_outlineCombo->sizePolicy();
    policy.setHorizontalPolicy(QSizePolicy::Expanding);
    m_outlineCombo->setSizePolicy(policy);

    m_vulkanSupport = new QToolButton;
    m_vulkanSupport->setCheckable(true);
    m_vulkanSupport->setChecked(true);
    m_vulkanSupport->setIcon(vulkanIcon.icon());
    const auto updateVulkanToolTip = [this] {
        m_vulkanSupport->setToolTip(
            m_vulkanSupport->isChecked() ? Tr::tr("Vulkan support is enabled.")
                                         : Tr::tr("Vulkan support is disabled."));
    };
    updateVulkanToolTip();

    insertExtraToolBarWidget(TextEditorWidget::Left, m_outlineCombo);
    insertExtraToolBarWidget(TextEditorWidget::Right, m_vulkanSupport);

    connect(m_vulkanSupport, &QToolButton::clicked, this, [this, updateVulkanToolTip] {
        updateVulkanToolTip();
        if (GlslSemantics * const semantics = GlslSemantics::of(textDocument())) {
            semantics->setVulkanEnabled(m_vulkanSupport->isChecked());
            semantics->updateNow();
        }
    });
    connect(this, &TextEditorWidget::tooltipRequested, this, &GlslEditorWidget::onTooltipRequested);
}

int GlslEditorWidget::editorRevision() const
{
    //return document()->revision();
    return 0;
}

bool GlslEditorWidget::isOutdated() const
{
//    if (m_semanticInfo.revision() != editorRevision())
//        return true;

    return false;
}

QString GlslEditorWidget::wordUnderCursor() const
{
    QTextCursor tc = textCursor();
    const QChar ch = document()->characterAt(tc.position() - 1);
    // make sure that we're not at the start of the next word.
    if (ch.isLetterOrNumber() || ch == QLatin1Char('_'))
        tc.movePosition(QTextCursor::Left);
    tc.movePosition(QTextCursor::StartOfWord);
    tc.movePosition(QTextCursor::EndOfWord, QTextCursor::KeepAnchor);
    const QString word = tc.selectedText();
    return word;
}

static QTextCursor cursorForDiagnosticMessage(QTextDocument *document,
                                             const DiagnosticMessage &message)
{
    const DiagnosticMessage::Location &location = message.location();
    if (location.length >= 0) {
        QTextCursor cursor(document);
        cursor.setPosition(location.position);
        cursor.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor, location.length);
        return cursor;
    }
    QTC_CHECK(false);
    // we only have a line number
    QTextCursor cursor(document->findBlockByNumber(message.line() - 1));
    static const QRegularExpression ws("^(\\s+)");
    const QRegularExpressionMatch match = ws.match(cursor.block().text());
    if (match.hasMatch())
        cursor.movePosition(QTextCursor::NextCharacter, QTextCursor::MoveAnchor, match.capturedLength(1));
    cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
    return cursor;
}

void GlslSemantics::updateNow()
{
    m_timer.stop();

    int variant = languageVariant(m_document->mimeType());
    const QString contents = m_document->plainText();
    int version = versionFor(contents);
    if (version >= 330) {
        if (version >= 420)
            variant |= GLSL::Lexer::Variant_GLSL_460;
        else
            variant |= GLSL::Lexer::Variant_GLSL_400;
        if (m_vulkanEnabled)
            variant |= GLSL::Lexer::Variant_Vulkan;
    }

    const QByteArray preprocessedCode = contents.toLatin1(); // ### use the QtCreator C++ preprocessor.

    Document::Ptr doc(new Document());
    doc->_currentGlslVersion = version;
    doc->_vulkanEnabled = m_vulkanEnabled;
    doc->_engine = new Engine();
    Parser parser(doc->_engine, preprocessedCode.constData(), preprocessedCode.size(), variant);

    TranslationUnitAST *ast = parser.parse();
    if (ast || m_document->extraSelections(TextEditorWidget::CodeWarningsSelection).isEmpty()) {
        Semantic sem;
        Scope *globalScope = new Namespace();
        doc->_globalScope = globalScope;
        const InitFile *file = shaderInit(variant);
        sem.translationUnit(file->ast(), globalScope, file->engine());
        if (variant & Lexer::Variant_VertexShader) {
            file = vertexShaderInit(variant);
            sem.translationUnit(file->ast(), globalScope, file->engine());
        }
        if (variant & Lexer::Variant_FragmentShader) {
            file = fragmentShaderInit(variant);
            sem.translationUnit(file->ast(), globalScope, file->engine());
        }
        if (variant & Lexer::Variant_Vulkan) {
            file = vulkanInit(variant);
            sem.translationUnit(file->ast(), globalScope, file->engine());
        }
        sem.translationUnit(ast, globalScope, doc->_engine);

        CreateRangesMarkSemanticDetails createRanges(m_document->document(), doc,
                                                     globalScope->asNamespace());
        createRanges(ast);

#if 0
        QTextStream qout(stdout, QIODevice::WriteOnly);
        GLSL::ASTDump dump(qout);
        dump(ast);
#endif
        const TextEditor::FontSettingsData fontSettings = TextEditor::globalFontSettings().data();

        QTextCharFormat warningFormat = fontSettings.toTextCharFormat(TextEditor::C_WARNING);
        QTextCharFormat errorFormat = fontSettings.toTextCharFormat(TextEditor::C_ERROR);

        QList<TextDocument::ExtraSelection> sels;
        QSet<int> errors;
        QSet<DiagnosticMessage::Location> errorsWithLocations;

        const QList<DiagnosticMessage> messages = doc->_engine->diagnosticMessages();
        for (const DiagnosticMessage &m : messages) {
            if (! m.line())
                continue;
            if (!Utils::insert(errorsWithLocations, m.location())) {
                if (!Utils::insert(errors, m.line()))
                    continue;
            }

            TextDocument::ExtraSelection sel;
            sel.cursor = cursorForDiagnosticMessage(m_document->document(), m);
            sel.format = m.isError() ? errorFormat : warningFormat;
            sel.format.setToolTip(m.message());
            sels.append(sel);
        }

        m_document->setExtraSelections(TextEditorWidget::CodeWarningsSelection, sels);
        m_document->setExtraSelections(TextEditorWidget::OtherSelection,
                                       createRanges.markedSemantics());
        m_glslDocument = doc;
    }
}

static QStringList diagnosticMessagesToStringList(const QList<DiagnosticMessage> &dMessages,
                                                  int lineNo, int pos)
{
    QStringList fullLine;
    QStringList specific;
    for (const DiagnosticMessage &msg : dMessages) {
        if (lineNo != msg.line())
            continue;
        const DiagnosticMessage::Location &loc = msg.location();
        if (loc.length == -1)
            fullLine.append(msg.message());
        else if (loc.position <= pos && loc.position + loc.length >= pos)
            specific.append(msg.message());
    }
    if (fullLine.isEmpty())
        return specific;
    return specific + fullLine;
}

void GlslEditorWidget::onTooltipRequested(const QPoint &point, int pos)
{
    GlslSemantics * const semantics = GlslSemantics::of(textDocument());
    const Document::Ptr glslDocument = semantics ? semantics->glslDocument() : Document::Ptr();
    QTC_ASSERT(glslDocument && glslDocument->engine(), return);
    const int lineno = document()->findBlock(pos).blockNumber() + 1;
    const QStringList messages = diagnosticMessagesToStringList(
                glslDocument->engine()->diagnosticMessages(), lineno, pos);
    if (!messages.isEmpty())
        Utils::ToolTip::show(point, messages.join("<hr/>"), this);
    else
        Utils::ToolTip::hide();
}

int languageVariant(const QString &type)
{
    int variant = 0;
    bool isVertex = false;
    bool isFragment = false;
    bool isDesktop = false;
    if (type.isEmpty()) {
        // ### Before file has been opened, so don't know the mime type.
        isVertex = true;
        isFragment = true;
    } else if (type == QLatin1String("text/x-glsl") ||
               type == QLatin1String(Utils::Constants::GLSL_MIMETYPE)) {
        isVertex = true;
        isFragment = true;
        isDesktop = true;
    } else if (type == QLatin1String(Utils::Constants::GLSL_VERT_MIMETYPE)) {
        isVertex = true;
        isDesktop = true;
    } else if (type == QLatin1String(Utils::Constants::GLSL_FRAG_MIMETYPE)) {
        isFragment = true;
        isDesktop = true;
    } else if (type == QLatin1String(Utils::Constants::GLSL_ES_VERT_MIMETYPE)) {
        isVertex = true;
    } else if (type == QLatin1String(Utils::Constants::GLSL_ES_FRAG_MIMETYPE)) {
        isFragment = true;
    } else if (type == QLatin1String(Utils::Constants::GLSL_COMP_MIMETYPE)) {
        isFragment = true; // not really, but we define the respective variables/functions there
    } else if (type == QLatin1String(Utils::Constants::GLSL_TESS_MIMETYPE)) {
        isVertex = true; // not really, but we define the respective variables/functions there
    } else if (type == QLatin1String(Utils::Constants::GLSL_GEOM_MIMETYPE)) {
        isVertex = true; // not really, but we define the respective variables/functions there
    }
    if (isDesktop)
        variant |= Lexer::Variant_GLSL_120;
    else
        variant |= Lexer::Variant_GLSL_ES_100;
    if (isVertex)
        variant |= Lexer::Variant_VertexShader;
    if (isFragment)
        variant |= Lexer::Variant_FragmentShader;
    return variant;
}

std::unique_ptr<AssistInterface> GlslEditorWidget::createAssistInterface(
    AssistKind kind, AssistReason reason) const
{
    if (kind != Completion)
        return TextEditorWidget::createAssistInterface(kind, reason);

    GlslSemantics * const semantics = GlslSemantics::of(textDocument());
    return std::make_unique<GlslCompletionAssistInterface>(
        textCursor(),
        textDocument()->filePath(),
        reason,
        textDocument()->mimeType(),
        semantics ? semantics->glslDocument() : Document::Ptr());
}

#ifdef WITH_TESTS

class GlslEditorTest final : public QObject
{
    Q_OBJECT

private slots:
    // The parse used to belong to the editor widget, so a shader that was not
    // open in one had no diagnostics anywhere. They are the document's now.
    void testAShadersDiagnosticsAreOnItsDocument()
    {
        TextDocument document(Constants::C_GLSLEDITOR_ID);
        document.setMimeType(Utils::Constants::GLSL_FRAG_MIMETYPE);
        auto * const semantics = new GlslSemantics(&document);

        // Well-formed: nothing to report.
        document.document()->setPlainText("void main() { gl_FragColor = vec4(1.0); }\n");
        semantics->updateNow();
        QVERIFY2(document.extraSelections(TextEditorWidget::CodeWarningsSelection).isEmpty(),
                 "a shader that parses was given warnings");
        QVERIFY2(semantics->glslDocument(), "nothing was parsed at all");

        // And a shader that does not parse says where.
        document.document()->setPlainText("void main() { this is not glsl }\n");
        semantics->updateNow();
        const QList<TextDocument::ExtraSelection> warnings
            = document.extraSelections(TextEditorWidget::CodeWarningsSelection);
        QVERIFY2(!warnings.isEmpty(), "a shader that does not parse was given no diagnostics");
        // On the offending line rather than merely somewhere.
        QCOMPARE(warnings.first().cursor.blockNumber(), 0);

        // No editor was opened anywhere in this test.
        QVERIFY(Core::EditorManager::visibleEditors().isEmpty());
    }

    // What the tool bar's Vulkan switch changes: the keywords the parse
    // accepts, which is a property of the file rather than of the editor.
    void testTheVulkanSwitchChangesWhatParses()
    {
        TextDocument document(Constants::C_GLSLEDITOR_ID);
        document.setMimeType(Utils::Constants::GLSL_FRAG_MIMETYPE);
        auto * const semantics = new GlslSemantics(&document);
        QVERIFY2(semantics->isVulkanEnabled(), "Vulkan is off before anyone said so");

        // texture2D is a type only Vulkan knows, and only from #version 330.
        document.document()->setPlainText("#version 450\nuniform texture2D t;\nvoid main() {}\n");

        semantics->updateNow();
        QVERIFY2(semantics->glslDocument(), "nothing was parsed");
        QVERIFY2(semantics->glslDocument()->vulkanEnabled(),
                 "the parse did not record that Vulkan was on");
        QVERIFY2(document.extraSelections(TextEditorWidget::CodeWarningsSelection).isEmpty(),
                 "a Vulkan shader was faulted while Vulkan was on");

        semantics->setVulkanEnabled(false);
        QVERIFY(!semantics->isVulkanEnabled());
        semantics->updateNow();
        QVERIFY2(!semantics->glslDocument()->vulkanEnabled(),
                 "the parse used Vulkan after the switch turned it off");
        QVERIFY2(!document.extraSelections(TextEditorWidget::CodeWarningsSelection).isEmpty(),
                 "a Vulkan type still parsed after the switch turned Vulkan off");
    }
};

QObject *createGlslEditorTest()
{
    return new GlslEditorTest;
}

#endif // WITH_TESTS

//  GlslEditorFactory

class GlslEditorFactory final : public TextEditor::TextEditorFactory
{
public:
    GlslEditorFactory()
    {
        setId(Constants::C_GLSLEDITOR_ID);
        setDisplayName(Tr::tr("GLSL Editor"));
        addMimeType(Utils::Constants::GLSL_MIMETYPE);
        addMimeType(Utils::Constants::GLSL_VERT_MIMETYPE);
        addMimeType(Utils::Constants::GLSL_FRAG_MIMETYPE);
        addMimeType(Utils::Constants::GLSL_ES_VERT_MIMETYPE);
        addMimeType(Utils::Constants::GLSL_ES_FRAG_MIMETYPE);
        addMimeType(Utils::Constants::GLSL_COMP_MIMETYPE);
        addMimeType(Utils::Constants::GLSL_TESS_MIMETYPE);
        addMimeType(Utils::Constants::GLSL_GEOM_MIMETYPE);

        setDocumentCreator([]() {
            auto * const document = new TextDocument(Constants::C_GLSLEDITOR_ID);
            // What the file means, kept with the file rather than with an
            // editor: two editors on one shader parse it once between them,
            // and a view that is not a widget gets it too.
            new GlslSemantics(document);
            return document;
        });
        setEditorWidgetCreator([]() { return new GlslEditorWidget; });
        setIndenterCreator(&createGlslIndenter);
        setSyntaxHighlighterCreator(&createGlslHighlighter);
        setCommentDefinition(Utils::CommentDefinition::CppStyle);
        setCompletionAssistProvider(createGlslCompletionAssistProvider());
        setParenthesesMatchingEnabled(true);
        setCodeFoldingSupported(true);

        setOptionalActionMask(OptionalActions::Format
                                | OptionalActions::UnCommentSelection
                                | OptionalActions::UnCollapseAll);
    }
};

void setupGlslEditorFactory()
{
    static GlslEditorFactory theGlslEditorFactory;
}

} // GlslEditor::Internal

#include "glsleditor.moc"
