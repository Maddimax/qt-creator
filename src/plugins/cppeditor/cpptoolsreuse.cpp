// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "cpptoolsreuse.h"

#include "clangdsettings.h"
#include "cppautocompleter.h"
#include "cppcanonicalsymbol.h"
#include "cppcodemodelsettings.h"
#include "cppcompletionassist.h"
#include "cppeditordocument.h"
#include "cppeditorwidget.h"
#include "cppeditorconstants.h"
#include "cppeditortr.h"
#include "cppfilesettingspage.h"
#include "cpphighlighter.h"
#include "cppqtstyleindenter.h"
#include "quickfixes/cppquickfixassistant.h"

#include <coreplugin/documentmanager.h>
#include <coreplugin/editormanager/editormanager.h>
#include <coreplugin/messagemanager.h>

#include <projectexplorer/projectexplorerconstants.h>
#include <projectexplorer/projectmanager.h>
#include <projectexplorer/projecttree.h>
#include <projectexplorer/projectnodes.h>

#include <texteditor/codeassist/assistinterface.h>
#include <texteditor/displaysettings.h>
#include <texteditor/textdocument.h>
#include <texteditor/texteditorconstants.h>
#include <coreplugin/actionmanager/actionmanager.h>
#include <coreplugin/coreconstants.h>
#include <coreplugin/actionmanager/command.h>
#include <texteditor/texteditor.h>

#ifdef WITH_TESTS
#include <coreplugin/editormanager/ieditor.h>
#include <coreplugin/find/searchresultwindow.h>
#include <texteditor/symbolrequests.h>
#include <QSignalSpy>
#include "modelmanagertesthelper.h"
#include "cpplocalrenaming.h"
#include <utils/temporarydirectory.h>
#include <QApplication>
#include <QClipboard>
#include <QScopeGuard>
#include <QTest>
#endif

#include <cplusplus/BackwardsScanner.h>
#include <cplusplus/ASTPath.h>
#include <cplusplus/declarationcomments.h>
#include <cplusplus/Overview.h>
#include <cplusplus/SimpleLexer.h>

#include <utils/algorithm.h>
#include <utils/qtcassert.h>
#include <utils/textfileformat.h>
#include <utils/textutils.h>

#include <QDebug>
#include <QElapsedTimer>
#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <QStringView>
#include <QTextCursor>
#include <QTextDocument>

#include <optional>
#include <vector>

using namespace CPlusPlus;
using namespace Utils;

