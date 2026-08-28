// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "textoperations.h"

#include "tabsettings.h"
#include "textdocument.h"

#include <utils/multitextcursor.h>
#include <utils/qtcassert.h>
#include <utils/uncommentselection.h>

#include <QRegularExpression>
#include <QTextBlock>

namespace TextEditor {

void joinLines(Utils::MultiTextCursor &cursor)
{
    cursor.beginEditBlock();
    for (QTextCursor &c : cursor) {
        QTextCursor start = c;
        QTextCursor end = c;

        start.setPosition(c.selectionStart());
        end.setPosition(c.selectionEnd() - 1);

        int lineCount = qMax(1, end.blockNumber() - start.blockNumber());

        c.setPosition(c.selectionStart());
        while (lineCount--) {
            c.movePosition(QTextCursor::NextBlock);
            c.movePosition(QTextCursor::StartOfBlock);
            c.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
            QString cutLine = c.selectedText();

            // Collapse leading whitespaces to one or insert whitespace
            static const QRegularExpression regexp("^\\s*");
            cutLine.replace(regexp, QLatin1String(" "));
            c.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor);
            c.removeSelectedText();

            c.movePosition(QTextCursor::PreviousBlock);
            c.movePosition(QTextCursor::EndOfBlock);

            c.insertText(cutLine);
        }
    }
    cursor.endEditBlock();
    cursor.mergeCursors();
}

void transformSelection(Utils::MultiTextCursor &cursor, const TextTransformation &transform)
{
    const bool several = cursor.hasMultipleCursors();
    cursor.beginEditBlock();
    for (QTextCursor &c : cursor) {
        const int pos = c.position();
        const int anchor = c.anchor();

        if (!c.hasSelection() && !several)
            c.select(QTextCursor::WordUnderCursor);

        const QString text = c.selectedText();
        const QString transformed = transform(text);
        if (transformed == text)
            continue;

        c.insertText(transformed);

        // Select the changed text again, which assumes the transformation did
        // not change its length.
        c.setPosition(anchor);
        c.setPosition(pos, QTextCursor::KeepAnchor);
    }
    cursor.endEditBlock();
}

void insertLineAbove(Utils::MultiTextCursor &cursor, TextDocument *document)
{
    cursor.beginEditBlock();
    for (QTextCursor &c : cursor) {
        // At the very start of the document there is no previous block to
        // open one after, so the line is opened here and stepped back onto.
        c.movePosition(QTextCursor::StartOfBlock, QTextCursor::MoveAnchor);
        c.insertBlock();
        c.movePosition(QTextCursor::PreviousBlock, QTextCursor::MoveAnchor);
        if (document)
            document->autoIndent(c);
    }
    cursor.endEditBlock();
}

void insertLineBelow(Utils::MultiTextCursor &cursor, TextDocument *document)
{
    cursor.beginEditBlock();
    for (QTextCursor &c : cursor) {
        c.movePosition(QTextCursor::EndOfBlock, QTextCursor::MoveAnchor);
        c.insertBlock();
        if (document)
            document->autoIndent(c);
    }
    cursor.endEditBlock();
}

void duplicateSelection(Utils::MultiTextCursor &cursor, const Utils::CommentDefinition *comment)
{
    // There is nowhere to put the markers of a language that has no block
    // comment, so the command does nothing rather than something wrong.
    if (comment && !comment->hasMultiLineStyle())
        return;

    const bool several = cursor.hasMultipleCursors();
    cursor.beginEditBlock();
    for (QTextCursor &c : cursor) {
        if (c.hasSelection()) {
            QString duplicate
                = c.selectedText().replace(QChar::ParagraphSeparator, QLatin1Char('\n'));
            if (comment)
                duplicate = comment->multiLineStart + duplicate + comment->multiLineEnd;
            const int selStart = c.selectionStart();
            const int selEnd = c.selectionEnd();
            const bool cursorAtStart = c.position() == selStart;
            c.setPosition(selEnd);
            c.insertText(duplicate);
            c.setPosition(cursorAtStart ? selEnd : selStart);
            c.setPosition(cursorAtStart ? selStart : selEnd, QTextCursor::KeepAnchor);
        } else if (!several) {
            const int at = c.position();
            const QTextBlock &block = c.block();
            QString duplicate = block.text() + QLatin1Char('\n');
            if (comment && comment->hasSingleLineStyle())
                duplicate.append(comment->singleLine);
            c.setPosition(block.position());
            c.insertText(duplicate);
            c.setPosition(at);
        }
    }
    cursor.endEditBlock();
}

