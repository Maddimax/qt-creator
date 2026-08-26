// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "textviewport.h"

#include "codesource.h"
#include "fontsettings.h"
#include "tabsettings.h"
#include "syntaxhighlighter.h"
#include "textdocument.h"
#include "texteditorconstants.h"

#include <utils/theme/theme.h>

#include <QFontMetricsF>
#include <QQuickWindow>
#include <QSGRectangleNode>
#include <QSGTextNode>
#include <utils/multitextcursor.h>

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
    const int line = cursor.blockNumber();
    const qreal top = line * m_lineHeight;
    if (top < m_scrollY)
        setScrollY(top);
    else if (top + m_lineHeight > m_scrollY + height())
        setScrollY(top + m_lineHeight - height());
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
    const auto move = [&](QTextCursor::MoveOperation operation) {
        cursor.movePosition(operation, mode);
        setTextCursor(cursor);
        event->accept();
    };

    switch (event->key()) {
    case Qt::Key_Left:   return move(QTextCursor::Left);
    case Qt::Key_Right:  return move(QTextCursor::Right);
    case Qt::Key_Up:     return move(QTextCursor::Up);
    case Qt::Key_Down:   return move(QTextCursor::Down);
    case Qt::Key_Home:   return move(QTextCursor::StartOfLine);
    case Qt::Key_End:    return move(QTextCursor::EndOfLine);
    case Qt::Key_PageUp:
    case Qt::Key_PageDown: {
        const int lines = qMax(1, int(height() / qMax(1.0, m_lineHeight)) - 1);
        cursor.movePosition(event->key() == Qt::Key_PageUp ? QTextCursor::Up
                                                           : QTextCursor::Down,
                            mode, lines);
        setTextCursor(cursor);
        return event->accept();
    }
    default:
        break;
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

    if (m_readOnly) {
        QQuickItem::keyPressEvent(event);
        return;
    }

    TextDocument * const doc = m_document->textDocument();
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
    emit cursorPositionChanged();
    emit cursorRectangleChanged();
}

QRectF TextViewport::cursorRectangle() const
{
    return rectangleAt(m_cursorPosition);
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

    // Highlighting arrives late and separately. It lands as formats on the
    // blocks' own layouts, which is not a content change and so says nothing
    // through contentsChanged - and the viewport copies those formats into
    // layouts of its own, so without this the text is drawn in one colour
    // until something else happens to relayout it.
    SyntaxHighlighter * const highlighter = doc ? doc->syntaxHighlighter() : nullptr;
    if (highlighter != m_connectedHighlighter) {
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

    polish();
    update();
}

void TextViewport::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() != oldGeometry.size())
        polish();
}

void TextViewport::updatePolish()
{
    m_lines.clear();

    TextDocument * const doc = m_document ? m_document->textDocument() : nullptr;
    QTextDocument * const text = doc ? doc->document() : nullptr;
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
    m_contentHeight = m_lineHeight * m_lineCount;
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
    const int last = qMin(text->blockCount() - 1, lastOnScreen);

    QTextBlock block = text->findBlockByNumber(m_firstVisibleLine);
    for (int number = m_firstVisibleLine; number <= last && block.isValid();
         ++number, block = block.next()) {
        Line line;
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

        line.at = QPointF(-m_scrollX, number * m_lineHeight - m_scrollY);
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
    }

    if (!qFuzzyCompare(previousLineHeight + 1, m_lineHeight + 1))
        setScrollY(m_scrollY); // re-clamp: the document is a different height now
    emit metricsChanged();
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
