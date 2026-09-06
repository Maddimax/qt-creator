// Copyright (C) 2024 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "../luaengine.h"
#include "../luatr.h"

#include "utils.h"

#include <texteditor/basehoverhandler.h>
#include <texteditor/fontsettings.h>
#include <texteditor/refactoroverlay.h>
#include <texteditor/textdocument.h>
#include <texteditor/textdocumentlayout.h>
#include <texteditor/texteditor.h>
#include <texteditor/suggestionhost.h>
#include <utils/layoutbuilder.h>
#include <utils/stringutils.h>
#include <utils/tooltip/tooltip.h>
#include <utils/utilsicons.h>
#include <QToolBar>

#include "sol/sol.hpp"

using namespace Utils;
using namespace Text;
using namespace TextEditor;
using namespace std::string_view_literals;

namespace {

template<typename Return, typename Argument>
Return get_or_throw(const Argument &arg, const char *key)
{
    const auto value = arg.template get<sol::optional<Return>>(key);
    if (!value) {
        throw sol::error(std::string("Failed to get value for key: ") + key);
    }
    return *value;
}

TextEditor::SuggestionHost *getSuggestionReadyHost(TextEditor::TextDocument *document)
{
    Core::IEditor * const textEditor = Core::EditorManager::currentEditor();
    if (!textEditor || textEditor->document() != document)
        return nullptr;

    // Everything a suggestion needs is what SuggestionHost asks for, and both
    // views answer it - so this no longer cares which one is showing the file.
    TextEditor::SuggestionHost * const host = suggestionHostForEditor(textEditor);
    if (!host || host->isReadOnly() || host->multiTextCursor().hasMultipleCursors())
        return nullptr;

    return host;
}

std::unique_ptr<EmbeddedWidgetInterface> addEmbeddedWidget(
    Core::IEditor *editor, QWidget *widget, std::variant<int, Position> cursorPosition)
{
    if (!widget)
        throw sol::error("No widget provided");

    if (!editor)
        throw sol::error("No editor provided");

    auto * const textDocument = qobject_cast<TextDocument *>(editor->document());
    if (!textDocument || !textDocument->document())
        throw sol::error("No text document set");

    const int pos = cursorPosition.index() == 0
                        ? std::get<int>(cursorPosition)
                        : std::get<Position>(cursorPosition)
                              .toPositionInDocument(textDocument->document());

    // Either view: the room comes from the document in one and a row spacer in
    // the other, and the widget is an overlay on whatever the editor's widget
    // is - a text edit, or the QQuickWidget the scene lives in.
    std::unique_ptr<EmbeddedWidgetInterface> embed
        = TextEditor::insertWidgetIn(editor, widget, pos);
    if (!embed)
        throw sol::error("this editor cannot show an embedded widget");
    return embed;
}

void clearRefactorMarkers(Core::IEditor *editor, const Utils::Id &id)
{
    QTC_ASSERT(editor, throw sol::error("TextEditor is not valid"));
    setRefactorMarkersIn(editor, id, {});
}

void setRefactorMarker(
    Core::IEditor *editor,
    const Utils::Icon &icon,
    int position,
    const Utils::Id &id,
    bool anchorLeft,
    sol::main_function callback)
{
    auto * const textDocument = qobject_cast<TextDocument *>(editor->document());
    QTC_ASSERT(textDocument, throw sol::error("TextEditor has no text document"));

    QTextCursor cursor = QTextCursor(textDocument->document());
    cursor.setPosition(position);

    // Move cursor to start of line
    if (anchorLeft)
        cursor.movePosition(QTextCursor::MoveOperation::StartOfBlock);

    TextEditor::RefactorMarker marker;
    marker.cursor = cursor;
    marker.icon = icon.icon();
    marker.callback = [callback](Core::IEditor *) {
        Result<> res = Lua::void_safe_call(callback);
        QTC_CHECK_RESULT(res);
    };
    marker.type = id;

    setRefactorMarkersIn(editor, id, {std::move(marker)});
}
} // namespace