bool sortLines(QTextCursor &cursor, const TabSettingsData &tabSettings)
{
    if (!cursor.hasSelection()) {
        // Without a selection the scope is the run of lines around the caret
        // at the same indentation, which is usually the list it is standing
        // in.
        const QTextBlock currentBlock = cursor.block();
        QString text = currentBlock.text();
        if (text.simplified().isEmpty())
            return false;
        const int currentIndent
            = tabSettings.columnAt(text, TabSettingsData::firstNonSpace(text));

        int anchor = currentBlock.position();
        for (auto block = currentBlock.previous(); block.isValid(); block = block.previous()) {
            text = block.text();
            if (text.simplified().isEmpty()
                || tabSettings.columnAt(text, TabSettingsData::firstNonSpace(text))
                       != currentIndent) {
                break;
            }
            anchor = block.position();
        }

        int pos = currentBlock.position();
        for (auto block = currentBlock.next(); block.isValid(); block = block.next()) {
            text = block.text();
            if (text.simplified().isEmpty()
                || tabSettings.columnAt(text, TabSettingsData::firstNonSpace(text))
                       != currentIndent) {
                break;
            }
            pos = block.position();
        }
        if (anchor == pos)
            return false;

        cursor.setPosition(anchor);
        cursor.setPosition(pos, QTextCursor::KeepAnchor);
        cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
    }

    const bool downwardDirection = cursor.anchor() < cursor.position();
    int startPosition = cursor.selectionStart();
    int endPosition = cursor.selectionEnd();

    cursor.setPosition(startPosition);
    cursor.movePosition(QTextCursor::StartOfBlock);
    startPosition = cursor.position();

    cursor.setPosition(endPosition, QTextCursor::KeepAnchor);
    if (cursor.positionInBlock() == 0)
        cursor.movePosition(QTextCursor::PreviousBlock, QTextCursor::KeepAnchor);
    cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
    endPosition = qMax(cursor.position(), endPosition);

    QStringList lines = cursor.selectedText().split(QChar::ParagraphSeparator);
    lines.sort();
    cursor.insertText(lines.join(QChar::ParagraphSeparator));

    // Select the sorted lines again, which assumes sorting did not change how
    // long they are altogether.
    cursor.setPosition(downwardDirection ? startPosition : endPosition);
    cursor.setPosition(downwardDirection ? endPosition : startPosition, QTextCursor::KeepAnchor);
    return true;
}

void maybeSelectLine(Utils::MultiTextCursor &cursor, QTextDocument *document)
{
    if (!document || cursor.hasSelection())
        return;
    for (QTextCursor &c : cursor) {
        const QTextBlock &block = document->findBlock(c.selectionStart());
        const QTextBlock &end = document->findBlock(c.selectionEnd()).next();
        c.setPosition(block.position());
        if (!end.isValid()) {
            // The last line has no newline after it to swallow, so the line
            // before it gives up its own instead.
            c.movePosition(QTextCursor::PreviousCharacter);
            c.movePosition(QTextCursor::End, QTextCursor::KeepAnchor);
        } else {
            c.setPosition(end.position(), QTextCursor::KeepAnchor);
        }
    }
    cursor.mergeCursors();
}