namespace CppEditor {

static int skipChars(QTextCursor *tc,
                      QTextCursor::MoveOperation op,
                      int offset,
                      std::function<bool(const QChar &)> skip)
{
    const QTextDocument *doc = tc->document();
    if (!doc)
        return 0;
    QChar ch = doc->characterAt(tc->position() + offset);
    if (ch.isNull())
        return 0;
    int count = 0;
    while (skip(ch)) {
        if (tc->movePosition(op))
            ++count;
        else
            break;
        ch = doc->characterAt(tc->position() + offset);
    }
    return count;
}

static int skipCharsForward(QTextCursor *tc, const std::function<bool(const QChar &)> &skip)
{
    return skipChars(tc, QTextCursor::NextCharacter, 0, skip);
}

static int skipCharsBackward(QTextCursor *tc, const std::function<bool(const QChar &)> &skip)
{
    return skipChars(tc, QTextCursor::PreviousCharacter, -1, skip);
}

QStringList identifierWordsUnderCursor(const QTextCursor &tc)
{
    const QTextDocument *document = tc.document();
    if (!document)
        return {};
    const auto isSpace = [](const QChar &c) { return c.isSpace(); };
    const auto isColon = [](const QChar &c) { return c == ':'; };
    const auto isValidIdentifierCharAt = [document](const QTextCursor &tc) {
        return isValidIdentifierChar(document->characterAt(tc.position()));
    };
    // move to the end
    QTextCursor endCursor(tc);
    do {
        moveCursorToEndOfIdentifier(&endCursor);
        // possibly skip ::
        QTextCursor temp(endCursor);
        skipCharsForward(&temp, isSpace);
        const int colons = skipCharsForward(&temp, isColon);
        skipCharsForward(&temp, isSpace);
        if (colons == 2 && isValidIdentifierCharAt(temp))
            endCursor = temp;
    } while (isValidIdentifierCharAt(endCursor));

    QStringList results;
    QTextCursor startCursor(endCursor);
    do {
        moveCursorToStartOfIdentifier(&startCursor);
        if (startCursor.position() == endCursor.position())
            break;
        QTextCursor temp(endCursor);
        temp.setPosition(startCursor.position(), QTextCursor::KeepAnchor);
        static const QRegularExpression rexgexp("\\s");
        results.append(temp.selectedText().remove(rexgexp));
        // possibly skip ::
        temp = startCursor;
        skipCharsBackward(&temp, isSpace);
        const int colons = skipCharsBackward(&temp, isColon);
        skipCharsBackward(&temp, isSpace);
        if (colons == 2
                && isValidIdentifierChar(document->characterAt(temp.position() - 1))) {
            startCursor = temp;
        }
    } while (!isValidIdentifierCharAt(startCursor));
    return results;
}

void findUsagesOf(Core::IEditor *editor, QTextCursor cursor)
{
    if (!editor)
        return;
    const auto document = qobject_cast<TextEditor::TextDocument *>(editor->document());
    if (cursor.isNull())
        cursor = TextEditor::textCursorOf(editor);
    if (!document || cursor.isNull())
        return;
    CppModelManager::findUsages(
        CursorInEditor{cursor,
                       document->filePath(),
                       qobject_cast<CppEditorWidget *>(editor->widget()),
                       document});
}

void renameUsagesOf(Core::IEditor *editor, const QString &replacement, QTextCursor cursor)
{
    if (!editor)
        return;
    const auto document = qobject_cast<TextEditor::TextDocument *>(editor->document());
    if (cursor.isNull())
        cursor = TextEditor::textCursorOf(editor);
    if (!document || cursor.isNull())
        return;

    const CursorInEditor data{cursor,
                              document->filePath(),
                              qobject_cast<CppEditorWidget *>(editor->widget()),
                              document};

    // Where the symbol is declared decides whether renaming it is a good idea
    // at all, so the rename waits for follow symbol to say.
    const Utils::LinkHandler continuation
        = [data, replacement, alive = QPointer(editor)](const Utils::Link &link) {
              if (!alive)
                  return;
              showRenameWarningIfFileIsGenerated(link.targetFilePath);
              CppModelManager::globalRename(data, replacement);
          };
    Internal::NonInteractiveFollowSymbolMarker niMarker;
    CppModelManager::followSymbol(data, continuation, false, false, FollowSymbolMode::Exact);
}

// A string literal that names something openable: an http(s) URL, or a qrc
// path that some resource file in the project provides.
static bool followUrlIn(const CursorInEditor &data, const Utils::LinkHandler &callback)
{
    // The parse has to describe what the file says now, or the token offsets
    // below point into the wrong text.
    if (const auto document = qobject_cast<CppEditorDocument *>(data.textDocument())) {
        if (!document->isSemanticInfoValid())
            return false;
    }

    const CPlusPlus::Document::Ptr parse = semanticDocumentOf(data.textDocument());
    if (!parse)
        return false;

    const ProjectExplorer::Project * const project
        = ProjectExplorer::ProjectTree::currentProject();
    if (!project || !project->rootProjectNode())
        return false;

    const QList<CPlusPlus::AST *> astPath = CPlusPlus::ASTPath(parse)(data.cursor());
    if (astPath.isEmpty())
        return false;
    const CPlusPlus::StringLiteralAST * const literalAst = astPath.last()->asStringLiteral();
    if (!literalAst)
        return false;
    const CPlusPlus::StringLiteral * const literal
        = parse->translationUnit()->stringLiteral(literalAst->literal_token);
    if (!literal)
        return false;
    const QString theString = QString::fromUtf8(literal->chars(), literal->size());

    QTextDocument * const text = data.cursor().document();
    const auto withTokenRange = [&](Utils::Link link) {
        link.linkTextStart = parse->translationUnit()->getTokenPositionInDocument(
            literalAst->literal_token, text);
        link.linkTextEnd = parse->translationUnit()->getTokenEndPositionInDocument(
            literalAst->literal_token, text);
        return link;
    };

    if (theString.startsWith("https:/") || theString.startsWith("http:/")) {
        callback(withTokenRange(Utils::FilePath::fromPathPart(theString)));
        return true;
    }

    if (!theString.startsWith("qrc:/") && !theString.startsWith(":/"))
        return false;

    const ProjectExplorer::Node * const nodeForPath
        = project->rootProjectNode()->findNode(
            [qrcPath = theString.mid(theString.indexOf(':') + 1)](ProjectExplorer::Node *n) {
                if (!n->asFileNode())
                    return false;
                const auto qrcNode = dynamic_cast<ProjectExplorer::ResourceFileNode *>(n);
                return qrcNode && qrcNode->qrcPath() == qrcPath;
            });
    if (!nodeForPath)
        return false;

    callback(withTokenRange(Utils::Link(nodeForPath->filePath())));
    return true;
}

void followCppSymbol(const CursorInEditor &data,
                     const Utils::LinkHandler &callback,
                     bool resolveTarget,
                     bool inNextSplit)
{
    if (!CppModelManager::instance())
        return callback(Utils::Link());

    if (followUrlIn(data, callback))
        return;

    // Following a "leaf" symbol in a generated UI header takes the reader to
    // the designer instead, where the thing it names actually comes from.
    QTextCursor word(data.cursor());
    word.select(QTextCursor::WordUnderCursor);
    const Utils::LinkHandler toDesigner =
        [start = word.selectionStart(), end = word.selectionEnd(),
         text = QPointer(data.cursor().document()), callback,
         filePath = data.filePath()](const Utils::Link &link) {
            const int linkPos = text ? link.target.toPositionInDocument(text) : -1;
            if (link.targetFilePath == filePath && linkPos >= start && linkPos < end) {
                const QString fileName = filePath.fileName();
                if (fileName.startsWith("ui_") && fileName.endsWith(".h")) {
                    const QString uiFileName = fileName.mid(3, fileName.size() - 4) + "ui";
                    const auto nodeMatcher = [uiFileName](ProjectExplorer::Node *n) {
                        return n->filePath().fileName() == uiFileName;
                    };
                    for (const ProjectExplorer::Project * const project :
                         ProjectExplorer::ProjectManager::projects()) {
                        ProjectExplorer::ProjectNode * const rootNode
                            = project->rootProjectNode();
                        if (!rootNode)
                            continue;
                        if (const ProjectExplorer::Node * const uiNode
                            = rootNode->findNode(nodeMatcher)) {
                            Core::EditorManager::openEditor(uiNode->filePath());
                            return;
                        }
                    }
                }
            }
            callback(link);
        };

    CppModelManager::followSymbol(data, toDesigner, resolveTarget, inNextSplit,
                                  FollowSymbolMode::Fuzzy);
}

CPlusPlus::Document::Ptr semanticDocumentOf(TextEditor::TextDocument *document)
{
    if (!document)
        return {};
    if (auto * const cppDocument = qobject_cast<CppEditorDocument *>(document))
        return cppDocument->semanticInfo().doc;
    return {};
}

// The widget where \a data carries one, and otherwise whichever editor is
// showing that document - which is the one the reader is in, because asking
// about the symbol under the caret is something they just did.
Core::IEditor *editorFor(const CursorInEditor &data)
{
    if (data.editorWidget())
        return TextEditor::editorForWidget(data.editorWidget());
    Core::IEditor * const current = Core::EditorManager::currentEditor();
    if (current && current->document() == data.textDocument())
        return current;
    return nullptr;
}

Core::IEditor *editorFor(TextEditor::TextEditorWidget *widget)
{
    if (!widget)
        return nullptr;
    const QList<TextEditor::BaseTextEditor *> editors
        = TextEditor::BaseTextEditor::textEditorsForDocument(widget->textDocument());
    for (TextEditor::BaseTextEditor * const editor : editors) {
        if (editor->editorWidget() == widget)
            return editor;
    }
    return nullptr;
}

// The three things every jump of this shape needs: where the caret is, what
// to ask about it, and where to put the reader when the answer comes back.
static void jumpFromCaret(
    Core::IEditor *editor,
    bool inNextSplit,
    const std::function<void(const CursorInEditor &, const Utils::LinkHandler &)> &ask)
{
    if (!editor)
        return;
    const auto document = qobject_cast<TextEditor::TextDocument *>(editor->document());
    const QTextCursor cursor = TextEditor::textCursorOf(editor);
    if (!document || cursor.isNull())
        return;

    // The widget where the view is one: its own semantic info carries the
    // local uses, and the model manager prefers it where it is there.
    const CursorInEditor data(cursor,
                              document->filePath(),
                              qobject_cast<CppEditorWidget *>(editor->widget()),
                              document);
    const bool split = inNextSplit != TextEditor::displaySettings().openLinksInNextSplit();
    ask(data, [target = QPointer(editor), split](const Utils::Link &link) {
        if (target && link.hasValidTarget())
            TextEditor::openLinkInEditor(target, link, split);
    });
}

void switchDeclarationDefinition(Core::IEditor *editor, bool inNextSplit)
{
    jumpFromCaret(editor, inNextSplit,
                  [](const CursorInEditor &data, const Utils::LinkHandler &callback) {
                      CppModelManager::switchDeclDef(data, callback);
                  });
}

void goToParentImpl(Core::IEditor *editor, bool inNextSplit)
{
    jumpFromCaret(editor, inNextSplit,
                  [](const CursorInEditor &data, const Utils::LinkHandler &callback) {
                      CppModelManager::followFunctionToParentImpl(data, callback);
                  });
}

void moveCursorToEndOfIdentifier(QTextCursor *tc)
{
    skipCharsForward(tc, isValidIdentifierChar);
}

void moveCursorToStartOfIdentifier(QTextCursor *tc)
{
    skipCharsBackward(tc, isValidIdentifierChar);
}

bool isValidAsciiIdentifierChar(const QChar &ch)
{
    return ch.isLetterOrNumber() || ch == QLatin1Char('_');
}

bool isValidFirstIdentifierChar(const QChar &ch)
{
    return ch.isLetter() || ch == QLatin1Char('_') || ch.isHighSurrogate() || ch.isLowSurrogate();
}

bool isValidIdentifierChar(const QChar &ch)
{
    return isValidFirstIdentifierChar(ch) || ch.isNumber();
}

bool isValidIdentifier(const QString &s)
{
    const int length = s.size();
    for (int i = 0; i < length; ++i) {
        const QChar &c = s.at(i);
        if (i == 0) {
            if (!isValidFirstIdentifierChar(c))
                return false;
        } else {
            if (!isValidIdentifierChar(c))
                return false;
        }
    }
    return true;
}

int activeArgumentForPrefix(const QString &prefix)
{
    int argnr = 0;
    int parcount = 0;
    int braceCount = 0;
    SimpleLexer tokenize;
    Tokens tokens = tokenize(prefix);
    for (int i = 0; i < tokens.count(); ++i) {
        const Token &tk = tokens.at(i);
        if (tk.is(T_LPAREN))
            ++parcount;
        else if (tk.is(T_RPAREN))
            --parcount;
        else if (tk.is(T_LBRACE))
            ++braceCount;
        else if (tk.is(T_RBRACE))
            --braceCount;
        else if (!parcount && !braceCount && tk.is(T_COMMA))
            ++argnr;
    }

    if (parcount < 0 || braceCount < 0)
        return -1;

    return argnr;
}

bool isQtKeyword(QStringView text)
{
    switch (text.length()) {
    case 4:
        switch (text.at(0).toLatin1()) {
        case 'e':
            if (text == QLatin1String("emit"))
                return true;
            break;
        case 'S':
            if (text == QLatin1String("SLOT"))
                return true;
            break;
        }
        break;

    case 5:
        if (text.at(0) == QLatin1Char('s') && text == QLatin1String("slots"))
            return true;
        break;

    case 6:
        if (text.at(0) == QLatin1Char('S') && text == QLatin1String("SIGNAL"))
            return true;
        break;

    case 7:
        switch (text.at(0).toLatin1()) {
        case 's':
            if (text == QLatin1String("signals"))
                return true;
            break;
        case 'f':
            if (text == QLatin1String("foreach") || text ==  QLatin1String("forever"))
                return true;
            break;
        }
        break;

    default:
        break;
    }
    return false;
}

QString identifierUnderCursor(QTextCursor *cursor)
{
    cursor->movePosition(QTextCursor::StartOfWord);
    cursor->movePosition(QTextCursor::EndOfWord, QTextCursor::KeepAnchor);
    return cursor->selectedText();
}

const Macro *findCanonicalMacro(const QTextCursor &cursor, Document::Ptr document)
{
    QTC_ASSERT(document, return nullptr);

    if (const Macro *macro = document->findMacroDefinitionAt(cursor.blockNumber() + 1)) {
        QTextCursor macroCursor = cursor;
        const QByteArray name = identifierUnderCursor(&macroCursor).toUtf8();
        if (macro->name() == name)
            return macro;
    } else if (const Document::MacroUse *use = document->findMacroUseAt(cursor.position())) {
        return &use->macro();
    }

    return nullptr;
}

bool isInCommentOrString(const TextEditor::AssistInterface *interface,
                         CPlusPlus::LanguageFeatures features)
{
    QTextCursor tc(interface->textDocument());
    tc.setPosition(interface->position());
    return isInCommentOrString(tc, features);
}

bool isInCommentOrString(const QTextCursor &cursor, CPlusPlus::LanguageFeatures features)
{
    SimpleLexer tokenize;
    features.qtMocRunEnabled = true;
    tokenize.setLanguageFeatures(features);
    tokenize.setSkipComments(false);
    const Tokens &tokens = tokenize(cursor.block().text(),
                                    BackwardsScanner::previousBlockState(cursor.block()));
    const int tokenIdx = SimpleLexer::tokenBefore(tokens, qMax(0, cursor.positionInBlock() - 1));
    const Token tk = (tokenIdx == -1) ? Token() : tokens.at(tokenIdx);

    if (tk.isComment())
        return true;
    if (!tk.isStringLiteral())
        return false;
    if (tokens.size() == 3 && tokens.at(0).kind() == T_POUND
        && tokens.at(1).kind() == T_IDENTIFIER) {
        const QString &line = cursor.block().text();
        const Token &idToken = tokens.at(1);
        QStringView identifier = QStringView(line).mid(idToken.utf16charsBegin(),
                                                       idToken.utf16chars());
        if (identifier == QLatin1String("include")
            || identifier == QLatin1String("include_next")
            || (features.objCEnabled && identifier == QLatin1String("import"))) {
            return false;
        }
    }
    return true;
}

TextEditor::QuickFixOperations quickFixOperations(const TextEditor::AssistInterface *interface)
{
    return Internal::quickFixOperations(interface);
}

CppCompletionAssistProcessor *getCppCompletionAssistProcessor()
{
    return new Internal::InternalCppCompletionAssistProcessor();
}

QString deriveHeaderGuard(const Utils::FilePath &filePath, ProjectExplorer::Project *project)
{
    return Internal::headerGuardForProject(project, filePath);
}

bool fileSizeExceedsLimit(const FilePath &filePath, int sizeLimitInMb)
{
    if (sizeLimitInMb <= 0)
        return false;

    const qint64 fileSizeInMB = filePath.fileSize() / (1000 * 1000);
    if (fileSizeInMB > sizeLimitInMb) {
        Core::MessageManager::writeSilently(Tr::tr("C++ Indexer: Skipping file \"%1\" because "
                                                   "it is too big.").arg(filePath.displayName()));
        return true;
    }
    return false;
}

void openEditor(const Utils::FilePath &filePath, bool inNextSplit, Utils::Id editorId)
{
    using Core::EditorManager;
    EditorManager::openEditor(filePath, editorId, inNextSplit ? EditorManager::OpenInOtherSplit
                                                              : EditorManager::NoFlags);
}

bool preferLowerCaseFileNames(ProjectExplorer::Project *project)
{
    return Internal::cppFileSettingsForProject(project).lowerCaseFiles;
}

QString preferredCxxHeaderSuffix(ProjectExplorer::Project *project)
{
    return Internal::cppFileSettingsForProject(project).headerSuffix;
}

QString preferredCxxSourceSuffix(ProjectExplorer::Project *project)
{
    return Internal::cppFileSettingsForProject(project).sourceSuffix;
}

SearchResultItems symbolOccurrencesInDeclarationComments(
    const Utils::SearchResultItems &symbolOccurrencesInCode)
{
    if (symbolOccurrencesInCode.isEmpty())
        return {};

    // When using clangd, this function gets called every time the replacement string changes,
    // so cache the results.
    static QHash<SearchResultItems, SearchResultItems> resultCache;
    if (const auto it = resultCache.constFind(symbolOccurrencesInCode);
        it != resultCache.constEnd())  {
        return it.value();
    }
    if (resultCache.size() > 5)
        resultCache.clear();

    QElapsedTimer timer;
    timer.start();
    Snapshot snapshot = CppModelManager::snapshot();
    std::vector<std::unique_ptr<QTextDocument>> docPool;
    using FileData = std::tuple<QTextDocument *, QString, Document::Ptr, QList<Token>>;
    QHash<FilePath, FileData> dataPerFile;
    QString symbolName;
    const auto fileData = [&](const FilePath &filePath) -> FileData & {
        auto &data = dataPerFile[filePath];
        auto &[doc, content, cppDoc, allCommentTokens] = data;
        if (!doc) {
            if (TextEditor::TextDocument * const textDoc
                = TextEditor::TextDocument::textDocumentForFilePath(filePath)) {
                doc = textDoc->document();
            } else {
                std::unique_ptr<QTextDocument> newDoc = std::make_unique<QTextDocument>();
                TextFileFormat format;
                const TextFileFormat::ReadResult result = format.readFile(
                        filePath, Core::EditorManager::defaultTextEncoding());
                if (result.code == TextFileFormat::ReadSuccess)
                    newDoc->setPlainText(result.content);

                doc = newDoc.get();
                docPool.push_back(std::move(newDoc));
            }
            content = doc->toPlainText();
            cppDoc = snapshot.preprocessedDocument(content.toUtf8(), filePath);
            cppDoc->check();
        }
        return data;
    };
    static const auto addToken = [](QList<Token> &tokens, const Token &tok) {
        if (!Utils::contains(tokens, [&tok](const Token &t) {
                return t.byteOffset == tok.byteOffset; })) {
            tokens << tok;
        }
    };

    struct ClassInfo {
        FilePath filePath;
        int startOffset = -1;
        int endOffset = -1;
    };
    std::optional<ClassInfo> classInfo;

    // Collect comment blocks associated with replace locations.
    for (const SearchResultItem &item : symbolOccurrencesInCode) {
        const FilePath filePath = FilePath::fromUserInput(item.path().last());
        auto &[doc, _, cppDoc, allCommentTokens] = fileData(filePath);
        const Text::Range &range = item.mainRange();
        if (symbolName.isEmpty())
            symbolName = range.text(doc);
        const QList<Token> commentTokens = commentsForDeclaration(symbolName, range.begin,
                                                                  *doc, cppDoc);
        for (const Token &tok : commentTokens)
            addToken(allCommentTokens, tok);

        if (!classInfo) {
            QTextCursor cursor = range.begin.toTextCursor(doc);
            Internal::CanonicalSymbol cs(cppDoc, snapshot);
            Symbol * const canonicalSymbol = cs(cursor);
            if (canonicalSymbol) {
                classInfo.emplace();
                if (Class * const klass = canonicalSymbol->asClass()) {
                    classInfo->filePath = canonicalSymbol->filePath();
                    classInfo->startOffset = klass->startOffset();
                    classInfo->endOffset = klass->endOffset();
                }
            }
        }

        // We hook in between the end of the "regular" search and (possibly non-interactive)
        // actions on it, so we must run synchronously in the UI thread and therefore be fast.
        // If we notice we are lagging, just abort, as renaming the comments is not
        // required for code correctness.
        if (timer.elapsed() > 1000) {
            resultCache.insert(symbolOccurrencesInCode, {});
            return {};
        }
    }

    // If the symbol is a class, collect all comment blocks in the class body.
    if (classInfo && !classInfo->filePath.isEmpty()) {
        auto &[_1, _2, symbolCppDoc, commentTokens] = fileData(classInfo->filePath);
        TranslationUnit * const tu = symbolCppDoc->translationUnit();
        for (int i = 0; i < tu->commentCount(); ++i) {
            const Token &tok = tu->commentAt(i);
            if (tok.bytesBegin() < classInfo->startOffset)
                continue;
            if (tok.bytesBegin() >= classInfo->endOffset)
                break;
            addToken(commentTokens, tok);
        }
    }

    // Create new replace items for occurrences of the symbol name in collected comment blocks.
    SearchResultItems commentItems;
    for (auto it = dataPerFile.cbegin(); it != dataPerFile.cend(); ++it) {
        const auto &[doc, content, cppDoc, commentTokens] = it.value();
        const QStringView docView(content);
        for (const Token &tok : commentTokens) {
            const int tokenStartPos = cppDoc->translationUnit()->getTokenPositionInDocument(
                tok, doc);
            const int tokenEndPos = cppDoc->translationUnit()->getTokenEndPositionInDocument(
                tok, doc);
            const QStringView tokenView = docView.mid(tokenStartPos, tokenEndPos - tokenStartPos);
            const QList<Text::Range> ranges = symbolOccurrencesInText(
                *doc, tokenView, tokenStartPos, symbolName);
            for (const Text::Range &range : ranges) {
                SearchResultItem item;
                item.setUseTextEditorFont(true);
                item.setFilePath(it.key());
                item.setMainRange(range);
                item.setLineText(doc->findBlockByNumber(range.begin.line - 1).text());
                commentItems << item;
            }
        }
    }

    resultCache.insert(symbolOccurrencesInCode, commentItems);
    return commentItems;
}

QList<Text::Range> symbolOccurrencesInText(const QTextDocument &doc, QStringView text, int offset,
                                           const QString &symbolName)
{
    QTC_ASSERT(!symbolName.isEmpty(), return QList<Text::Range>());
    QList<Text::Range> ranges;
    int index = 0;
    while (true) {
        index = text.indexOf(symbolName, index);
        if (index == -1)
            break;

        // Prevent substring matching.
        const auto checkAdjacent = [&](int i) {
            if (i == -1 || i == text.size())
                return true;
            const QChar c = text.at(i);
            if (c.isLetterOrNumber() || c == '_') {
                index += symbolName.size();
                return false;
            }
            return true;
        };
        if (!checkAdjacent(index - 1))
            continue;
        if (!checkAdjacent(index + symbolName.size()))
            continue;

        const Text::Position startPos = Text::Position::fromPositionInDocument(&doc, offset + index);
        index += symbolName.size();
        const Text::Position endPos = Text::Position::fromPositionInDocument(&doc, offset + index);
        ranges << Text::Range{startPos, endPos};
    }
    return ranges;
}

QList<Text::Range> symbolOccurrencesInDeclarationComments(CppEditorDocument *document,
                                                          const QTextCursor &cursor)
{
    if (!document)
        return {};
    const SemanticInfo semanticInfo = document->semanticInfo();
    const Document::Ptr &cppDoc = semanticInfo.doc;
    if (!cppDoc)
        return {};
    Internal::CanonicalSymbol cs(cppDoc, semanticInfo.snapshot);
    const Symbol * const symbol = cs(cursor);
    if (!symbol || !symbol->asArgument())
        return {};
    const QTextDocument * const textDoc = document->document();
    QTC_ASSERT(textDoc, return {});
    const QList<Token> comments = commentsForDeclaration(symbol, *textDoc, cppDoc);
    if (comments.isEmpty())
        return {};
    QList<Text::Range> ranges;
    const QString &content = textDoc->toPlainText();
    const QStringView docView = QStringView(content);
    const QString symbolName = Overview().prettyName(symbol->name());
    for (const Token &tok : comments) {
        const int tokenStartPos = cppDoc->translationUnit()->getTokenPositionInDocument(
            tok, textDoc);
        const int tokenEndPos = cppDoc->translationUnit()->getTokenEndPositionInDocument(
            tok, textDoc);
        const QStringView tokenView = docView.mid(tokenStartPos, tokenEndPos - tokenStartPos);
        ranges << symbolOccurrencesInText(*textDoc, tokenView, tokenStartPos, symbolName);
    }
    return ranges;
}

namespace Internal {

#ifdef WITH_TESTS

// Switching between a declaration and its definition is a caret read and a
// caret moved, and both were taken from CppEditorWidget - so Shift+F2 in the
// Qt Quick editor did nothing at all.
class SymbolJumpTest final : public QObject
{
    Q_OBJECT

private slots:
    void testDeclDefJumpsInAnyView()
    {
        Utils::TemporaryDirectory dir("cpp-decl-def-without-a-widget");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("c.cpp");
        QVERIFY(file.writeFileContents("struct C {\n    void f();\n};\n\nvoid C::f()\n{\n}\n"));

        // The built-in model does this jump; clangd answers it itself, and
        // usesClangd() is settled when the document is opened.
        const bool wasClangd = ClangdSettings::instance().useClangd();
        const QScopeGuard restoreClangd(
            [wasClangd] { ClangdSettings::setUseClangd(wasClangd); });
        ClangdSettings::setUseClangd(false);

        TextEditor::TextEditorFactory * const editorFactory
            = TextEditor::TextEditorFactory::preferredFactoryFor(file);
        QVERIFY2(editorFactory, "no editor factory claims a C++ file");
        const bool wasQuick = editorFactory->usesQuickEditor();
        const QScopeGuard restore(
            [editorFactory, wasQuick] { editorFactory->setUsesQuickEditor(wasQuick); });
        editorFactory->setUsesQuickEditor(true);

        Core::IEditor * const editor = Core::EditorManager::openEditor(file);
        QVERIFY2(editor, "the editor manager opened nothing");
        const QScopeGuard closeIt(
            [editor] { Core::EditorManager::closeEditors({editor}, false); });
        QVERIFY2(!TextEditor::TextEditorWidget::fromEditor(editor),
                 "the C++ file opened in a widget editor, so this tests nothing");

        auto * const document = qobject_cast<CppEditorDocument *>(editor->document());
        QVERIFY(document);
        document->recalculateSemanticInfo();
        QTRY_VERIFY2(document->isSemanticInfoValid(),
                     "the document never worked out what the file says");

        // On f() in the declaration, which is line 2.
        editor->gotoLine(2, 10);
        QCOMPARE(editor->currentLine(), 2);
        switchDeclarationDefinition(editor, /*inNextSplit=*/false);
        QTRY_COMPARE(editor->currentLine(), 5);

        // And back, so that what is asserted is the jump rather than one
        // position being reachable.
        switchDeclarationDefinition(editor, /*inNextSplit=*/false);
        QTRY_COMPARE(editor->currentLine(), 2);
    }