namespace Lua::Internal {

// The editor, not a widget editor: a C++ file opens in the Qt Quick one, and
// a script that asked for the current editor used to be handed nil.
using TextEditorPtr = QPointer<Core::IEditor>;
using TextDocumentPtr = QPointer<TextDocument>;

class TextEditorRegistry : public QObject
{
    Q_OBJECT

public:
    static TextEditorRegistry *instance()
    {
        static TextEditorRegistry *instance = new TextEditorRegistry();
        return instance;
    }

    TextEditorRegistry()
    {
        connect(
            Core::EditorManager::instance(),
            &Core::EditorManager::currentEditorChanged,
            this,
            [this](Core::IEditor *editor) {
                if (!editor) {
                    emit currentEditorChanged(nullptr);
                    return;
                }

                if (m_currentTextEditor) {
                    m_currentTextEditor->disconnect(this);
                    if (QWidget * const view = m_currentTextEditor->widget())
                        view->disconnect(this);
                    m_currentTextEditor->document()->disconnect(this);
                    m_currentTextEditor = nullptr;
                }

                // Any view of a text document, which is what these bindings
                // are about - the file, not the kind of view.
                if (qobject_cast<TextDocument *>(editor->document()))
                    m_currentTextEditor = editor;

                if (m_currentTextEditor) {
                    if (!connectTextEditor(m_currentTextEditor))
                        m_currentTextEditor = nullptr;
                }

                emit currentEditorChanged(m_currentTextEditor);
            });
        connect(
            Core::EditorManager::instance(),
            &Core::EditorManager::editorCreated,
            this,
            [this](Core::IEditor *editor) {
                if (editor && qobject_cast<TextDocument *>(editor->document()))
                    emit editorCreated(editor);
            });
    }

    bool connectTextEditor(Core::IEditor *editor)
    {
        auto * const textDocument = qobject_cast<TextDocument *>(editor->document());
        if (!textDocument)
            return false;

        // The editor says the caret moved whichever view it has; what the
        // carets then are is asked of that view.
        connect(
            editor,
            &Core::IEditor::cursorPositionChanged,
            this,
            [editor, this]() {
                emit currentCursorChanged(editor, multiTextCursorOf(editor));
            });

        connect(
            textDocument,
            &TextDocument::contentsChangedWithPosition,
            this,
            [this, textDocument](int position, int charsRemoved, int charsAdded) {
                emit documentContentsChanged(textDocument, position, charsRemoved, charsAdded);
            });

        return true;
    }

signals:
    void currentEditorChanged(Core::IEditor *editor);
    void editorCreated(Core::IEditor *editor);
    void documentContentsChanged(
        TextDocument *document, int position, int charsRemoved, int charsAdded);

