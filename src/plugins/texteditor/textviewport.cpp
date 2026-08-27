// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "textviewport.h"

#include "behaviorsettings.h"
#include "autocompleter.h"
#include "codesource.h"
#include "completionsettings.h"
#include "displaysettings.h"
#include "fontsettings.h"
#include "tabsettings.h"
#include "syntaxhighlighter.h"
#include "texteditorconstants.h"
#include "texteditortr.h"
#include "textdocument.h"
#include "textdocumentlayout.h"
#include "textmark.h"
#include "typingsettings.h"

#include <coreplugin/idocument.h>

#include <qtcquick/qtciconprovider.h>

#include <utils/theme/theme.h>
#include <utils/utilsicons.h>

#include <QFontMetricsF>
#include <QQuickWindow>
#include <QSGRectangleNode>
#include <QSGTextNode>
#include <utils/multitextcursor.h>
#include <utils/plaintextedit/plaintextedit.h>

#include <QClipboard>
#include <QGuiApplication>
#include <QInputMethod>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextLayout>

namespace TextEditor {

// QTextCursor::selectedText() separates paragraphs with U+2029, which is right
// inside a document and wrong on a clipboard: pasted into anything else it is a
// stray character where a line break should be. Converted where the text leaves
// the document, so that nothing downstream has to know.
static QString selectedPlainText(const QTextCursor &cursor)
{
    return cursor.selectedText().replace(QChar::ParagraphSeparator, '\n');
}

TextViewport::Line::Line() = default;
TextViewport::Line::~Line() = default;
TextViewport::Line::Line(Line &&other) noexcept = default;
TextViewport::Line &TextViewport::Line::operator=(Line &&other) noexcept = default;

TextViewport::TextViewport(QQuickItem *parent)
    : QQuickItem(parent)
{
    setFlag(ItemHasContents);
    setFlag(ItemIsFocusScope);
    setFlag(ItemAcceptsInputMethod);
    setClip(true);

    m_autoCompleter = std::make_unique<AutoCompleter>();
    connect(&globalCompletionSettings(), &Utils::AspectContainer::changed, this, [this] {
        applyCompletionSettings();
    });
    applyCompletionSettings();

    // Preferences are *pushed* into a document rather than read from one: a
    // TextDocument on its own never hears about them, which is why the widget
    // editor does exactly this for the document it shows. Doing it here covers
    // the editor and every preview alike.
    connect(&globalFontSettings(), &FontSettings::changed, this, [this] {
        applyGlobalFontSettings();
    });
    // Turning bracket matching off has to take effect on what is already open,
    // rather than on whatever is opened next.
    connect(&displaySettings(), &Utils::AspectContainer::changed, this, [this] {
        emit fileFormatChanged();
        polish();
        update();
    });
}

void TextViewport::applyCompletionSettings()
{
    const CompletionSettings &settings = globalCompletionSettings();
    m_autoCompleter->setAutoInsertBracketsEnabled(settings.autoInsertBrackets());
    m_autoCompleter->setSurroundWithBracketsEnabled(settings.surroundingAutoBrackets());
    m_autoCompleter->setAutoInsertQuotesEnabled(settings.autoInsertQuotes());
    m_autoCompleter->setSurroundWithQuotesEnabled(settings.surroundingAutoQuotes());
    m_autoCompleter->setOverwriteClosingCharsEnabled(settings.overwriteClosingChars());
}

void TextViewport::applyGlobalFontSettings()
{
    if (TextDocument * const doc = m_document ? m_document->textDocument() : nullptr)
        doc->setFontSettings(globalFontSettings().data());
}

TextViewport::~TextViewport() = default;

CodeSource *TextViewport::document() const
{
    return m_document;
}

void TextViewport::setDocument(CodeSource *document)
{
    if (m_document == document)
        return;

    if (m_document)
        m_document->disconnect(this);

    m_document = document;

    if (m_document) {
        // A source hands out a different document when it reopens, and says so.
        connect(m_document, &CodeSource::textDocumentChanged,
                this, &TextViewport::documentChangedInternal);
    }

    documentChangedInternal();
    emit documentChanged();
}

qreal TextViewport::scrollY() const
{
    return m_scrollY;
}

void TextViewport::setScrollY(qreal scrollY)
{
    // Held inside the document: scrolling past the end shows nothing, and a
    // negative offset shows nothing twice.
    const qreal clamped = qBound(0.0, scrollY, qMax(0.0, m_contentHeight - height()));
    if (qFuzzyCompare(m_scrollY + 1, clamped + 1))
        return;
    m_scrollY = clamped;
    polish();
    emit scrollYChanged();
}

qreal TextViewport::scrollX() const
{
    return m_scrollX;
}

void TextViewport::setScrollX(qreal scrollX)
{
    const qreal clamped = qMax(0.0, scrollX);
    if (qFuzzyCompare(m_scrollX + 1, clamped + 1))
        return;
    m_scrollX = clamped;
    polish();
    emit scrollXChanged();
}

qreal TextViewport::contentHeight() const
{
    return m_contentHeight;
}

qreal TextViewport::lineHeight() const
{
    return m_lineHeight;
}

QColor TextViewport::backgroundColor() const
{
    return m_background;
}

int TextViewport::firstVisibleLine() const
{
    return m_firstVisibleLine;
}

QColor TextViewport::currentLineColor() const
{
    return m_currentLine;
}

QFont TextViewport::font() const
{
    return m_font;
}

int TextViewport::lineCount() const
{
    return m_lineCount;
}

// The two fold markers. Utils::Icon::icon() masks and tints on every call and
// these are wanted per foldable line per relayout, so they are made once -
// which is also why a theme change needs the restart Creator already asks for.
static QString foldMarkerUrl(bool folded)
{
    static const QString expand = QtcQuick::iconUrl(Utils::Icons::EXPAND.icon());
    static const QString collapse = QtcQuick::iconUrl(Utils::Icons::COLLAPSE.icon());
    return folded ? expand : collapse;
}

int TextViewport::visibleLineCount() const
{
    return int(m_lines.size());
}

int TextViewport::selectionStart() const
{
    return m_selectionStart;
}

void TextViewport::setSelectionStart(int position)
{
    if (m_selectionStart == position)
        return;
    m_selectionStart = position;
    polish();
    emit selectionChanged();
}

int TextViewport::selectionEnd() const
{
    return m_selectionEnd;
}

void TextViewport::setSelectionEnd(int position)
{
    if (m_selectionEnd == position)
        return;
    m_selectionEnd = position;
    polish();
    emit selectionChanged();
}

QVariantList TextViewport::visibleLines() const
{
    QVariantList lines;
    lines.reserve(int(m_lines.size()));
    for (int i = 0; i < int(m_lines.size()); ++i)
        lines.append(visibleLine(i));
    return lines;
}

QVariantMap TextViewport::visibleLine(int index) const
{
    if (index < 0 || index >= int(m_lines.size()))
        return {};

    const Line &line = m_lines.at(index);
    QVariantList formats;
    const QList<QTextLayout::FormatRange> ranges = line.layout->formats();
    for (const QTextLayout::FormatRange &range : ranges) {
        formats.append(QVariantMap{{"start", range.start},
                                   {"length", range.length},
                                   // Syntax highlighting is mostly a foreground
                                   // colour, so a test that reads only the
                                   // background cannot tell coloured text from
                                   // text drawn in one colour.
                                   {"foreground", range.format.foreground().color()},
                                   {"background", range.format.background().color()}});
    }
    return QVariantMap{{"text", line.layout->text()},
                       {"formats", formats},
                       // What the gutter numbers this line, counting folded
                       // lines that are not on screen.
                       {"lineNumber", line.lineNumber},
                       // What the gutter draws beside this line, if anything.
                       {"markIcon", line.markIcon},
                       // Whether a fold marker belongs beside this line, and
                       // which way round it points.
                       {"foldable", line.foldable},
                       {"folded", line.folded},
                       {"foldIcon", line.foldIcon},
                       {"foldReplacement", line.foldReplacement},
                       {"annotation", line.annotation},
                       {"newlineTail", line.newlineTail},
                       // What is being composed on this line, if anything. Not
                       // part of "text": it is not in the document yet, which
                       // is the whole distinction.
                       {"preedit", line.layout->preeditAreaText()},
                       {"width", line.layout->lineAt(0).naturalTextWidth()}};
}

TextViewport::Located TextViewport::locate(int position) const
{
    for (int i = 0; i < int(m_lines.size()); ++i) {
        const Line &line = m_lines.at(i);
        // blockLength counts the newline, and the caret is allowed to sit on
        // it: that is the position at the end of the line.
        if (position >= line.blockPosition && position < line.blockPosition + line.blockLength)
            return {i, position - line.blockPosition};
    }
    return {};
}

QRectF TextViewport::rectangleAt(int position) const
{
    const Located found = locate(position);
    if (found.index < 0)
        return {};

    const Line &line = m_lines.at(found.index);
    const QTextLine textLine = line.layout->lineAt(0);
    if (!textLine.isValid())
        return {};

    int offset = found.offsetInLine;
    const qreal x = textLine.cursorToX(&offset);
    return QRectF(line.at.x() + x, line.at.y(), 1, m_lineHeight);
}

bool TextViewport::isReadOnly() const
{
    return m_readOnly;
}

void TextViewport::setReadOnly(bool readOnly)
{
    if (m_readOnly == readOnly)
        return;
    m_readOnly = readOnly;
    // ImEnabled is answered from this, and the platform caches the answer until
    // it is told to ask again.
    if (QGuiApplication::inputMethod())
        QGuiApplication::inputMethod()->update(Qt::ImEnabled);
    emit readOnlyChanged();
}

QTextCursor TextViewport::textCursor() const
{
    TextDocument * const doc = m_document ? m_document->textDocument() : nullptr;
    QTextDocument * const text = doc ? doc->document() : nullptr;
    if (!text)
        return {};

    QTextCursor cursor(text);
    // Move through what is on screen, not through what is in the file: without
    // this, Down from a folded line steps into the hidden text and the caret
    // is nowhere.
    cursor.setVisualNavigation(true);
    // The anchor is where the selection was started from, which is not always
    // where the caret is: a selection dragged upwards has its anchor after its
    // position, and collapsing it has to keep that straight.
    const bool selected = m_selectionStart >= 0 && m_selectionEnd >= 0
                          && m_selectionStart != m_selectionEnd;
    cursor.setPosition(selected ? m_selectionStart : m_cursorPosition);
    if (selected)
        cursor.setPosition(m_selectionEnd, QTextCursor::KeepAnchor);
    return cursor;
}

void TextViewport::setTextCursor(const QTextCursor &cursor)
{
    setCursorPosition(cursor.position());
    if (cursor.hasSelection()) {
        setSelectionStart(cursor.anchor());
        setSelectionEnd(cursor.position());
    } else {
        setSelectionStart(-1);
        setSelectionEnd(-1);
    }
    ensureCursorVisible();
    polish();
}

void TextViewport::ensureCursorVisible()
{
    if (m_lineHeight <= 0)
        return;

    TextDocument * const doc = m_document ? m_document->textDocument() : nullptr;
    QTextDocument * const text = doc ? doc->document() : nullptr;
    if (!text)
        return;

    QTextCursor cursor(text);
    cursor.setPosition(qBound(0, m_cursorPosition, text->characterCount() - 1));
    // Where the line sits on screen, not where it sits in the file: a folded
    // block above it takes up no room, so the two part company as soon as
    // anything is folded. firstLineNumber() is what the layout keeps for this.
    const qreal top = cursor.block().firstLineNumber() * m_lineHeight;
    if (top < m_scrollY)
        setScrollY(top);
    else if (top + m_lineHeight > m_scrollY + height())
        setScrollY(top + m_lineHeight - height());
}

static Utils::PlainTextDocumentLayout *layoutOf(const QTextDocument *text)
{
    return text ? qobject_cast<Utils::PlainTextDocumentLayout *>(text->documentLayout())
                : nullptr;
}

// Home, the way an editor means it: the first character that is not
// indentation, and the true start of the line when the cursor is already
// there. Mirrors TextEditorWidgetPrivate::handleHomeKey().
static void moveToFirstCharacter(QTextCursor &cursor, QTextCursor::MoveMode mode)
{
    const QTextBlock block = cursor.block();
    const int start = block.position();
    const int was = cursor.position();

    const QString text = block.text();
    int offset = 0;
    while (offset < text.size()
           && (text.at(offset) == '\t' || text.at(offset).category() == QChar::Separator_Space)) {
        ++offset;
        // Stopping *at* the cursor rather than running past it is what sends a
        // caret sitting inside the indentation to the margin instead of
        // forward to the code. The widget editor stops here too.
        if (start + offset == was)
            break;
    }

    cursor.setPosition(start + offset == was ? start : start + offset, mode);
}

void TextViewport::keyPressEvent(QKeyEvent *event)
{
    QTextCursor cursor = textCursor();
    if (cursor.isNull()) {
        QQuickItem::keyPressEvent(event);
        return;
    }

    const QTextCursor::MoveMode mode = event->modifiers().testFlag(Qt::ShiftModifier)
                                           ? QTextCursor::KeepAnchor
                                           : QTextCursor::MoveAnchor;

    // Home goes to the first thing on the line and only to column zero from
    // there, which is what an editor does with indented code. Before the move
    // table below, which would otherwise take the same key - the same order
    // the widget editor uses.
    const bool toStartOfBlock = event->matches(QKeySequence::MoveToStartOfBlock)
                                || event->matches(QKeySequence::SelectStartOfBlock);
    if (toStartOfBlock || event->matches(QKeySequence::MoveToStartOfLine)
        || event->matches(QKeySequence::SelectStartOfLine)) {
        moveToFirstCharacter(cursor, mode);
        setTextCursor(cursor);
        return event->accept();
    }

    if (event->key() == Qt::Key_PageUp || event->key() == Qt::Key_PageDown) {
        // Pages are the one move that depends on how tall the view is, so they
        // are not in the shared table below.
        const int lines = qMax(1, int(height() / qMax(1.0, m_lineHeight)) - 1);
        cursor.movePosition(event->key() == Qt::Key_PageUp ? QTextCursor::Up
                                                           : QTextCursor::Down,
                            mode, lines);
        setTextCursor(cursor);
        return event->accept();
    }

    // Everything else the keyboard can do to a cursor comes from the one place
    // both editors take it: the platform's own chords - a word is an Alt chord
    // on a Mac and a Ctrl chord elsewhere, and Home means the top of the file
    // on one and the start of the line on the other - plus camel-case stepping
    // and moving through what is on screen rather than what is in the file.
    Utils::MultiTextCursor cursors({cursor});
    if (cursors.handleMoveKeyEvent(event, globalBehaviorSettings().camelCaseNavigation(),
                                   layoutOf(cursor.document()))) {
        setTextCursor(cursors.mainCursor());
        return event->accept();
    }

    // Reading a file means being able to select all of it and copy it, so
    // these come before the read-only guard.
    if (event->matches(QKeySequence::SelectAll)) {
        cursor.select(QTextCursor::Document);
        setTextCursor(cursor);
        event->accept();
        return;
    }
    if (event->matches(QKeySequence::Copy)) {
        if (cursor.hasSelection())
            QGuiApplication::clipboard()->setText(selectedPlainText(cursor));
        event->accept();
        return;
    }

    // Told not to edit, or editing a file the filesystem will not take back.
    // The widget editor offers to make it writable; this only refuses, which
    // is the half that must not be missing.
    TextDocument * const doc = m_document->textDocument();
    if (m_readOnly || (doc && doc->isFileReadOnly())) {
        QQuickItem::keyPressEvent(event);
        return;
    }

    QTextDocument * const text = doc->document();

    // Undo and redo are the document's, so they take back what any other view
    // of it did as well - which is the point of editing through a cursor.
    if (event->matches(QKeySequence::Undo) || event->matches(QKeySequence::Redo)) {
        if (event->matches(QKeySequence::Undo))
            text->undo(&cursor);
        else
            text->redo(&cursor);
        setTextCursor(cursor);
        event->accept();
        return;
    }

    if (event->matches(QKeySequence::Cut)) {
        if (cursor.hasSelection()) {
            QGuiApplication::clipboard()->setText(selectedPlainText(cursor));
            cursor.removeSelectedText();
            setTextCursor(cursor);
        }
        event->accept();
        return;
    }
    if (event->matches(QKeySequence::Paste)) {
        cursor.insertText(QGuiApplication::clipboard()->text());
        setTextCursor(cursor);
        event->accept();
        return;
    }

    switch (event->key()) {
    case Qt::Key_Backspace:
        // Between the two halves of a pair this editor inserted, Backspace
        // takes both - otherwise it leaves the closing one orphaned.
        if (!cursor.hasSelection() && m_autoCompleter->autoBackspace(cursor))
            break;
        // With a selection, Backspace removes it rather than one more character
        // before it, which deletePreviousChar() already does.
        cursor.deletePreviousChar();
        break;
    case Qt::Key_Delete:
        cursor.deleteChar();
        break;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        // The new line starts where the language says it should, which is the
        // difference between an editor and a text box.
        cursor.beginEditBlock();
        cursor.insertText("\n");
        doc->autoIndent(cursor);
        cursor.endEditBlock();
        break;
    case Qt::Key_Tab:
        // One indent's worth of whatever the tab settings say, and a whole
        // block where something is selected. A literal tab is what a text box
        // types; it is not what a code style asks for.
        cursor = doc->indent(Utils::MultiTextCursor({cursor})).mainCursor();
        break;
    case Qt::Key_Backtab:
        cursor = doc->unindent(Utils::MultiTextCursor({cursor})).mainCursor();
        break;
    default:
        // Anything else is text only if it produced any. A modifier chord
        // produces none, and neither does a function key.
        if (event->text().isEmpty() || event->text().at(0).isNonCharacter()
            || event->text().at(0).category() == QChar::Other_Control) {
            QQuickItem::keyPressEvent(event);
            return;
        }
        cursor.insertText(event->text());
        break;
    }

    setTextCursor(cursor);
    event->accept();
}

void TextViewport::inputMethodEvent(QInputMethodEvent *event)
{
    if (m_readOnly) {
        event->ignore();
        return;
    }

    QTextCursor cursor = textCursor();
    if (cursor.isNull()) {
        event->ignore();
        return;
    }

    m_preeditText = event->preeditString();
    m_preeditFormats.clear();
    for (const QInputMethodEvent::Attribute &attribute : event->attributes()) {
        if (attribute.type != QInputMethodEvent::TextFormat)
            continue;
        QTextLayout::FormatRange range;
        range.start = attribute.start;
        range.length = attribute.length;
        range.format = qvariant_cast<QTextFormat>(attribute.value).toCharFormat();
        m_preeditFormats.append(range);
    }

    if (!event->commitString().isEmpty() || event->replacementLength() > 0) {
        // A replacement is counted from the cursor, and may reach back before
        // it - an input method correcting what it already committed.
        if (event->replacementLength() > 0) {
            cursor.setPosition(cursor.position() + event->replacementStart());
            cursor.setPosition(cursor.position() + event->replacementLength(),
                               QTextCursor::KeepAnchor);
        }
        cursor.insertText(event->commitString());
        setTextCursor(cursor);
    }

    polish();
    update();
    event->accept();
}

QVariant TextViewport::inputMethodQuery(Qt::InputMethodQuery query) const
{
    const QTextCursor cursor = textCursor();
    switch (query) {
    case Qt::ImEnabled:
        return !m_readOnly;
    case Qt::ImHints:
        return int(Qt::ImhMultiLine);
    case Qt::ImCursorRectangle:
        return cursorRectangle();
    case Qt::ImFont:
        return m_document && m_document->textDocument()
                   ? m_document->textDocument()->fontSettings().font()
                   : QFont();
    case Qt::ImCursorPosition:
        // Relative to the surrounding text, which is the block the caret is in.
        return cursor.isNull() ? 0 : cursor.positionInBlock();
    case Qt::ImAnchorPosition:
        return cursor.isNull() ? 0 : cursor.anchor() - cursor.block().position();
    case Qt::ImSurroundingText:
        return cursor.isNull() ? QString() : cursor.block().text();
    case Qt::ImCurrentSelection:
        return cursor.isNull() ? QString() : selectedPlainText(cursor);
    case Qt::ImAbsolutePosition:
        return cursor.isNull() ? 0 : cursor.position();
    default:
        return QQuickItem::inputMethodQuery(query);
    }
}

int TextViewport::cursorPosition() const
{
    return m_cursorPosition;
}

void TextViewport::setCursorPosition(int position)
{
    if (m_cursorPosition == position)
        return;
    m_cursorPosition = position;
    polish();
    emit cursorPositionChanged();
    emit cursorRectangleChanged();
}

QRectF TextViewport::cursorRectangle() const
{
    return rectangleAt(m_cursorPosition);
}

QTextBlock TextViewport::cursorBlock() const
{
    TextDocument * const doc = m_document ? m_document->textDocument() : nullptr;
    QTextDocument * const text = doc ? doc->document() : nullptr;
    return text ? text->findBlock(m_cursorPosition) : QTextBlock();
}

int TextViewport::cursorLine() const
{
    const QTextBlock block = cursorBlock();
    return block.isValid() ? block.blockNumber() + 1 : 0;
}

int TextViewport::cursorColumn() const
{
    const QTextBlock block = cursorBlock();
    return block.isValid() ? m_cursorPosition - block.position() + 1 : 0;
}

void TextViewport::selectWordAt(int position)
{
    QTextCursor cursor = textCursor();
    if (cursor.isNull())
        return;

    cursor.setPosition(position);
    cursor.select(QTextCursor::WordUnderCursor);

    // Between two spaces, WordUnderCursor selects the word *before* them, or
    // nothing at the start of a line. Take the whitespace itself instead,
    // which is what the widget editor corrects this to.
    const QTextDocument * const text = cursor.document();
    const QChar here = text->characterAt(position);
    const QChar before = text->characterAt(position - 1);
    if (here.isSpace() && before.isSpace() && here != QChar::ParagraphSeparator) {
        cursor.setPosition(position);
        if (before != QChar::ParagraphSeparator) {
            cursor.movePosition(QTextCursor::PreviousWord);
            cursor.movePosition(QTextCursor::EndOfWord);
        }
        cursor.movePosition(QTextCursor::NextWord, QTextCursor::KeepAnchor);
    }

    setTextCursor(cursor);
}

void TextViewport::selectLineAt(int position)
{
    QTextCursor cursor = textCursor();
    if (cursor.isNull())
        return;

    cursor.setPosition(position);
    cursor.select(QTextCursor::BlockUnderCursor);
    // BlockUnderCursor reaches back over the newline that ends the line above,
    // which reads as selecting two lines. Start where the text does.
    if (cursor.selectionStart() < cursor.block().position()) {
        cursor.setPosition(cursor.block().position());
        cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
    }
    setTextCursor(cursor);
}

void TextViewport::zoomBy(int steps)
{
    if (steps == 0 || !globalBehaviorSettings().scrollWheelZooming())
        return;

    // The same step the widget editor takes, and always at least one, so a
    // high-resolution wheel still does something per notch.
    const int step = steps * 10;
    globalFontSettings().increaseFontZoom(step != 0 ? step : (steps > 0 ? 1 : -1));
}

// The kind under which the bracket pair is highlighted, so that setting it
// replaces the previous pair and nobody else's ranges.
const char PARENTHESES_MATCH[] = "TextEditor.TextViewport.ParenthesesMatch";

void TextViewport::updateParenthesesMatch()
{
    TextDocument * const doc = m_document ? m_document->textDocument() : nullptr;
    QTextDocument * const text = doc ? doc->document() : nullptr;
    if (!text || !displaySettings().data().m_highlightMatchingParentheses) {
        setHighlights(PARENTHESES_MATCH, {});
        return;
    }

    // The caret alone, not the selection: what is being asked is which bracket
    // the caret is beside, and textCursor() answers with the selection when
    // there is one.
    QTextCursor caret(text);
    caret.setPosition(qBound(0, m_cursorPosition, text->characterCount() - 1));

    QTextCursor backward = caret;
    QTextCursor forward = caret;
    const TextBlockUserData::MatchType backwardType
        = TextBlockUserData::matchCursorBackward(&backward);
    const TextBlockUserData::MatchType forwardType
        = TextBlockUserData::matchCursorForward(&forward);

    const FontSettingsData &fonts = doc->fontSettings();
    const QTextCharFormat matched = fonts.toTextCharFormat(C_PARENTHESES);
    // C_PARENTHESES_MISMATCH is an overlay category, so its format is nearly
    // empty by design - the same trap as the search results.
    QTextCharFormat mismatched = fonts.toTextCharFormat(C_PARENTHESES_MISMATCH);
    if (mismatched.background().style() == Qt::NoBrush
        || !mismatched.background().color().isValid()) {
        mismatched.setBackground(Utils::creatorColor(Utils::Theme::TextEditor_SearchResult_ScrollBarColor));
    }

    QList<Highlight> found;
    const auto add = [&found, &matched, &mismatched](const QTextCursor &cursor,
                                                     TextBlockUserData::MatchType type) {
        if (!cursor.hasSelection())
            return;
        if (type == TextBlockUserData::Mismatch) {
            found.append({cursor.selectionStart(), cursor.selectionEnd(), mismatched});
            return;
        }
        // The two brackets themselves rather than everything between them: a
        // whole function body in the parenthesis colour is not a hint.
        found.append({cursor.selectionStart(), cursor.selectionStart() + 1, matched});
        found.append({cursor.selectionEnd() - 1, cursor.selectionEnd(), matched});
    };
    add(backward, backwardType);
    add(forward, forwardType);

    setHighlights(PARENTHESES_MATCH, found);
}

void TextViewport::setHighlights(Utils::Id kind, const QList<Highlight> &highlights)
{
    if (highlights.isEmpty()) {
        if (m_highlights.remove(kind) == 0)
            return;
    } else if (m_highlights.value(kind) == highlights) {
        return;
    } else {
        QList<Highlight> sorted = highlights;
        // Sorted so that a line can find the ranges that reach it without
        // looking at the ones before, which is what keeps highlighting every
        // match in a large file O(what is on screen).
        std::sort(sorted.begin(), sorted.end(),
                  [](const Highlight &a, const Highlight &b) { return a.start < b.start; });
        m_highlights.insert(kind, sorted);
    }
    polish();
    update();
}

QList<TextViewport::Highlight> TextViewport::highlights(Utils::Id kind) const
{
    return m_highlights.value(kind);
}

int TextViewport::cursorDisplayColumn() const
{
    TextDocument * const doc = m_document ? m_document->textDocument() : nullptr;
    const QTextBlock block = cursorBlock();
    if (!doc || !block.isValid())
        return 0;
    return doc->tabSettings().columnAt(block.text(), m_cursorPosition - block.position()) + 1;
}

int TextViewport::selectedCharacterCount() const
{
    if (m_selectionStart < 0 || m_selectionEnd < 0)
        return 0;
    return qAbs(m_selectionEnd - m_selectionStart);
}

QString TextViewport::fileLineEnding() const
{
    TextDocument * const doc = m_document ? m_document->textDocument() : nullptr;
    // The widget editor hides this for a read-only file too: there is nothing
    // to be done about the line endings of a file that cannot be written.
    if (!doc || !displaySettings().displayFileLineEnding() || m_readOnly
        || doc->isFileReadOnly()) {
        return {};
    }

    switch (doc->lineTerminationMode()) {
    case Utils::TextFileFormat::LFLineTerminator:
        return Tr::tr("LF");
    case Utils::TextFileFormat::CRLFLineTerminator:
        return Tr::tr("CRLF");
    default:
        return {};
    }
}

void TextViewport::setFileLineEndingIsWindows(bool windows)
{
    TextDocument * const doc = m_document ? m_document->textDocument() : nullptr;
    if (!doc)
        return;

    const Utils::TextFileFormat::LineTerminationMode wanted
        = windows ? Utils::TextFileFormat::CRLFLineTerminator
                  : Utils::TextFileFormat::LFLineTerminator;
    if (doc->lineTerminationMode() == wanted)
        return;

    doc->setLineTerminationMode(wanted);
    doc->document()->setModified(true);
    emit fileFormatChanged();
}

QString TextViewport::fileEncoding() const
{
    TextDocument * const doc = m_document ? m_document->textDocument() : nullptr;
    if (!doc || !displaySettings().displayFileEncoding())
        return {};
    return doc->encoding().displayName();
}

void TextViewport::gotoLine(int line, int column, bool centerLine)
{
    TextDocument * const doc = m_document ? m_document->textDocument() : nullptr;
    QTextDocument * const text = doc ? doc->document() : nullptr;
    if (!text)
        return;

    const QTextBlock block = text->findBlockByNumber(
        qBound(0, qMin(line, text->blockCount()) - 1, text->blockCount() - 1));
    if (!block.isValid())
        return;

    TextBlockUserData::unfoldTo(block);

    QTextCursor cursor(block);
    if (column >= block.length()) {
        cursor.movePosition(QTextCursor::EndOfBlock);
    } else if (column > 0) {
        cursor.movePosition(QTextCursor::Right, QTextCursor::MoveAnchor, column);
    } else {
        // No column asked for means the line rather than its margin, so this
        // lands on the first thing on it - what a search result or a compiler
        // message is pointing at.
        int position = cursor.position();
        while (text->characterAt(position).category() == QChar::Separator_Space)
            ++position;
        cursor.setPosition(position);
    }

    setSelectionStart(-1);
    setSelectionEnd(-1);
    setCursorPosition(cursor.position());

    // firstLineNumber() rather than the block number: whatever is folded above
    // this line is not a row, and something may have just been unfolded.
    const qreal top = block.firstLineNumber() * m_lineHeight;
    if (centerLine)
        setScrollY(top - (height() - m_lineHeight) / 2);
    else if (top < m_scrollY || top + m_lineHeight > m_scrollY + height())
        setScrollY(top < m_scrollY ? top : top + m_lineHeight - height());
}

void TextViewport::toggleFold(int lineNumber)
{
    TextDocument * const doc = m_document ? m_document->textDocument() : nullptr;
    if (!doc)
        return;
    QTextDocument * const text = doc->document();
    const QTextBlock block = text->findBlockByNumber(lineNumber - 1);
    if (!block.isValid() || !TextBlockUserData::canFold(block))
        return;

    // Folding indents come from the highlighter, so folding while it is still
    // running would fold the range it had worked out so far. The widget editor
    // waits; looking the line up again afterwards rather than keeping the
    // block means it does not matter what the highlighter did to the document.
    if (SyntaxHighlighter * const highlighter = doc->syntaxHighlighter();
        highlighter && !highlighter->syntaxHighlighterUpToDate()) {
        connect(highlighter, &SyntaxHighlighter::finished, this,
                [this, lineNumber] { toggleFold(lineNumber); }, Qt::SingleShotConnection);
        return;
    }

    auto * const layout = qobject_cast<TextDocumentLayout *>(text->documentLayout());
    QTC_ASSERT(layout, return);

    TextBlockUserData::doFoldOrUnfold(block, TextBlockUserData::isFolded(block));
    layout->requestUpdate();
    layout->emitDocumentSizeChanged();

    // A caret left inside what was just folded would type into text nobody can
    // see. The line that owns the fold is visible by construction, so that is
    // where it goes.
    const QTextBlock cursorBlock = text->findBlock(m_cursorPosition);
    if (cursorBlock.isValid() && !cursorBlock.isVisible())
        setCursorPosition(block.position() + block.length() - 1);
}

int TextViewport::positionAt(qreal x, qreal y) const
{
    if (m_lines.empty() || m_lineHeight <= 0)
        return -1;

    // Clamped to what is laid out rather than to the document: a drag that
    // leaves the viewport should select to the edge of it, not jump to the end
    // of the file.
    const int index = qBound(0, int((y + m_scrollY) / m_lineHeight) - m_firstVisibleLine,
                             int(m_lines.size()) - 1);
    const Line &line = m_lines.at(index);
    const QTextLine textLine = line.layout->lineAt(0);
    if (!textLine.isValid())
        return line.blockPosition;

    return line.blockPosition + textLine.xToCursor(x - line.at.x());
}

void TextViewport::documentChangedInternal()
{
    // The QTextDocument only exists once the file has opened, and it is a
    // different one each time it reopens, so the connection to it is made here
    // rather than in setDocument(). Nothing is cached across the signal - the
    // whole visible window is laid out again - so this needs none of the
    // ordering care that a widget editor's contentsChange handler does.
    TextDocument * const doc = m_document ? m_document->textDocument() : nullptr;
    QTextDocument * const text = doc ? doc->document() : nullptr;
    if (text != m_connectedDocument) {
        if (m_connectedDocument)
            disconnect(m_connectedDocument, nullptr, this, nullptr);
        m_connectedDocument = text;
        if (text) {
            connect(text, &QTextDocument::contentsChanged, this, [this] {
                polish();
                update();
            });
        }
    }

    // Marks arrive and go without the text changing: an error appears while
    // the file sits there. The widget gutter is redrawn from
    // TextDocumentLayout::updateExtraArea, so this listens to the same thing,
    // and to the document for the one that says a mark has gone.
    if (doc != m_connectedMarkSource) {
        if (m_connectedMarkSource) {
            disconnect(m_connectedMarkSource, nullptr, this, nullptr);
            if (auto *layout = qobject_cast<TextDocumentLayout *>(
                    m_connectedMarkSource->document()->documentLayout())) {
                disconnect(layout, &TextDocumentLayout::updateExtraArea, this, nullptr);
            }
        }
        m_connectedMarkSource = doc;
        if (doc) {
            connect(doc, &TextDocument::markRemoved, this, [this] {
                polish();
                update();
            });
            // The encoding and the line endings are part of what "the document
            // changed" covers - reopening with another encoding says so here.
            connect(doc, &Core::IDocument::changed, this, &TextViewport::fileFormatChanged);
            // The font, the colours and the zoom are all read in updatePolish()
            // from the document's font settings, and nothing else makes this
            // lay out again - so without this an open file keeps the size and
            // the scheme it was opened with.
            connect(doc, &TextDocument::fontSettingsChanged, this, [this] {
                polish();
                update();
            });
            // A document opened after a zoom starts at the size everything
            // else is already showing.
            applyGlobalFontSettings();
            if (auto *layout = qobject_cast<TextDocumentLayout *>(
                    doc->document()->documentLayout())) {
                connect(layout, &TextDocumentLayout::updateExtraArea, this, [this] {
                    polish();
                    update();
                });
                // The *first* mark on a document does not go through
                // requestExtraAreaUpdate() at all - TextDocument::addMark()
                // calls scheduleUpdate() instead, because the layout has to
                // make room for a gutter it did not have before. That is a
                // layout update, so this listens to the one signal that says
                // so, or the first error in a file is drawn only if something
                // else happens to relayout. Folding arrives the same way:
                // whoever folds a block calls requestUpdate() afterwards.
                connect(layout, &QAbstractTextDocumentLayout::update, this, [this] {
                    polish();
                    update();
                });
            }
        }
    }

    // Both of these are also done by the layout pass the polish below asks
    // for, so there is nothing to do here but ask for it.
    polish();
    update();
}

// Highlighting arrives late and separately. It lands as formats on the
// blocks' own layouts, which is not a content change and so says nothing
// through contentsChanged - and the viewport copies those formats into layouts
// of its own, so without this the text is drawn in one colour until something
// else happens to relayout it.
//
// Called from updatePolish() as well as on a document change, because a
// highlighter is installed on a document that is *already* being shown: an
// editor only knows which language to ask for once it has the file's path, and
// nothing announces the swap.
void TextViewport::connectHighlighter(TextDocument *doc)
{
    SyntaxHighlighter * const highlighter = doc ? doc->syntaxHighlighter() : nullptr;
    if (highlighter == m_connectedHighlighter)
        return;

    if (m_connectedHighlighter)
        disconnect(m_connectedHighlighter, nullptr, this, nullptr);
    m_connectedHighlighter = highlighter;
    if (highlighter) {
        connect(highlighter, &SyntaxHighlighter::finished, this, [this] {
            polish();
            update();
        });
    }
}

void TextViewport::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() != oldGeometry.size())
        polish();
}