    // An in-place rename spans every use of the name at once, and a
    // completion applied into it has to reach them all. The widget editor
    // heard about that through CppEditorWidget::encourageApply(); a view that
    // is not one had nowhere to hear it, so the applied text changed the use
    // the caret was in and left the others behind.
    void testAnAppliedCompletionReachesTheWholeRename()
    {
        Utils::TemporaryDirectory dir("cpp-rename-apply-in-any-view");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("rename.cpp");
        QVERIFY(file.writeFileContents(
            "int main()\n{\n    int alpha = 1;\n    return alpha + alpha;\n}\n"));

        const bool wasClangd = ClangdSettings::instance().useClangd();
        const QScopeGuard restoreClangd(
            [wasClangd] { ClangdSettings::setUseClangd(wasClangd); });
        ClangdSettings::setUseClangd(false);

        TextEditor::TextEditorFactory * const editorFactory
            = TextEditor::TextEditorFactory::preferredFactoryFor(file);
        QVERIFY(editorFactory);
        const bool wasQuick = editorFactory->usesQuickEditor();
        const QScopeGuard restore(
            [editorFactory, wasQuick] { editorFactory->setUsesQuickEditor(wasQuick); });
        editorFactory->setUsesQuickEditor(true);
        const QScopeGuard closeAll([] { Core::EditorManager::closeAllEditors(false); });

        Core::IEditor * const editor = Core::EditorManager::openEditor(file);
        QVERIFY(editor);
        QVERIFY2(!TextEditor::TextEditorWidget::fromEditor(editor),
                 "the C++ file opened in a widget editor, so this tests nothing");
        auto * const document = qobject_cast<CppEditorDocument *>(editor->document());
        QVERIFY(document);
        document->recalculateSemanticInfo();
        QTRY_VERIFY2(document->isSemanticInfoValid(),
                     "the document never worked out what the file says");

        // The rename that spans all three uses of alpha.
        editor->gotoLine(3, 10);
        TextEditor::renameSymbolUnderCursorIn(editor);
        auto * const renaming = editor->findChild<CppLocalRenaming *>();
        QVERIFY2(renaming, "the Quick editor has no in-place rename to speak of");
        QTRY_VERIFY2(renaming->isActive(),
                     "renaming the symbol under the cursor started nothing");

        // What a completion does: rewrite the text where the caret is, without
        // a keystroke for the rename to see.
        QTextCursor edit = TextEditor::textCursorOf(editor);
        edit.setPosition(document->plainText().indexOf("alpha"));
        edit.setPosition(edit.position() + 5, QTextCursor::KeepAnchor);
        edit.insertText("beta");
        QCOMPARE(document->plainText().count("beta"), 1);

        // Applying is what tells the rename to catch the others up.
        QVERIFY2(TextEditor::encourageApplyIn(editor),
                 "nothing in this view took the applied edit");
        QTRY_COMPARE(document->plainText().count("beta"), 3);
        QVERIFY2(!document->plainText().contains("alpha"),
                 "a use of the old name was left behind");
    }