    void currentCursorChanged(Core::IEditor *editor, MultiTextCursor cursor);

protected:
    TextEditorPtr m_currentTextEditor = nullptr;
};

void setupTextEditorModule()
{
    TextEditorRegistry::instance();

    registerProvider("TextEditor", [](sol::state_view lua) -> sol::object {
        const ScriptPluginSpec *pluginSpec = lua.get<ScriptPluginSpec *>("PluginSpec"sv);
        QObject *guard = pluginSpec->connectionGuard.get();

        sol::table result = lua.create_table();

        result["currentEditor"] = []() -> TextEditorPtr {
            Core::IEditor * const editor = Core::EditorManager::currentEditor();
            if (editor && qobject_cast<TextDocument *>(editor->document()))
                return editor;
            return nullptr;
        };

        result["openedEditors"] = [lua]() mutable -> sol::table {
            sol::table result = lua.create_table();
            for (Core::IEditor * const editor : Core::DocumentModel::editorsForOpenedDocuments()) {
                if (!qobject_cast<TextDocument *>(editor->document()))
                    continue;
                result.add(TextEditorPtr(editor));
            }
            return result;
        };

        result.new_usertype<MultiTextCursor>(
            "MultiTextCursor",
            sol::no_constructor,
            "mainCursor",
            &MultiTextCursor::mainCursor,
            "setMainCursor",
            [](MultiTextCursor *self, QTextCursor *cursor) { self->replaceMainCursor(*cursor); },
            "cursors",
            [](MultiTextCursor *self) { return sol::as_table(self->cursors()); },
            "setCursors",
            [](MultiTextCursor *self, const sol::table &cursors) {
                QList<QTextCursor> textCursors;
                for (const auto &[k, cursor] : cursors) {
                    if (QTC_GUARD(cursor.is<QTextCursor>()))
                        textCursors.append(cursor.as<QTextCursor>());
                }
                self->setCursors(textCursors);
            },
            "insertText",
            [](MultiTextCursor *self, const QString &text) { self->insertText(text); });

        result.new_usertype<Position>(
            "Position",
            sol::no_constructor,
            "line",
            sol::property(
                [](const Position &pos) { return pos.line; },
                [](Position &pos, int line) { pos.line = line; }),
            "column",
            sol::property(
                [](const Position &pos) { return pos.column; },
                [](Position &pos, int column) { pos.column = column; }),
            "toPositionInDocument",
            sol::overload(
                &Position::toPositionInDocument,
                [](const Position &pos, TextDocument *doc) {
                    return pos.toPositionInDocument(doc->document());
                }),
            "toTextCursor",
            sol::overload(&Position::toTextCursor, [](const Position &pos, TextDocument *doc) {
                return pos.toTextCursor(doc->document());
            }));

        // In range can't use begin/end as "end" is a reserved word for LUA scripts
        result.new_usertype<Range>(
            "Range",
            sol::no_constructor,
            "from",
            sol::property(
                [](const Range &range) { return range.begin; },
                [](Range &range, const Position &begin) { range.begin = begin; }),
            "to",
            sol::property(
                [](const Range &range) { return range.end; },
                [](Range &range, const Position &end) { range.end = end; }),
            "toTextCursor",
            sol::overload(&Range::toTextCursor, [](const Range &range, TextDocument *doc) {
                return range.toTextCursor(doc->document());
            }));

        auto textCursorType = result.new_usertype<QTextCursor>(
            "TextCursor",
            sol::no_constructor,
            "create",
            sol::overload(
                []() { return QTextCursor(); },
                [](QTextDocument *doc) { return QTextCursor(doc); },
                [](const QTextCursor &other) { return QTextCursor(other); },
                [](TextDocument *doc) { return QTextCursor(doc->document()); }),
            "position",
            &QTextCursor::position,
            "blockNumber",
            &QTextCursor::blockNumber,
            "columnNumber",
            &QTextCursor::columnNumber,
            "hasSelection",
            &QTextCursor::hasSelection,
            "selectedText",
            [](QTextCursor *cursor) {
                return cursor->selectedText().replace(QChar::ParagraphSeparator, '\n');
            },
            "selectionRange",
            [](const QTextCursor &textCursor) -> Range {
                Range ret;
                if (!textCursor.hasSelection())
                    throw sol::error("Cursor has no selection");

                int startPos = textCursor.selectionStart();
                int endPos = textCursor.selectionEnd();

                QTextDocument *doc = textCursor.document();
                if (!doc)
                    throw sol::error("Cursor has no document");

                QTextBlock startBlock = doc->findBlock(startPos);
                QTextBlock endBlock = doc->findBlock(endPos);

                ret.begin.line = startBlock.blockNumber();
                ret.begin.column = startPos - startBlock.position() - 1;

                ret.end.line = endBlock.blockNumber();
                ret.end.column = endPos - endBlock.position() - 1;
                return ret;
            },
            "insertText",
            [](QTextCursor *textCursor, const QString &text) { textCursor->insertText(text); },
            "movePosition",
            sol::overload(
                [](QTextCursor *cursor, QTextCursor::MoveOperation op) { cursor->movePosition(op); },
                [](QTextCursor *cursor, QTextCursor::MoveOperation op, QTextCursor::MoveMode mode) {
                    cursor->movePosition(op, mode);
                },
                [](QTextCursor *cursor,
                   QTextCursor::MoveOperation op,
                   QTextCursor::MoveMode mode,
                   int n) { cursor->movePosition(op, mode, n); }),
            "setPosition",
            sol::overload(&QTextCursor::setPosition, [](QTextCursor *cursor, int pos) {
                cursor->setPosition(pos);
            }));

        textCursorType["MoveMode"] = lua.create_table_with(
            "MoveAnchor", QTextCursor::MoveAnchor,
            "KeepAnchor", QTextCursor::KeepAnchor
        );

        textCursorType["MoveOperation"] = lua.create_table_with(
            "NoMove",            QTextCursor::NoMove,

            "Start",            QTextCursor::Start,
            "Up",               QTextCursor::Up,
            "StartOfLine",      QTextCursor::StartOfLine,
            "StartOfBlock",     QTextCursor::StartOfBlock,
            "StartOfWord",      QTextCursor::StartOfWord,
            "PreviousBlock",    QTextCursor::PreviousBlock,
            "PreviousCharacter",QTextCursor::PreviousCharacter,
            "PreviousWord",     QTextCursor::PreviousWord,
            "Left",             QTextCursor::Left,
            "WordLeft",         QTextCursor::WordLeft,

            "End",              QTextCursor::End,
            "Down",             QTextCursor::Down,
            "EndOfLine",        QTextCursor::EndOfLine,
            "EndOfWord",        QTextCursor::EndOfWord,
            "EndOfBlock",       QTextCursor::EndOfBlock,
            "NextBlock",        QTextCursor::NextBlock,
            "NextCharacter",    QTextCursor::NextCharacter,
            "NextWord",         QTextCursor::NextWord,
            "Right",            QTextCursor::Right,
            "WordRight",        QTextCursor::WordRight,

            "NextCell",         QTextCursor::NextCell,
            "PreviousCell",     QTextCursor::PreviousCell,
            "NextRow",          QTextCursor::NextRow,
            "PreviousRow",      QTextCursor::PreviousRow
        );

        using LayoutOrWidget = std::variant<Layouting::Layout *, Layouting::Widget *, QWidget *>;

        static auto toWidget = [](LayoutOrWidget &arg) {
            return std::visit(
                [](auto &&arg) -> QWidget * {
                    using T = std::decay_t<decltype(arg)>;
                    if constexpr (std::is_same_v<T, Layouting::Widget *>)
                        return arg->emerge();
                    else if constexpr (std::is_same_v<T, QWidget *>)
                        return arg;
                    else if constexpr (std::is_same_v<T, Layouting::Layout *>)
                        return arg->emerge();
                    else
                        return nullptr;
                },
                arg);
        };

        result.new_usertype<EmbeddedWidgetInterface>(
            "EmbeddedWidgetInterface",
            sol::no_constructor,
            "resize",
            &EmbeddedWidgetInterface::resize,
            "close",
            &EmbeddedWidgetInterface::close,
            "onShouldClose",
            [guard](EmbeddedWidgetInterface *widget, sol::main_function func) {
                QObject::connect(widget, &EmbeddedWidgetInterface::shouldClose, guard, [func]() {
                    Result<> res = void_safe_call(func);
                    QTC_CHECK_RESULT(res);
                });
            });

        std::shared_ptr<QMap<TextEditorPtr, QSet<Utils::Id>>> activeMarkers
            = std::make_shared<QMap<TextEditorPtr, QSet<Utils::Id>>>();

        QObject::connect(guard, &QObject::destroyed, [activeMarkers] {
            for (const auto &[k, v] : activeMarkers->asKeyValueRange()) {
                if (k) {
                    for (const auto &id : std::as_const(v))
                        setRefactorMarkersIn(k, id, {});
                }
            }
        });

        result.new_usertype<Core::IEditor>(
            "TextEditor",
            sol::no_constructor,
            "document",
            [](const TextEditorPtr &textEditor) -> TextDocumentPtr {
                QTC_ASSERT(textEditor, throw sol::error("TextEditor is not valid"));
                return qobject_cast<TextDocument *>(textEditor->document());
            },
            "addEmbeddedWidget",
            [](const TextEditorPtr &textEditor,
               LayoutOrWidget widget,
               std::variant<int, Position> position) {
                QTC_ASSERT(textEditor, throw sol::error("TextEditor is not valid"));
                return addEmbeddedWidget(textEditor, toWidget(widget), position);
            },
            "insertExtraToolBarWidget",
            [](const TextEditorPtr &textEditor, TextEditorWidget::Side side, LayoutOrWidget widget) {
                QTC_ASSERT(textEditor, throw sol::error("TextEditor is not valid"));
                // A QWidget in the toolbar has nowhere to go in the Qt Quick
                // editor, whose toolbar is built from actions. Refused rather
                // than ignored, so a script learns why nothing appeared.
                TextEditorWidget * const view = TextEditorWidget::fromEditor(textEditor);
                if (!view)
                    throw sol::error("insertExtraToolBarWidget needs a widget editor");
                view->insertExtraToolBarWidget(side, toWidget(widget));
            },
            "insertExtraToolBarAction",
            [](const TextEditorPtr &textEditor, TextEditorWidget::Side side, QAction *action) {
                QTC_ASSERT(textEditor, throw sol::error("TextEditor is not valid"));
                TextEditor::insertExtraToolBarActionIn(textEditor, side, action);
            },
            "setRefactorMarker",
            [pluginSpec, activeMarkers](
                const TextEditorPtr &textEditor,
                const IconFilePathOrString &icon,
                int position,
                const QString &id,
                bool anchorLeft,
                sol::main_function callback) {
                QTC_ASSERT(textEditor, throw sol::error("TextEditor is not valid"));
                QTC_ASSERT(!id.isEmpty(), throw sol::error("Id is empty"));
                QTC_ASSERT(!icon.valueless_by_exception(), throw sol::error("Icon is invalid"));

                Id finalId = Utils::Id::fromString(QString(pluginSpec->id + "." + id));
                (*activeMarkers)[textEditor].insert(finalId);

                setRefactorMarker(textEditor, *toIcon(icon), position, finalId, anchorLeft, callback);
            },
            "clearRefactorMarkers",
            [pluginSpec, activeMarkers](const TextEditorPtr &textEditor, const QString &id) {
                QTC_ASSERT(textEditor, throw sol::error("TextEditor is not valid"));
                QTC_ASSERT(!id.isEmpty(), throw sol::error("Id is empty"));

                Id finalId = Utils::Id::fromString(QString(pluginSpec->id + "." + id));
                (*activeMarkers)[textEditor].remove(finalId);

                clearRefactorMarkers(textEditor, finalId);
            },
            "cursor",
            [](const TextEditorPtr &textEditor) {
                QTC_ASSERT(textEditor, throw sol::error("TextEditor is not valid"));
                return multiTextCursorOf(textEditor);
            },
            "setCursor",
            [](const TextEditorPtr &textEditor, MultiTextCursor *cursor) {
                QTC_ASSERT(textEditor, throw sol::error("TextEditor is not valid"));
                setMultiTextCursorOf(textEditor, *cursor);
            },
            "hasLockedSuggestion",
            [](const TextEditorPtr &textEditor) {
                QTC_ASSERT(textEditor, throw sol::error("TextEditor is not valid"));
                TextEditor::SuggestionHost * const host
                    = suggestionHostForEditor(textEditor);
                return host && host->suggestionVisible();
            },
            "insertText",
            [](TextEditorPtr editor, const QString &text) {
                MultiTextCursor cursor = multiTextCursorOf(editor);
                cursor.insertText(text);
            },
            "hasFocus",
            [](const TextEditorPtr &textEditor) {
                QTC_ASSERT(textEditor, throw sol::error("TextEditor is not valid"));
                return hasFocusIn(textEditor);
            },
            "setFocus",
            [](const TextEditorPtr &textEditor) {
                QTC_ASSERT(textEditor, throw sol::error("TextEditor is not valid"));
                setFocusIn(textEditor);
            },
            "firstVisibleBlockNumber",
            [](const TextEditorPtr &textEditor) -> int {
                QTC_ASSERT(textEditor, throw sol::error("TextEditor is not valid"));
                return visibleLinesIn(textEditor).first;
            },
            "lastVisibleBlockNumber",
            [](const TextEditorPtr &textEditor) -> int {
                QTC_ASSERT(textEditor, throw sol::error("TextEditor is not valid"));
                return visibleLinesIn(textEditor).second;
            });

        result["Side"] = lua.create_table_with(
                "Left", TextEditorWidget::Left,
                "Right", TextEditorWidget::Right
            );

        result.new_usertype<TextSuggestion::Data>(
            "Suggestion",
            "create",
            [](const sol::table &suggestion) -> TextEditor::TextSuggestion::Data {
                const auto one_based = [](int zero_based) { return zero_based + 1; };
                const auto position = get_or_throw<sol::table>(suggestion, "position");
                const auto position_line = get_or_throw<int>(position, "line");
                const auto position_column = get_or_throw<int>(position, "column");

                const auto range = get_or_throw<sol::table>(suggestion, "range");

                const auto from = get_or_throw<sol::table>(range, "from");
                const auto from_line = get_or_throw<int>(from, "line");
                const auto from_column = get_or_throw<int>(from, "column");

                const auto to = get_or_throw<sol::table>(range, "to");
                const auto to_line = get_or_throw<int>(to, "line");
                const auto to_column = get_or_throw<int>(to, "column");

                const auto text = get_or_throw<QString>(suggestion, "text");

                const Position cursor_pos = {one_based(position_line), position_column};
                const Position from_pos = {one_based(from_line), from_column};
                const Position to_pos = {one_based(to_line), to_column};

                return {Range{from_pos, to_pos}, cursor_pos, text};
            });

        result.new_usertype<TextDocument>(
            "TextDocument",
            sol::no_constructor,
            "file",
            [](const TextDocumentPtr &document) {
                QTC_ASSERT(document, throw sol::error("TextDocument is not valid"));
                return document->filePath();
            },
            "font",
            [](const TextDocumentPtr &document) {
                QTC_ASSERT(document, throw sol::error("TextDocument is not valid"));
                return document->fontSettings().font();
            },
            "blockAndColumn",
            [](const TextDocumentPtr &document, int position) -> std::optional<std::pair<int, int>> {
                QTC_ASSERT(document, throw sol::error("TextDocument is not valid"));
                QTextBlock block = document->document()->findBlock(position);
                if (!block.isValid())
                    return std::nullopt;

                int column = position - block.position();

                return std::make_pair(block.blockNumber() + 1, column + 1);
            },
            "blockCount",
            [](const TextDocumentPtr &document) {
                QTC_ASSERT(document, throw sol::error("TextDocument is not valid"));
                return document->document()->blockCount();
            },
            "setSuggestions",
            [](const TextDocumentPtr &document, QList<TextSuggestion::Data> suggestions) {
                QTC_ASSERT(document, throw sol::error("TextDocument is not valid"));

                if (suggestions.isEmpty())
                    return;

                auto host = getSuggestionReadyHost(document);
                if (!host)
                    return;

                host->insertSuggestion(
                    std::make_unique<CyclicSuggestion>(suggestions, document->document()));
            });

        return result;
    });

    registerHook("editors.text.currentChanged", [](sol::main_function func, QObject *guard) {
        QObject::connect(
            TextEditorRegistry::instance(),
            &TextEditorRegistry::currentEditorChanged,
            guard,
            [func](Core::IEditor *editor) {
                Result<> res = void_safe_call(func, editor);
                QTC_CHECK_RESULT(res);
            });
    });

    registerHook("editors.text.editorCreated", [](sol::main_function func, QObject *guard) {
        QObject::connect(
            TextEditorRegistry::instance(),
            &TextEditorRegistry::editorCreated,
            guard,
            [func](TextEditorPtr editor) {
                Result<> res = void_safe_call(func, editor);
                QTC_CHECK_RESULT(res);
            });
    });

    registerHook("editors.text.contentsChanged", [](sol::main_function func, QObject *guard) {
        QObject::connect(
            TextEditorRegistry::instance(),
            &TextEditorRegistry::documentContentsChanged,
            guard,
            [func](TextDocument *document, int position, int charsRemoved, int charsAdded) {
                Result<> res
                    = void_safe_call(func, document, position, charsRemoved, charsAdded);
                QTC_CHECK_RESULT(res);
            });
    });

    registerHook("editors.text.cursorChanged", [](sol::main_function func, QObject *guard) {
        QObject::connect(
            TextEditorRegistry::instance(),
            &TextEditorRegistry::currentCursorChanged,
            guard,
            [func](Core::IEditor *editor, const MultiTextCursor &cursor) {
                Result<> res = void_safe_call(func, editor, cursor);
                QTC_CHECK_RESULT(res);
            });
    });
}

} // namespace Lua::Internal

#include "texteditor.moc"