void copyLineUpDown(QTextCursor &cursor, bool up, TextDocument *document)
{
    QTextCursor move = cursor;
    move.beginEditBlock();

    if (cursor.hasSelection()) {
        move.setPosition(cursor.selectionStart());
        move.movePosition(QTextCursor::StartOfBlock);
        move.setPosition(cursor.selectionEnd(), QTextCursor::KeepAnchor);
        move.movePosition(move.atBlockStart() ? QTextCursor::Left : QTextCursor::EndOfBlock,
                          QTextCursor::KeepAnchor);
    } else {
        move.movePosition(QTextCursor::StartOfBlock);
        move.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
    }

    const QString text = move.selectedText();

    if (up) {
        move.setPosition(cursor.selectionStart());
        move.movePosition(QTextCursor::StartOfBlock);
        move.insertBlock();
        move.movePosition(QTextCursor::Left);
    } else {
        move.movePosition(QTextCursor::EndOfBlock);
        if (move.atBlockStart()) {
            move.movePosition(QTextCursor::NextBlock);
            move.insertBlock();
            move.movePosition(QTextCursor::Left);
        } else {
            move.insertBlock();
        }
    }

    const int start = move.position();
    move.clearSelection();
    move.insertText(text);
    const int end = move.position();

    move.setPosition(start);
    move.setPosition(end, QTextCursor::KeepAnchor);

    if (document)
        document->autoIndent(move);
    move.endEditBlock();

    cursor = move;
}

QTextCursor selectLinesToMove(const QTextCursor &cursor)
{
    QTextCursor move = cursor;
    // Opens folded items rather than destroying them.
    move.setVisualNavigation(false);

    if (cursor.hasSelection()) {
        move.setPosition(cursor.selectionStart());
        move.movePosition(QTextCursor::StartOfBlock);
        move.setPosition(cursor.selectionEnd(), QTextCursor::KeepAnchor);
        move.movePosition(move.atBlockStart() ? QTextCursor::PreviousCharacter
                                              : QTextCursor::EndOfBlock,
                          QTextCursor::KeepAnchor);
    } else {
        move.movePosition(QTextCursor::StartOfBlock);
        move.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
    }
    return move;
}

int moveSelectedLines(QTextCursor &move, bool up, bool hadSelection, TextDocument *document,
                      const Utils::CommentDefinition &comment)
{
    const QString text = move.selectedText();

    move.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor);
    move.removeSelectedText();

    if (up) {
        move.movePosition(QTextCursor::PreviousBlock);
        move.insertBlock();
        move.movePosition(QTextCursor::PreviousCharacter);
    } else {
        move.movePosition(QTextCursor::EndOfBlock);
        if (move.atBlockStart()) { // empty block
            move.movePosition(QTextCursor::NextBlock);
            move.insertBlock();
            move.movePosition(QTextCursor::PreviousCharacter);
        } else {
            move.insertBlock();
        }
    }

    const int start = move.position();
    move.clearSelection();
    move.insertText(text);
    const int end = move.position();

    if (hadSelection) {
        move.setPosition(end);
        move.setPosition(start, QTextCursor::KeepAnchor);
    } else {
        move.setPosition(start);
    }

    // Text that is entirely commented out keeps the indentation it was given:
    // where a comment sits is the reader's business, and re-indenting it would
    // undo that every time the line moved.
    bool shouldReindent = true;
    if (comment.isValid()) {
        if (comment.hasMultiLineStyle()) {
            if (text.startsWith(comment.multiLineStart) && text.endsWith(comment.multiLineEnd))
                shouldReindent = false;
        }
        if (shouldReindent && comment.hasSingleLineStyle()) {
            shouldReindent = false;
            QTextBlock block = move.block();
            while (block.isValid() && block.position() < end) {
                if (!block.text().startsWith(comment.singleLine))
                    shouldReindent = true;
                block = block.next();
            }
        }
    }

    if (shouldReindent && document)
        document->autoReindent(move);

    return start;
}