    // The clipboard while an in-place rename is running. A rename spans every
    // use of the name at once, so Select All takes the name rather than the
    // file and a paste replaces all of them rather than the one the caret is
    // in. CppEditorWidget took those three over for exactly that reason; a
    // view that is not one takes them through the same handler, and only the
    // fourth of the four - encourageApply() - was ever asked whether it
    // arrives.
    void testTheClipboardDuringAnInPlaceRenameSpansEveryUse()
    {
        Utils::TemporaryDirectory dir("cpp-rename-clipboard-in-any-view");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("rename.cpp");
        QVERIFY(file.writeFileContents(
            "int main()\n{\n    int alpha = 1;\n    return alpha + alpha;\n}\n"));

        const bool wasClangd = ClangdSettings::instance().useClangd();
        const QScopeGuard restoreClangd(
            [wasClangd] { ClangdSettings::setUseClangd(wasClangd); });
        ClangdSettings::setUseClangd(false);

        TextEditor::TextEditorFactory * const editorFactory
            = TextEditor::TextEditorFactory::preferredFactoryFor(file);
        QVERIFY(editorFactory);
        const bool wasQuick = editorFactory->usesQuickEditor();
        const QScopeGuard restore(
            [editorFactory, wasQuick] { editorFactory->setUsesQuickEditor(wasQuick); });
        editorFactory->setUsesQuickEditor(true);
        const QScopeGuard closeAll([] { Core::EditorManager::closeAllEditors(false); });

        Core::IEditor * const editor = Core::EditorManager::openEditor(file);
        QVERIFY(editor);
        QVERIFY2(!TextEditor::TextEditorWidget::fromEditor(editor),
                 "the C++ file opened in a widget editor, so this tests nothing");
        auto * const document = qobject_cast<CppEditorDocument *>(editor->document());
        QVERIFY(document);
        document->recalculateSemanticInfo();
        QTRY_VERIFY2(document->isSemanticInfoValid(),
                     "the document never worked out what the file says");

        // The commands are routed by the active context, so the view has to
        // have focus for a trigger to reach it at all.
        QWidget * const host = editor->widget();
        QVERIFY(host);
        host->resize(400, 300);
        host->show();
        const QScopeGuard hideIt([host] { host->hide(); });
        host->activateWindow();
        QApplication::setActiveWindow(host);
        host->setFocus(Qt::OtherFocusReason);
        QTRY_VERIFY2(QApplication::focusWidget(), "nothing took focus in this fixture");

        editor->gotoLine(3, 10);
        TextEditor::renameSymbolUnderCursorIn(editor);
        auto * const renaming = editor->findChild<CppLocalRenaming *>();
        QVERIFY2(renaming, "the Quick editor has no in-place rename to speak of");
        QTRY_VERIFY2(renaming->isActive(),
                     "renaming the symbol under the cursor started nothing");

        const auto trigger = [](const Utils::Id &id) {
            Core::Command * const command = Core::ActionManager::command(id);
            if (!command || !command->action()->isEnabled())
                return false;
            command->action()->trigger();
            return true;
        };

        // Select All takes the name being renamed, not the file.
        QVERIFY2(trigger(Core::Constants::SELECTALL),
                 "Select All does not reach the Qt Quick C++ editor");
        QCOMPARE(TextEditor::textCursorOf(editor).selectedText(), QString("alpha"));

        // And a paste over it reaches every use.
        QGuiApplication::clipboard()->setText("beta");
        QVERIFY2(trigger(Core::Constants::PASTE),
                 "Paste does not reach the Qt Quick C++ editor");
        QTRY_COMPARE(document->plainText().count("beta"), 3);
        QVERIFY2(!document->plainText().contains("alpha"),
                 "a use of the old name was left behind");

        // The rename is still running over the new name, and a cut empties
        // every use of it rather than the one the caret is in.
        QVERIFY2(renaming->isActive(), "the paste ended the rename");
        QVERIFY2(trigger(Core::Constants::SELECTALL),
                 "Select All does not reach the Qt Quick C++ editor");
        QCOMPARE(TextEditor::textCursorOf(editor).selectedText(), QString("beta"));
        QVERIFY2(trigger(Core::Constants::CUT),
                 "Cut does not reach the Qt Quick C++ editor");
        QTRY_COMPARE(document->plainText().count("beta"), 0);
    }

