// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "core_global.h"

#include <QList>
#include <QString>

#include <chrono>
#include <functional>

QT_BEGIN_NAMESPACE
class QPalette;
class QTextCursor;
class QTextDocument;
QT_END_NAMESPACE

namespace Core::OutputText {

// The decisions an output pane makes about the text it is showing: what a
// filter hides, how much is kept, where a chunk is cut, when a backlog is
// thrown away. All of them lived inside Core::OutputWindow, where the only way
// to ask one anything was to run a build and look at a pane.

enum class FilterModeFlag {
    Default       = 0x00, // Plain text, non case sensitive, for initialization
    RegExp        = 0x01,
    CaseSensitive = 0x02,
    Inverted      = 0x04,
};
Q_DECLARE_FLAGS(FilterModeFlags, FilterModeFlag)

// What a line has to satisfy to stay visible under \a filterText. Kept out
// of the class because it is a decision about text, not about a widget:
// inside one it could only be exercised by typing into a pane.
// What a line has to satisfy to stay visible under a filter.
using TextMatchingFunction = std::function<bool(const QString &text)>;

CORE_EXPORT TextMatchingFunction filterPredicate(const QString &filterText,
                                            FilterModeFlags mode);

// The lines a filter reveals around what it matched: \a before above each
// match and \a after below, within a document of \a lineCount lines. The
// matches themselves are not in it - they are visible already. Also a
// decision about text: the widget expressed it by asking a document for
// block numbers that may not exist and letting the invalid ones do
// nothing, which is a clamp nobody could see.
CORE_EXPORT QList<int> contextLines(const QList<int> &matchedLines, int lineCount,
                               int before, int after);

// What is shown of a chunk too long to show: the first and last half of
// what the limit allows, with a note between them saying how much went.
CORE_EXPORT QString elideChunk(const QString &chunk, qsizetype maxCharCount);

// How many blocks a document may keep for \a incomingChars more characters
// to fit within \a maxCharCount, given what it holds now. -1 where nothing
// has to go, which is what QPlainTextEdit reads as "no limit".
CORE_EXPORT int blocksToKeep(const QList<int> &blockLengths, qsizetype existingChars,
                        qsizetype incomingChars, qsizetype maxCharCount);

// Fills \a target with the lines of \a source that \a matches accepts, plus
// \a before and \a after lines of context, keeping how each was drawn.
//
// A Qt Quick view cannot be filtered the way a QPlainTextEdit is: it
// ignores QTextBlock::setVisible(), so a filtered view means a filtered
// document rather than one with parts switched off.
CORE_EXPORT void copyFiltered(const QTextDocument *source, QTextDocument *target,
                         const TextMatchingFunction &matches, int before, int after);

// How far a filtered document has followed the one it is built from.
struct FilteredAppendState
{
    int lastConsidered = -1; // The last source block looked at.
    int lastEmitted = -1;    // The last source block copied over.
    int afterRemaining = 0;  // Context lines still owed after a match.
};

// Copies the lines \a source has gained since \a state last saw it. A
// rebuild costs the whole document, which a build appending thousands of
// times cannot pay; this pays only for what arrived.
//
// A line that arrives later can make an earlier skipped one into context,
// so this is not simply "copy what matches": the lines owed *before* a new
// match are copied with it.
//
// The source's last block is left alone: output arrives as text and then a
// newline, so whatever is last is a line still being written. Unlike
// copyFiltered(), which is given a document nobody is adding to.
CORE_EXPORT void appendFiltered(const QTextDocument *source, QTextDocument *target,
                           const TextMatchingFunction &matches, int before, int after,
                           FilteredAppendState &state);

// Dims the output a run left behind, so that what the next one appends
// stands out from it. \a startOfNewContent marks where the run being dimmed
// began, and is moved to where the next one will.
//
// It is a cursor rather than a position because the document is also cut
// from the front when the character limit bites, which would leave a
// position pointing at a line that is no longer the one meant.
//
// The palette is passed in because the dimmed colour is halfway between
// the text and the background, and a Qt Quick view has no widget palette
// to read that from.
CORE_EXPORT void grayOutContentBefore(QTextCursor &startOfNewContent, const QPalette &palette);

// Removes every line starting with \a prefix. What lets a pane retract
// what it said - the progress lines a build system overwrites.
CORE_EXPORT void removeLinesPrefixedWith(QTextDocument *document, const QString &prefix,
                                    bool deleteTrailingLineBreak);

// How much of \a text to write now. Up to \a chunkSize characters, but
// backed off to just after the last line end within a thousand characters
// of it: cutting through an ANSI escape code draws the code as text.
CORE_EXPORT qsizetype chunkEndPosition(const QString &text, qsizetype chunkSize);

// How fast output is being written out: how much goes at a time, and how
// long to wait between chunks.
struct OutputPacing
{
    std::chrono::milliseconds interval;
    qsizetype chunkSize;
};

// The pacing to use next, given how long the last chunk took to format.
// Slower than the interval and it backs off - half the chunk, twice the
// wait - so a pane that cannot keep up stops trying. Comfortably faster,
// and only while there is still more of the same chunk to write, it speeds
// up again. Nothing changes in between, so ordinary output does not make
// the pane oscillate.
CORE_EXPORT OutputPacing pacedBy(OutputPacing current, std::chrono::milliseconds formatterTook,
                            bool chunkWasSplit);

// How the backlog has been moving, so a flood can be told from a burst.
struct PendingOutputState
{
    QList<qsizetype> queuedSizeHistory;
};

// Whether the output still waiting should be thrown away rather than
// shown. Two reasons, and either is enough:
//
//   - the backlog has grown ten times in a row and is more than five
//     chunks' worth, so more is arriving than can be drawn;
//   - writing what is waiting would take over a minute, and enough has
//     been shown already that the user is not staring at an empty pane.
//
// Kept out of the widget because it is a judgement about numbers, and
// inside one the only way to reach it was to flood a real build.
CORE_EXPORT bool shouldDiscardPendingOutput(PendingOutputState &state, qsizetype queuedSize,
                                       qsizetype chunkSize, int formatterCalls,
                                       std::chrono::milliseconds interval);

#ifdef WITH_TESTS
QObject *createOutputTextTest();
#endif

} // namespace Core::OutputText