void TextViewport::appendHighlights(QList<QTextLayout::FormatRange> &formats,
                                    const QTextBlock &block) const
{

    const int blockStart = block.position();
    // length() counts the newline, which no format can cover.
    const int blockEnd = blockStart + block.length() - 1;

    for (const QList<Highlight> &set : m_highlights) {
        // The first range that reaches this line. Ranges from one source do
        // not overlap, so their ends rise with their starts and this is exact.
        auto it = std::lower_bound(set.cbegin(), set.cend(), blockStart,
                                   [](const Highlight &highlight, int position) {
                                       return highlight.end <= position;
                                   });
        for (; it != set.cend() && it->start < blockEnd; ++it) {
            const int from = qMax(it->start, blockStart);
            const int to = qMin(it->end, blockEnd);
            if (to <= from)
                continue;
            QTextLayout::FormatRange range;
            range.start = from - blockStart;
            range.length = to - from;
            range.format = it->format;
            formats.append(range);
        }
    }
}

void TextViewport::updatePolish()
{
    m_lines.clear();

    TextDocument * const doc = m_document ? m_document->textDocument() : nullptr;
    QTextDocument * const text = doc ? doc->document() : nullptr;

    // Both of these belong here rather than on a signal of their own. Anything
    // that can change which bracket the caret is beside - the caret moving,
    // the text changing, the highlighter finishing a pass - already asks for a
    // layout, and setHighlights() does nothing when the answer is the same, so
    // this settles rather than loops.
    connectHighlighter(doc);
    updateParenthesesMatch();

    if (!text || height() <= 0) {
        m_lineHeight = 0;
        m_contentHeight = 0;
        m_firstVisibleLine = 0;
        emit metricsChanged();
        emit cursorRectangleChanged();
        update();
        return;
    }

    const FontSettingsData &fonts = doc->fontSettings();
    QFont font = fonts.font();
    // font() is the unzoomed font and lineSpacing() is already zoomed, so a
    // font taken straight from font() draws glyphs of one size on lines of
    // another. Zoom it the way lineSpacing() does.
    font.setPointSize(std::max(fonts.fontSize() * fonts.fontZoom() / 100, 1));
    const QFontMetricsF metrics(font);
    // The same height for every line is what makes the line at a scroll offset
    // arithmetic rather than a walk, which is what holds at a million lines.
    // lineSpacing() is what the widget editor lays out with, in pixels, so the
    // two backends put a line in the same place.
    const qreal previousLineHeight = m_lineHeight;
    m_lineHeight = qMax(1.0, fonts.lineSpacing());
    m_font = font;
    m_lineCount = text->blockCount();
    // A folded block counts as zero lines, so lineCount() is how tall the
    // document is and blockCount() is only how far the gutter's numbers run.
    m_contentHeight = m_lineHeight * text->lineCount();
    // The widget editor fills with the *brush*, so a scheme that sets no
    // background paints nothing and the palette shows through. A colour has no
    // way to say "nothing", and QBrush().color() is black, so ask the theme
    // instead of painting the fallback black.
    const QBrush backgroundBrush = fonts.toTextCharFormat(C_TEXT).background();
    m_background = backgroundBrush.style() == Qt::NoBrush
                       ? Utils::creatorColor(Utils::Theme::BackgroundColorNormal)
                       : backgroundBrush.color();

    // The same brush question as the background above: a scheme that sets no
    // current-line colour must draw nothing, and QBrush().color() is black.
    const QBrush currentLineBrush = fonts.toTextCharFormat(C_CURRENT_LINE).background();
    m_currentLine = currentLineBrush.style() == Qt::NoBrush ? QColor(Qt::transparent)
                                                           : currentLineBrush.color();

    const QTextCharFormat selectionFormat = fonts.toTextCharFormat(C_SELECTION);
    const int selectionFrom = qMin(m_selectionStart, m_selectionEnd);
    const int selectionTo = qMax(m_selectionStart, m_selectionEnd);
    const bool hasSelection = m_selectionStart >= 0 && m_selectionEnd >= 0
                              && selectionFrom != selectionTo;

    QTextOption option;
    option.setTabStopDistance(doc->tabSettings().m_tabSize * metrics.horizontalAdvance(' '));
    // No wrapping: see the class comment. A wrapped line breaks the arithmetic
    // above, not just the layout.
    option.setWrapMode(QTextOption::NoWrap);

    m_firstVisibleLine = qMax(0, int(m_scrollY / m_lineHeight));
    // The last line that starts before the bottom edge - not one more. A hair
    // off the edge so that a viewport an exact number of lines tall does not
    // lay out one that begins where it ends.
    const int lastOnScreen = int((m_scrollY + height() - 0.001) / m_lineHeight);
    const int last = qMin(text->lineCount() - 1, lastOnScreen);

    // By line rather than by block, so that what is folded above the screen
    // costs nothing to skip: the document already knows which block a line
    // belongs to, and a hidden one is no line at all.
    QTextBlock block = text->findBlockByLineNumber(m_firstVisibleLine);
    while (block.isValid() && !block.isVisible())
        block = block.next();
    for (int row = m_firstVisibleLine; row <= last && block.isValid(); ++row) {
        Line line;
        // The line the document calls this, which is not the row it is drawn
        // on once anything above it is folded.
        line.lineNumber = block.blockNumber() + 1;
        // A line starts a fold when what follows it is indented deeper - the
        // same test the widget gutter makes - and that fold is closed when
        // what follows is not shown at all.
        const QTextBlock next = block.next();
        line.foldable = next.isValid()
                        && TextBlockUserData::foldingIndent(next)
                               > TextBlockUserData::foldingIndent(block);
        line.folded = line.foldable && !next.isVisible();
        if (line.foldable)
            line.foldIcon = foldMarkerUrl(line.folded);
        if (line.folded) {
            // Everything the fold swallowed, so that its last bracket can be
            // put back on the end of the replacement.
            QTextBlock lastHidden = next;
            while (lastHidden.next().isValid() && !lastHidden.next().isVisible())
                lastHidden = lastHidden.next();
            line.foldReplacement
                = TextBlockUserData::foldReplacementText(QString("..."), next, lastHidden);
        }
        // The marks on this line - errors, warnings, breakpoints. The highest
        // priority one wins the slot, which is what the widget gutter does
        // with the space too.
        const TextMarks marks = doc->marksAt(line.lineNumber);
        const TextMark *shown = nullptr;
        for (TextMark * const mark : marks) {
            if (!mark->isVisible() || mark->icon().isNull())
                continue;
            if (!shown || mark->priority() > shown->priority())
                shown = mark;
        }
        if (shown) {
            line.markIcon = QtcQuick::iconUrl(shown->icon());
            line.annotation = shown->lineAnnotation();
        }

        line.layout = std::make_unique<QTextLayout>(block.text(), font);
        line.layout->setTextOption(option);
        line.layout->setCacheEnabled(true);

        // What the highlighter said about this block, plus the selection over
        // the top of it. Read here, on the GUI thread: the block's own layout
        // belongs to the document and must not be reached from the render one.
        // The highlighter's colours. This layout belongs to the document, which
        // more than one view may be showing - so it is only ever read here, and
        // everything this view alone knows (its selection, its preedit) goes on
        // the copy below. See the migration doc for the one case where that is
        // not enough, which is the widget editor writing *its* preedit formats
        // into this shared list.
        QList<QTextLayout::FormatRange> formats = block.layout()->formats();
        // Under the selection rather than over it: a search result the reader
        // has selected should still look selected.
        appendHighlights(formats, block);
        if (hasSelection) {
            const int blockStart = block.position();
            const int from = qMax(0, selectionFrom - blockStart);
            const int to = qMin(block.length() - 1, selectionTo - blockStart);
            if (to > from) {
                QTextLayout::FormatRange range;
                range.start = from;
                range.length = to - from;
                range.format = selectionFormat;
                formats.append(range);
            }
        }
        // Composing text belongs to the line the caret is on and to no other,
        // and it is not in the document: setPreeditArea() is where a layout
        // keeps text that is being typed but has not been committed.
        if (!m_preeditText.isEmpty() && m_cursorPosition >= block.position()
            && m_cursorPosition < block.position() + block.length()) {
            const int offset = m_cursorPosition - block.position();
            line.layout->setPreeditArea(offset, m_preeditText);
            // The input method says how it wants each part of it drawn -
            // underlined for what is being composed, highlighted for the part
            // under consideration - in positions relative to the preedit.
            for (QTextLayout::FormatRange range : std::as_const(m_preeditFormats)) {
                range.start += offset;
                formats.append(range);
            }
        }
        line.layout->setFormats(formats);

        line.layout->beginLayout();
        QTextLine textLine = line.layout->createLine();
        if (textLine.isValid()) {
            textLine.setPosition(QPointF(0, 0));
            textLine.setLineWidth(std::numeric_limits<qreal>::max());
        }
        line.layout->endLayout();

        line.at = QPointF(-m_scrollX, row * m_lineHeight - m_scrollY);
        line.blockPosition = block.position();
        line.blockLength = block.length();

        // The one thing a format range cannot cover: a selection that runs past
        // the end of the line covers the newline too, and there is no character
        // there to format. One rect, a quarter of a line wide, which is what
        // QTextLayout::draw() does for the same case.
        const int blockEnd = block.position() + block.length() - 1;
        if (hasSelection && selectionFrom <= blockEnd && selectionTo > blockEnd
            && textLine.isValid()) {
            const qreal right = textLine.naturalTextRect().right();
            line.newlineTail = QRectF(right, 0, m_lineHeight / 4, m_lineHeight);
            line.newlineTailColour = selectionFormat.background().color();
        }

        m_lines.push_back(std::move(line));

        do
            block = block.next();
        while (block.isValid() && !block.isVisible());
    }

    if (!qFuzzyCompare(previousLineHeight + 1, m_lineHeight + 1))
        setScrollY(m_scrollY); // re-clamp: the document is a different height now
    emit metricsChanged();
    // What is on screen has just been rebuilt, marks and all. Delegates read it
    // through visibleLines, so this is what tells them to look again.
    emit linesChanged();
    // Everything the caret's position on screen depends on was just recomputed.
    emit cursorRectangleChanged();
    update();
}

QSGNode *TextViewport::updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *)
{
    // Nothing here reads the document. Everything it draws was decided in
    // updatePolish() on the GUI thread.
    delete oldNode;
    if (m_lines.empty())
        return nullptr;

    QQuickWindow * const win = window();
    if (!win)
        return nullptr;

    auto *root = new QSGNode;
    for (const Line &line : m_lines) {
        if (!line.newlineTail.isEmpty()) {
            QSGRectangleNode * const tail = win->createRectangleNode();
            tail->setRect(line.newlineTail.translated(line.at));
            tail->setColor(line.newlineTailColour);
            root->appendChildNode(tail);
        }
        QSGTextNode * const node = win->createTextNode();
        node->setRenderType(QSGTextNode::NativeRendering);
        node->addTextLayout(line.at, line.layout.get());
        root->appendChildNode(node);
    }
    return root;
}

} // namespace TextEditor