    // Completion. CppEditorWidget built the C++ assist interface itself, and
    // the document only answered quick fixes - so in the Qt Quick editor a C++
    // file completed with the words already in the file. Worse, configuring
    // the view cleared the provider CppEditorDocument installs when it learns
    // its mime type, so the C++ one was not even asked.
    void testCppCompletionIsOfferedInEitherView()
    {
        Utils::TemporaryDirectory dir("cpp-completion-in-any-view");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("complete.cpp");
        QVERIFY(file.writeFileContents("struct S { int member; };\n"
                                       "void f() { S s; s.\n}\n"));

        // The built-in model, so that the provider under test is CppEditor's
        // rather than a language client's.
        const bool wasClangd = ClangdSettings::instance().useClangd();
        const QScopeGuard restoreClangd(
            [wasClangd] { ClangdSettings::setUseClangd(wasClangd); });
        ClangdSettings::setUseClangd(false);

        TextEditor::TextEditorFactory * const factory
            = TextEditor::TextEditorFactory::preferredFactoryFor(file);
        QVERIFY(factory);
        const bool wasQuick = factory->usesQuickEditor();
        const QScopeGuard restore(
            [factory, wasQuick] { factory->setUsesQuickEditor(wasQuick); });
        const QScopeGuard closeAll([] { Core::EditorManager::closeAllEditors(false); });

        // Both views, because what this fixes is the two disagreeing.
        for (const bool quick : {false, true}) {
            factory->setUsesQuickEditor(quick);
            Core::IEditor * const editor = Core::EditorManager::openEditor(file);
            QVERIFY(editor);
            QCOMPARE(TextEditor::TextEditorWidget::fromEditor(editor) == nullptr, quick);
            auto * const document = qobject_cast<CppEditorDocument *>(editor->document());
            QVERIFY(document);
            document->recalculateSemanticInfo();
            QTRY_VERIFY(document->isSemanticInfoValid());

            // The C++ provider, not the words already in the file.
            QVERIFY2(qobject_cast<CppCompletionAssistProvider *>(
                         document->completionAssistProvider()),
                     "the language's completion provider was replaced by a generic one");

            // On the member access, which is where a C++ completion differs
            // from any other kind.
            QTextCursor cursor(document->document());
            cursor.setPosition(document->plainText().indexOf("s.") + 2);
            const std::unique_ptr<TextEditor::AssistInterface> interface
                = document->createAssistInterface(cursor, TextEditor::Completion,
                                                  TextEditor::ExplicitlyInvoked, editor);
            QVERIFY2(interface, "nothing answered the request for a completion interface");
            QVERIFY2(dynamic_cast<const Internal::CppCompletionAssistInterface *>(interface.get()),
                     "the completion got a plain interface, so it knows no C++");

            Core::EditorManager::closeEditors({editor}, false);
        }
    }