void rewrapParagraph(QTextCursor &cursor, const TabSettingsData &ts, int paragraphWidth)
{
    static const QRegularExpression anyLettersOrNumbers("\\w");

    cursor.beginEditBlock();

    // A single-line ("//") comment forms a paragraph on its own: it must not
    // be merged with adjacent code lines, which are not part of the comment
    // (QTCREATORBUG-31149).
    static const QRegularExpression lineCommentLeader("^\\s*//+[/!]*");
    const auto commentLeader = [](const QString &text) {
        return lineCommentLeader.match(text).captured(0);
    };
    const bool inLineComment = !commentLeader(cursor.block().text()).isEmpty();

    // Blank lines end a plain-text paragraph; a comment paragraph also ends
    // where the run of "//" comment lines does.
    const auto isParagraphBoundary = [&](const QString &text) {
        if (inLineComment)
            return commentLeader(text).isEmpty();
        return !text.contains(anyLettersOrNumbers);
    };

    // Find start of paragraph.

    while (cursor.movePosition(QTextCursor::PreviousBlock, QTextCursor::MoveAnchor)) {
        QTextBlock block = cursor.block();
        QString text = block.text();

        // If this block ends the paragraph, move marker back and terminate.
        if (isParagraphBoundary(text)) {
            cursor.movePosition(QTextCursor::NextBlock, QTextCursor::MoveAnchor);
            break;
        }
    }

    cursor.movePosition(QTextCursor::StartOfBlock, QTextCursor::MoveAnchor);

    // Find indent level of current block.
    const QString text = cursor.block().text();
    int indentLevel = ts.indentationColumn(text);

    // If there is a common prefix, it should be kept and expanded to all lines.
    // this allows nice reflowing of doxygen style comments.
    QTextCursor nextBlock = cursor;
    QString commonPrefix;

    const QString doxygenPrefix("^\\s*(?:///|/\\*\\*|/\\*\\!|\\*)?[ *]+");
    if (nextBlock.movePosition(QTextCursor::NextBlock))
    {
         QString nText = nextBlock.block().text();
         int maxLength = qMin(text.size(), nText.size());

         const auto hasDoxygenPrefix = [&] {
             static const QRegularExpression pattern(doxygenPrefix);
             return pattern.match(commonPrefix).hasMatch();
         };

         for (int i = 0; i < maxLength; ++i) {
             const QChar ch = text.at(i);

             if (ch != nText[i] || ch.isLetterOrNumber()
                     || ((ch == '@' || ch == '\\' ) && hasDoxygenPrefix())) {
                 break;
             }
             commonPrefix.append(ch);
         }
    }

    // A lone "//" comment line has no following comment line to derive the
    // common prefix from; take it from the comment leader itself, so the
    // leader is preserved instead of being reflowed away.
    if (commonPrefix.isEmpty() && inLineComment) {
        static const QRegularExpression leaderWithSpace("^\\s*//+[/!]*\\s?");
        commonPrefix = leaderWithSpace.match(cursor.block().text()).captured(0);
    }

    // Find end of paragraph.
    static const QRegularExpression immovableDoxygenCommand(doxygenPrefix + "[@\\\\][a-zA-Z]{2,}");
    QTC_CHECK(immovableDoxygenCommand.isValid());
    while (cursor.movePosition(QTextCursor::NextBlock, QTextCursor::KeepAnchor)) {
        QString text = cursor.block().text();

        if (isParagraphBoundary(text) || immovableDoxygenCommand.match(text).hasMatch())
            break;
    }


    QString selectedText = cursor.selectedText();

    // Preserve initial indent level.or common prefix.
    QString spacing;

    if (commonPrefix.isEmpty()) {
        spacing = ts.indentationString(0, indentLevel, 0);
    } else {
        spacing = commonPrefix;
        indentLevel = ts.columnCountForText(spacing);
    }

    int currentLength = indentLevel;
    QString result;
    result.append(spacing);

    // Remove existing instances of any common prefix from paragraph to
    // reflow.
    selectedText.remove(0, commonPrefix.size());
    commonPrefix.prepend(QChar::ParagraphSeparator);
    selectedText.replace(commonPrefix, QLatin1String("\n"));

    // remove any repeated spaces, trim lines to PARAGRAPH_WIDTH width and
    // keep the same indentation level as first line in paragraph.
    QString currentWord;

    for (const QChar &ch : std::as_const(selectedText)) {
        if (ch.isSpace() && ch != QChar::Nbsp) {
            if (!currentWord.isEmpty()) {
                currentLength += currentWord.size() + 1;

                if (currentLength > paragraphWidth) {
                    currentLength = currentWord.size() + 1 + indentLevel;
                    result.chop(1); // remove trailing space
                    result.append(QChar::ParagraphSeparator);
                    result.append(spacing);
                }

                result.append(currentWord);
                result.append(QLatin1Char(' '));
                currentWord.clear();
            }

            continue;
        }

        currentWord.append(ch);
    }
    result.chop(1);
    result.append(QChar::ParagraphSeparator);

    cursor.insertText(result);
    cursor.endEditBlock();
}

} // namespace TextEditor