    // A URL in a string literal is a link, and CppEditor answered that inside
    // CppEditorWidget::findLinkAt() - so in the Qt Quick editor Ctrl+click on
    // one did nothing. It needs a project only because the same code path
    // also resolves qrc: paths through the project's resource files.
    void testAUrlInAStringLiteralIsFollowedWithoutAWidget()
    {
        Utils::TemporaryDirectory dir("cpp-follow-url-without-a-widget");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("urls.cpp");
        QVERIFY(file.writeFileContents(
            "const char *link = \"https://www.qt.io/\";\n"));

        CppEditor::Tests::ModelManagerTestHelper helper;
        ProjectExplorer::Project * const project
            = helper.createProject("follow-url", dir.path() / "follow-url.pro");
        auto root = std::make_unique<ProjectExplorer::ProjectNode>(dir.path());
        // The file has to be *in* the project, or ProjectTree has no current
        // project once its editor is the current one - which is exactly the
        // condition the branch under test needs.
        root->addNode(std::make_unique<ProjectExplorer::FileNode>(
            file, ProjectExplorer::FileType::Source));
        project->setRootProjectNode(std::move(root));

        const bool wasClangd = ClangdSettings::instance().useClangd();
        const QScopeGuard restoreClangd(
            [wasClangd] { ClangdSettings::setUseClangd(wasClangd); });
        ClangdSettings::setUseClangd(false);

        TextEditor::TextEditorFactory * const editorFactory
            = TextEditor::TextEditorFactory::preferredFactoryFor(file);
        QVERIFY2(editorFactory, "no editor factory claims a C++ file");
        const bool wasQuick = editorFactory->usesQuickEditor();
        const QScopeGuard restore(
            [editorFactory, wasQuick] { editorFactory->setUsesQuickEditor(wasQuick); });
        editorFactory->setUsesQuickEditor(true);

        const QScopeGuard closeAll([] { Core::EditorManager::closeAllEditors(false); });
        Core::IEditor * const editor = Core::EditorManager::openEditor(file);
        QVERIFY2(editor, "the editor manager opened nothing");
        QVERIFY2(!TextEditor::TextEditorWidget::fromEditor(editor),
                 "the C++ file opened in a widget editor, so this tests nothing");
        QVERIFY2(ProjectExplorer::ProjectTree::currentProject(),
                 "no project is current, so the branch under test is unreachable");

        auto * const document = qobject_cast<CppEditorDocument *>(editor->document());
        QVERIFY(document);
        document->recalculateSemanticInfo();
        QTRY_VERIFY2(document->isSemanticInfoValid(),
                     "the document never worked out what the file says");

        // On the literal, which is what carries the URL.
        QTextCursor cursor(document->document());
        cursor.setPosition(document->plainText().indexOf("https"));
        Utils::Link found;
        bool answered = false;
        const CursorInEditor data(cursor, file, nullptr, document);
        followCppSymbol(data, [&](const Utils::Link &link) {
            found = link;
            answered = true;
        }, true, false);
        QTRY_VERIFY2(answered, "following the literal answered nothing at all");
        QCOMPARE(found.targetFilePath.toUrlishString(), QString("https://www.qt.io/"));
        // And it underlines the literal itself, not some other span: what the
        // range covers is exactly the quoted token the URL was read from.
        QCOMPARE(document->plainText().mid(found.linkTextStart,
                                           found.linkTextEnd - found.linkTextStart),
                 QString("\"https://www.qt.io/\""));
    }

    // The parse a jump walks. clangd corrects the caret before asking - the
    // workaround for its enum and operator edge cases - and read that parse
    // off a CppEditorWidget, so in the Qt Quick editor there was none and
    // every position it corrects for was followed from where the reader had
    // not pointed.
    void testTheParseIsReachableWithoutAWidget()
    {
        Utils::TemporaryDirectory dir("cpp-parse-without-a-widget");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("parsed.cpp");
        QVERIFY(file.writeFileContents("enum E { v1, v2 };\n\nint main() { return v1; }\n"));

        TextEditor::TextEditorFactory * const editorFactory
            = TextEditor::TextEditorFactory::preferredFactoryFor(file);
        QVERIFY2(editorFactory, "no editor factory claims a C++ file");
        const bool wasQuick = editorFactory->usesQuickEditor();
        const QScopeGuard restore(
            [editorFactory, wasQuick] { editorFactory->setUsesQuickEditor(wasQuick); });
        editorFactory->setUsesQuickEditor(true);

        Core::IEditor * const editor = Core::EditorManager::openEditor(file);
        QVERIFY2(editor, "the editor manager opened nothing");
        const QScopeGuard closeIt(
            [editor] { Core::EditorManager::closeEditors({editor}, false); });
        QVERIFY2(!TextEditor::TextEditorWidget::fromEditor(editor),
                 "the C++ file opened in a widget editor, so this tests nothing");

        auto * const document = qobject_cast<CppEditorDocument *>(editor->document());
        QVERIFY(document);
        document->recalculateSemanticInfo();
        QTRY_VERIFY2(document->isSemanticInfoValid(),
                     "the document never worked out what the file says");

        const CPlusPlus::Document::Ptr parse = semanticDocumentOf(document);
        QVERIFY2(parse, "no view could say how this file parses");

        // And it is this file's parse, not merely some document: the enum the
        // fixture declares is in it.
        QCOMPARE(parse->filePath(), file);
        QVERIFY2(parse->globalSymbolCount() > 0,
                 "the parse knows no symbol from a file that declares two");
    }

    // Which view a question about the caret was asked from. Every C++ jump
    // carries a CursorInEditor, and the only view it names is a widget - so
    // clangd's fallback to the built-in model, which runs only while the
    // reader is still there to be sent somewhere, was skipped outright for a
    // file in the Qt Quick editor.
    void testTheAskingViewIsFoundWithoutAWidget()
    {
        Utils::TemporaryDirectory dir("cpp-asking-view-without-a-widget");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("origin.cpp");
        QVERIFY(file.writeFileContents("struct C {\n    void f();\n};\n"));

        TextEditor::TextEditorFactory * const editorFactory
            = TextEditor::TextEditorFactory::preferredFactoryFor(file);
        QVERIFY2(editorFactory, "no editor factory claims a C++ file");
        const bool wasQuick = editorFactory->usesQuickEditor();
        const QScopeGuard restore(
            [editorFactory, wasQuick] { editorFactory->setUsesQuickEditor(wasQuick); });
        editorFactory->setUsesQuickEditor(true);

        const QScopeGuard closeAll([] { Core::EditorManager::closeAllEditors(false); });

        Core::IEditor * const editor = Core::EditorManager::openEditor(file);
        QVERIFY2(editor, "the editor manager opened nothing");
        QVERIFY2(!TextEditor::TextEditorWidget::fromEditor(editor),
                 "the C++ file opened in a widget editor, so this tests nothing");
        auto * const document = qobject_cast<CppEditorDocument *>(editor->document());
        QVERIFY(document);

        const CursorInEditor data(TextEditor::textCursorOf(editor),
                                  document->filePath(),
                                  qobject_cast<CppEditorWidget *>(editor->widget()),
                                  document);
        QVERIFY2(!data.editorWidget(),
                 "the fixture named a widget, so finding one proves nothing");
        QCOMPARE(editorFor(data), editor);

        // And the half the guard is there for: with the reader gone to another
        // file there is nowhere to send them, so nothing is found rather than
        // whichever editor happens to be in front.
        const Utils::FilePath elsewhere = dir.filePath("elsewhere.cpp");
        QVERIFY(elsewhere.writeFileContents("int x;\n"));
        Core::IEditor * const other = Core::EditorManager::openEditor(elsewhere);
        QVERIFY2(other, "the editor manager opened nothing for the second file");
        QCOMPARE(Core::EditorManager::currentEditor(), other);
        QVERIFY2(!editorFor(data),
                 "an editor showing another file was offered as where to send the reader");
    }

    // Find Usages, which the viewport asks for through a relay rather than by
    // being asked itself. Nobody answered that relay for the built-in code
    // model, so Ctrl+Shift+U in the Quick editor did nothing.
    void testFindUsagesAnswersTheViewsRequest()
    {
        Utils::TemporaryDirectory dir("cpp-find-usages-without-a-widget");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("uses.cpp");
        QVERIFY(file.writeFileContents(
            "int main()\n{\n    int alpha = 1;\n    return alpha + alpha;\n}\n"));

        const bool wasClangd = ClangdSettings::instance().useClangd();
        const QScopeGuard restoreClangd(
            [wasClangd] { ClangdSettings::setUseClangd(wasClangd); });
        ClangdSettings::setUseClangd(false);

        TextEditor::TextEditorFactory * const editorFactory
            = TextEditor::TextEditorFactory::preferredFactoryFor(file);
        QVERIFY2(editorFactory, "no editor factory claims a C++ file");
        const bool wasQuick = editorFactory->usesQuickEditor();
        const QScopeGuard restore(
            [editorFactory, wasQuick] { editorFactory->setUsesQuickEditor(wasQuick); });
        editorFactory->setUsesQuickEditor(true);

        Core::IEditor * const editor = Core::EditorManager::openEditor(file);
        QVERIFY2(editor, "the editor manager opened nothing");
        const QScopeGuard closeIt(
            [editor] { Core::EditorManager::closeEditors({editor}, false); });
        QVERIFY2(!TextEditor::TextEditorWidget::fromEditor(editor),
                 "the C++ file opened in a widget editor, so this tests nothing");

        auto * const document = qobject_cast<CppEditorDocument *>(editor->document());
        QVERIFY(document);
        document->recalculateSemanticInfo();
        QTRY_VERIFY2(document->isSemanticInfoValid(),
                     "the document never worked out what the file says");

        TextEditor::SymbolRequests * const requests
            = TextEditor::symbolRequestsForEditor(editor);
        QVERIFY2(requests, "the Quick editor has no relay to ask through");

        // Starting a search is what pops the Search Results pane, so that is
        // the one thing a test can see without reaching into it.
        QSignalSpy searches(Core::SearchResultWindow::instance(),
                            &Core::IOutputPane::showPage);

        editor->gotoLine(3, 10); // on alpha
        requests->askForUsages(TextEditor::textCursorOf(editor));
        QTRY_VERIFY2(searches.count() > 0, "asking for usages started no search");

        // A search that starts and finds nothing passes everything above,
        // and that is what "Ctrl+Shift+U did nothing" looked like. What it
        // found is the command.
        Core::SearchResultWindow * const found = Core::SearchResultWindow::instance();
        QTRY_VERIFY2(found->canNext(), "the search that started found no usages");

        // And it found the right ones: every hit is the name the caret was
        // on, and there are as many distinct ones as the file has uses.
        QSet<int> landedOn;
        for (int i = 0; i < 3; ++i) {
            found->goToNext();
            Core::IEditor * const shown = Core::EditorManager::currentEditor();
            QVERIFY2(shown, "stepping to a usage opened no editor");
            // Stepping to a hit selects it, so the selection is the hit -
            // and reading the word under the caret instead would read what
            // follows the name, the caret being at the selection's end.
            const QTextCursor at = TextEditor::textCursorOf(shown);
            QCOMPARE(at.selectedText(), QString("alpha"));
            landedOn.insert(at.selectionStart());
        }
        QCOMPARE(landedOn.size(), 3);

        // And a caret on nothing asks for nothing, so what is asserted above
        // is the symbol rather than the request being unconditional.
        const int afterSymbol = searches.count();
        editor->gotoLine(2, 1);
        requests->askForUsages(TextEditor::textCursorOf(editor));
        QCOMPARE(searches.count(), afterSymbol);
    }

    // Rename asks the same relay, but reaches the model manager the long way
    // round: follow symbol first, so that renaming something declared in a
    // generated file can say so, and the rename runs from that answer.
    // The outline the tool bar row draws, the Ctrl+U walk and the
    // declaration/definition link are all things a view that is not a widget
    // gets from the editor rather than from a widget subclass. They sat below
    // a return meant for the searches alone.
    void testAQuickCppEditorGetsWhatTheWidgetSubclassOwns()
    {
        Utils::TemporaryDirectory dir("cpp-quick-editor-parts");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("main.cpp");
        QVERIFY(file.writeFileContents("int one() { return 1; }\nint two() { return 2; }\n"));

        TextEditor::TextEditorFactory * const editorFactory
            = TextEditor::TextEditorFactory::preferredFactoryFor(file);
        QVERIFY2(editorFactory, "no editor factory claims a C++ file");
        const bool wasQuick = editorFactory->usesQuickEditor();
        const QScopeGuard restore(
            [editorFactory, wasQuick] { editorFactory->setUsesQuickEditor(wasQuick); });
        editorFactory->setUsesQuickEditor(true);

        Core::IEditor * const editor = Core::EditorManager::openEditor(file);
        QVERIFY2(editor, "the editor manager opened nothing");
        const QScopeGuard closeIt(
            [editor] { Core::EditorManager::closeEditors({editor}, false); });
        QVERIFY2(!TextEditor::TextEditorWidget::fromEditor(editor),
                 "the C++ file opened in a widget editor, so this tests nothing");

        // All three sat below the return, and each is what a widget subclass
        // owns for the widget editor.
        QVERIFY2(editor->findChild<TextEditor::ToolBarOutline *>(),
                 "the editor has no outline for the toolbar row to draw");
        QVERIFY2(editor->findChild<TextEditor::SelectionExpander *>(),
                 "the editor has no selection expander, so Ctrl+U walks brackets");
    }

    // The census for the goal this work is for: everything CppEditorFactory
    // configures, asked of one editor at once, so that a regression in any of
    // them shows up here as well as in its own test.
    //
    // It is a written list, so it cannot notice a *new* factory setting going
    // unread - only testEveryQuickLanguageGetsWhatItsFactoryConfigures() is
    // driven by the factory itself, and only for the settings it knows to ask
    // about. Adding something to CppEditorFactory means adding it here too.
    void testACppEditorHasEverythingTheFactoryConfigures_data()
    {
        QTest::addColumn<bool>("quick");
        // The widget row is the documented way back, which nothing ran until
        // it had already gone stale in three places. Asked here so that it
        // cannot go stale again without an ordinary run saying so.
        QTest::newRow("widget") << false;
        QTest::newRow("quick") << true;
    }

    void testACppEditorHasEverythingTheFactoryConfigures()
    {
        QFETCH(bool, quick);

        // The commands this test ends by asserting are on. Sampled here so
        // that the assertions below say something: all five are off with no
        // C++ editor open, so finding them on afterwards is this view turning
        // them on rather than the process having them on anyway.
        const auto enabledNow = [](const Utils::Id &id) {
            Core::Command * const command = Core::ActionManager::command(id);
            return command && command->action()->isEnabled();
        };
        const QList<Utils::Id> commands{TextEditor::Constants::UN_COMMENT_SELECTION,
                                        TextEditor::Constants::COMPLETE_THIS,
                                        TextEditor::Constants::QUICKFIX_THIS,
                                        TextEditor::Constants::FIND_USAGES,
                                        TextEditor::Constants::RENAME_SYMBOL};
        for (const Utils::Id &id : commands) {
            QVERIFY2(!enabledNow(id),
                     qPrintable(id.toString() + " is already on with no C++ editor open, so "
                                                "finding it on below would mean nothing"));
        }

        Utils::TemporaryDirectory dir("cpp-quick-census");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("main.cpp");
        QVERIFY(file.writeFileContents("int one() { return 1; }\nint two() { return one(); }\n"));

        TextEditor::TextEditorFactory * const factory
            = TextEditor::TextEditorFactory::preferredFactoryFor(file);
        QVERIFY(factory);
        const bool wasQuick = factory->usesQuickEditor();
        const QScopeGuard restore(
            [factory, wasQuick] { factory->setUsesQuickEditor(wasQuick); });
        factory->setUsesQuickEditor(quick);

        Core::IEditor * const editor = Core::EditorManager::openEditor(file);
        QVERIFY(editor);
        const QScopeGuard closeIt(
            [editor] { Core::EditorManager::closeEditors({editor}, false); });
        QCOMPARE(TextEditor::TextEditorWidget::fromEditor(editor) == nullptr, quick);
        auto * const document = qobject_cast<TextEditor::TextDocument *>(editor->document());
        QVERIFY(document);

        // The language context. Every C++ command is registered against it,
        // so an editor that does not carry it is one none of them reach.
        QVERIFY2(editor->context().contains(
                     Utils::Id(ProjectExplorer::Constants::CXX_LANGUAGE_ID)),
                 "the editor is not in the C++ language context");

        // The menu a right click offers beside what every editor offers.
        // CppEditorWidget named it in contextMenuEvent(); a view that is not
        // one is told by the factory instead.
        QVERIFY2(factory->contextMenuId() == Utils::Id(CppEditor::Constants::M_CONTEXT),
                 "the factory names no C++ context menu for a view to open");

        // Completion is the C++ one, not the word completer every document
        // falls back to. TextEditorFactory installs that fallback when a
        // factory names none, so asking only whether *a* provider is there
        // passes with no C++ completion at all - measured, not reasoned:
        // control EA removed it and this test stayed green.
        QVERIFY2(qobject_cast<CppCompletionAssistProvider *>(
                     document->completionAssistProvider()),
                 "the document completes like plain text, not like C++");

        // Quick fixes, by identity rather than by existence: the document
        // falls back to the C++ provider when nothing else is set, so asking
        // whether it has one at all cannot fail while that fallback is there.
        QVERIFY2(document->quickFixAssistProvider() == &cppQuickFixAssistProvider(),
                 "the document does not offer C++ quick fixes");

        // Follow symbol.
        QVERIFY2(TextEditor::TextEditorFactory::linkFinderFor(document),
                 "no link finder reached the document");

        // Everything above is the *document*, which is the same object in
        // either view - the factory configures it before it decides what
        // draws it. So none of it can say whether the Quick view reaches any
        // of it. What can is whether the commands are enabled while this view
        // has focus, which is the active context and nothing else.
        //
        // A command is enabled by the active context, so the view has to
        // actually have focus - entry 97 is what a test that skips this
        // measures instead.
        QWidget * const host = editor->widget();
        QVERIFY(host);
        host->resize(400, 300);
        host->show();
        const QScopeGuard hideIt([host] { host->hide(); });
        host->activateWindow();
        QApplication::setActiveWindow(host);
        host->setFocus(Qt::OtherFocusReason);
        QTRY_VERIFY2(QApplication::focusWidget(), "nothing took focus in this fixture");

        // The optional-action mask, asked as what it does rather than as the
        // object that does it: a command C++'s mask turns on.
        // The optional-action mask, completion, quick fixes and refactoring,
        // asked of the view rather than of the document. Each has a provider
        // on the document either way; what the flip depends on is that the
        // command reaches *here*. Every one of them was off above.
        for (const Utils::Id &id : commands) {
            QTRY_VERIFY2(enabledNow(id),
                         qPrintable(id.toString() + " is off in the "
                                    + QLatin1String(quick ? "Qt Quick" : "widget")
                                    + " C++ editor"));
        }
    }

    void testRenameAnswersTheViewsRequest()
    {
        Utils::TemporaryDirectory dir("cpp-rename-without-a-widget");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("rename.cpp");
        QVERIFY(file.writeFileContents(
            "int main()\n{\n    int alpha = 1;\n    return alpha + alpha;\n}\n"));

        const bool wasClangd = ClangdSettings::instance().useClangd();
        const QScopeGuard restoreClangd(
            [wasClangd] { ClangdSettings::setUseClangd(wasClangd); });
        ClangdSettings::setUseClangd(false);

        TextEditor::TextEditorFactory * const editorFactory
            = TextEditor::TextEditorFactory::preferredFactoryFor(file);
        QVERIFY2(editorFactory, "no editor factory claims a C++ file");
        const bool wasQuick = editorFactory->usesQuickEditor();
        const QScopeGuard restore(
            [editorFactory, wasQuick] { editorFactory->setUsesQuickEditor(wasQuick); });
        editorFactory->setUsesQuickEditor(true);

        Core::IEditor * const editor = Core::EditorManager::openEditor(file);
        QVERIFY2(editor, "the editor manager opened nothing");
        const QScopeGuard closeIt(
            [editor] { Core::EditorManager::closeEditors({editor}, false); });
        QVERIFY2(!TextEditor::TextEditorWidget::fromEditor(editor),
                 "the C++ file opened in a widget editor, so this tests nothing");

        auto * const document = qobject_cast<CppEditorDocument *>(editor->document());
        QVERIFY(document);
        document->recalculateSemanticInfo();
        QTRY_VERIFY2(document->isSemanticInfoValid(),
                     "the document never worked out what the file says");

        TextEditor::SymbolRequests * const requests
            = TextEditor::symbolRequestsForEditor(editor);
        QVERIFY2(requests, "the Quick editor has no relay to ask through");

        QSignalSpy searches(Core::SearchResultWindow::instance(),
                            &Core::IOutputPane::showPage);

        editor->gotoLine(3, 10); // on alpha
        requests->askForRename(TextEditor::textCursorOf(editor));
        QTRY_VERIFY2(searches.count() > 0, "asking for a rename started no search");

        // A search that starts and finds nothing passes everything above,
        // and that is what "Ctrl+Shift+U did nothing" looked like. What it
        // found is the command.
        Core::SearchResultWindow * const found = Core::SearchResultWindow::instance();
        QTRY_VERIFY2(found->canNext(), "the search that started found no usages");

        // And it found the right ones: every hit is the name the caret was
        // on, and there are as many distinct ones as the file has uses.
        QSet<int> landedOn;
        for (int i = 0; i < 3; ++i) {
            found->goToNext();
            Core::IEditor * const shown = Core::EditorManager::currentEditor();
            QVERIFY2(shown, "stepping to a usage opened no editor");
            // Stepping to a hit selects it, so the selection is the hit -
            // and reading the word under the caret instead would read what
            // follows the name, the caret being at the selection's end.
            const QTextCursor at = TextEditor::textCursorOf(shown);
            QCOMPARE(at.selectedText(), QString("alpha"));
            landedOn.insert(at.selectionStart());
        }
        QCOMPARE(landedOn.size(), 3);
    }

    // The parse everything walks has to describe what the file says now. The
    // document recalculates it and keeps it; a widget keeps a copy of its own,
    // fed by a signal, so a recalculation that nobody signalled leaves that
    // copy behind - and it was the copy that was handed out.
    void testTheParseHandedOutIsTheDocumentsOwn()
    {
        Utils::TemporaryDirectory dir("cpp-parse-handed-out");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("p.cpp");
        QVERIFY(file.writeFileContents("int alpha = 1;\n"));

        const bool wasClangd = ClangdSettings::instance().useClangd();
        const QScopeGuard restoreClangd(
            [wasClangd] { ClangdSettings::setUseClangd(wasClangd); });
        ClangdSettings::setUseClangd(false);

        // The widget view on purpose: it is the one that keeps a copy.
        TextEditor::TextEditorFactory * const editorFactory
            = TextEditor::TextEditorFactory::preferredFactoryFor(file);
        QVERIFY2(editorFactory, "no editor factory claims a C++ file");
        const bool wasQuick = editorFactory->usesQuickEditor();
        const QScopeGuard restore(
            [editorFactory, wasQuick] { editorFactory->setUsesQuickEditor(wasQuick); });
        editorFactory->setUsesQuickEditor(false);

        Core::IEditor * const editor = Core::EditorManager::openEditor(file);
        QVERIFY2(editor, "the editor manager opened nothing");
        const QScopeGuard closeIt(
            [editor] { Core::EditorManager::closeEditors({editor}, false); });
        QVERIFY2(TextEditor::TextEditorWidget::fromEditor(editor),
                 "the file opened in a view that keeps no copy, so this tests nothing");

        auto * const document = qobject_cast<CppEditorDocument *>(editor->document());
        QVERIFY(document);
        QTRY_VERIFY2(document->isSemanticInfoValid(),
                     "the document never worked out what the file says");

        // Change the file and have the document parse it again. Nothing
        // signals that, which is exactly the case a copy cannot keep up with.
        QTextCursor cursor(document->document());
        cursor.movePosition(QTextCursor::End);
        cursor.insertText("int beta = 2;\n");
        document->recalculateSemanticInfo();
        QVERIFY(document->isSemanticInfoValid());

        QVERIFY2(semanticDocumentOf(document) == document->semanticInfo().doc,
                 "what was handed out is not the parse the document has");
    }
};

QObject *createSymbolJumpTest()
{
    return new SymbolJumpTest;
}

#endif // WITH_TESTS

void decorateCppDocument(TextEditor::TextDocument *document)
{
    document->resetSyntaxHighlighter([] { return new CppHighlighter(); });
    document->setIndenter(createCppQtStyleIndenter(document->document()));
}

} // Internal
} // CppEditor

#ifdef WITH_TESTS
#include "cpptoolsreuse.moc"
#endif
