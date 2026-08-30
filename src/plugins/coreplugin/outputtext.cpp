// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "outputtext.h"

#include "coreplugintr.h"
#include "outputtasksink.h"

#include <utils/qtcassert.h>

#include <QPalette>
#include <QRegularExpression>
#include <QTextBlock>
#include <QTextCursor>
#include <QElapsedTimer>
#include <QTextDocument>

#ifdef WITH_TESTS
#include <utils/outputformatter.h>

#include <QTest>
#endif

using namespace std::chrono_literals;

namespace Core::OutputText {

// How long the pane waits between chunks at most, and how small a chunk it
// will fall back to. Both are the ends the pacing runs between.
const auto maxInterval = 1000ms;
const qsizetype minChunkSize = 1000;

qsizetype chunkEndPosition(const QString &text, qsizetype chunkSize)
{
    qsizetype end = std::min(chunkSize, text.size());
    const qsizetype earliest = std::max(qsizetype(0), end - 1000);
    for (qsizetype i = end - 1; i >= earliest; --i) {
        if (text.at(i) == '\n')
            return i + 1;
    }
    return end;
}

bool shouldDiscardPendingOutput(PendingOutputState &state, qsizetype queuedSize,
                                              qsizetype chunkSize, int formatterCalls,
                                              std::chrono::milliseconds interval)
{
    // A backlog that shrank is being kept up with, whatever it did before.
    if (!state.queuedSizeHistory.isEmpty() && state.queuedSizeHistory.last() > queuedSize)
        state.queuedSizeHistory.clear();
    state.queuedSizeHistory << queuedSize;

    if (state.queuedSizeHistory.size() > 10 && queuedSize > 5 * chunkSize)
        return true;

    return formatterCalls >= 10 && (queuedSize / chunkSize) * interval > 60s;
}

OutputPacing pacedBy(OutputPacing current,
                                                std::chrono::milliseconds formatterTook,
                                                bool chunkWasSplit)
{
    if (formatterTook > current.interval) {
        return {std::min(maxInterval, current.interval * 2),
                std::max(minChunkSize, current.chunkSize / 2)};
    }
    if (chunkWasSplit && formatterTook < current.interval / 2) {
        return {std::max(1ms, current.interval * 2 / 3),
                qsizetype(current.chunkSize * 1.5)};
    }
    return current;
}

void copyFiltered(const QTextDocument *source, QTextDocument *target,
                                const TextMatchingFunction &matches, int before, int after)
{
    QTC_ASSERT(source && target && matches, return);

    QList<int> matched;
    for (QTextBlock block = source->begin(); block != source->end(); block = block.next()) {
        if (matches(block.text()))
            matched << block.blockNumber();
    }

    QSet<int> keep(matched.begin(), matched.end());
    const QList<int> context = contextLines(matched, source->blockCount(), before, after);
    for (const int line : context)
        keep.insert(line);

    target->clear();
    QTextCursor cursor(target);
    bool first = true;
    for (QTextBlock block = source->begin(); block != source->end(); block = block.next()) {
        if (!keep.contains(block.blockNumber()))
            continue;
        if (!first)
            cursor.insertBlock();
        first = false;
        // Fragment by fragment: a line of output is usually one, but a parser
        // that marked part of it - a file name made into a link - leaves
        // several, and copying the text alone would drop what it did.
        for (QTextBlock::iterator it = block.begin(); it != block.end(); ++it) {
            const QTextFragment fragment = it.fragment();
            if (fragment.isValid())
                cursor.insertText(fragment.text(), fragment.charFormat());
        }
    }
}

void appendFiltered(const QTextDocument *source, QTextDocument *target,
                                  const TextMatchingFunction &matches, int before, int after,
                                  FilteredAppendState &state)
{
    QTC_ASSERT(source && target && matches, return);

    QTextCursor cursor(target);
    cursor.movePosition(QTextCursor::End);

    const auto emitBlock = [&](const QTextBlock &block) {
        // An empty target starts with one empty block, which is the first line
        // rather than a line before it.
        if (!(target->blockCount() == 1 && target->firstBlock().length() <= 1))
            cursor.insertBlock();
        for (QTextBlock::iterator it = block.begin(); it != block.end(); ++it) {
            const QTextFragment fragment = it.fragment();
            if (fragment.isValid())
                cursor.insertText(fragment.text(), fragment.charFormat());
        }
        state.lastEmitted = block.blockNumber();
    };

    // Every block but the last. Nothing follows the last one, so it is a line
    // still being written: output arrives as text and then a newline, so the
    // block that is last now will have more added to it before it is done.
    // Considering it here would emit half a line and then never look again.
    const int complete = source->blockCount() - 1;
    for (int number = state.lastConsidered + 1; number < complete; ++number) {
        const QTextBlock block = source->findBlockByNumber(number);
        state.lastConsidered = number;
        if (matches(block.text())) {
            // What this match is owed behind it, minus whatever is already
            // there: the lines between are the ones that were skipped for not
            // matching, and this match is what makes them context.
            const int firstOwed = std::max(state.lastEmitted + 1, number - before);
            for (int owed = firstOwed; owed < number; ++owed)
                emitBlock(source->findBlockByNumber(owed));
            emitBlock(block);
            state.afterRemaining = after;
        } else if (state.afterRemaining > 0) {
            emitBlock(block);
            state.afterRemaining -= 1;
        }
    }
}

QString elideChunk(const QString &chunk, qsizetype maxCharCount)
{
    if (chunk.size() <= maxCharCount)
        return chunk;

    // Both halves of what the limit allows, which for an odd limit is one
    // character fewer. The count reported is what actually goes, not
    // size - maxCharCount: those differ by that same character, and the
    // message is the only place anyone can see either.
    const qsizetype half = maxCharCount / 2;
    const qsizetype elided = chunk.size() - 2 * half;
    return chunk.left(half)
           + "[[[... "
           + Tr::tr("Elided %n characters due to settings limit", nullptr, elided)
           + " ...]]]"
           + chunk.right(half);
}

int blocksToKeep(const QList<int> &blockLengths, qsizetype existingChars,
                               qsizetype incomingChars, qsizetype maxCharCount)
{
    qsizetype planned = existingChars + incomingChars;
    if (planned <= maxCharCount)
        return -1;

    // Drop leading blocks until what is coming fits - but never the last one,
    // which is where the new text lands.
    int keep = int(blockLengths.size());
    for (const int length : blockLengths) {
        if (planned <= maxCharCount || keep <= 1)
            break;
        planned -= length;
        keep -= 1;
    }
    return keep;
}

QList<int> contextLines(const QList<int> &matchedLines, int lineCount,
                                      int before, int after)
{
    QSet<int> matches(matchedLines.begin(), matchedLines.end());
    QSet<int> revealed;
    for (const int line : matchedLines) {
        for (int i = 1; i <= before; ++i) {
            const int above = line - i;
            if (above >= 0 && !matches.contains(above))
                revealed.insert(above);
        }
        for (int i = 1; i <= after; ++i) {
            const int below = line + i;
            if (below < lineCount && !matches.contains(below))
                revealed.insert(below);
        }
    }
    QList<int> lines(revealed.begin(), revealed.end());
    std::sort(lines.begin(), lines.end());
    return lines;
}

void grayOutContentBefore(QTextCursor &startOfNewContent, const QPalette &palette)
{
    QTextDocument * const document = startOfNewContent.document();
    QTC_ASSERT(document, return);

    QTextCursor cursor(document);
    cursor.movePosition(QTextCursor::End);
    // What the end of the document is drawn in, kept so that the block opened
    // for the next run does not inherit the dimming applied below.
    const QTextCharFormat endFormat = cursor.charFormat();

    cursor.setPosition(startOfNewContent.position());
    cursor.movePosition(QTextCursor::End, QTextCursor::KeepAnchor, 1);

    // Halfway between the text and the background. Not StyleHelper's
    // mergedColors(), which halves each colour before adding and so differs by
    // up to one per channel - the point here is to move the operation, not to
    // change what it draws.
    const QColor background = palette.base().color();
    const QColor text = palette.text().color();
    const auto halfway = [](int a, int b) { return int(0.5 * a + 0.5 * b); };

    QTextCharFormat format;
    format.setForeground(QColor(halfway(background.red(), text.red()),
                                halfway(background.green(), text.green()),
                                halfway(background.blue(), text.blue())));
    cursor.mergeCharFormat(format);

    cursor.movePosition(QTextCursor::End);
    cursor.setCharFormat(endFormat);
    startOfNewContent.setPosition(cursor.position());
    cursor.insertBlock(QTextBlockFormat());
}

void removeLinesPrefixedWith(QTextDocument *doc, const QString &prefix,
                                           bool deleteTrailingLineBreak)
{
    QTC_ASSERT(doc, return);

    auto block = doc->lastBlock();
    while (true) {
        if (block.text().startsWith(prefix)) {
            QTextCursor c(block);
            c.select(QTextCursor::BlockUnderCursor);
            c.removeSelectedText();
            if (deleteTrailingLineBreak)
                c.deleteChar();
        }
        if (block == doc->firstBlock())
            break;
        block = block.previous();
    }
}

TextMatchingFunction filterPredicate(const QString &filterText,
                                                                 FilterModeFlags mode)
{
    // With no filter every line stays, whether or not the filter is inverted:
    // inverting "show everything" is still everything, not nothing.
    if (filterText.isEmpty())
        return [](const QString &) { return true; };

    // Past that, a line stays when it matches - or, inverted, when it does
    // not, which is what comparing the match against this says in one place
    // for both. The original also tested for an empty filter here; it cannot
    // be empty this far down, and a control that would not bite is what said
    // so.
    const bool normal = !mode.testFlag(FilterModeFlag::Inverted);

    if (mode.testFlag(FilterModeFlag::RegExp)) {
        QRegularExpression regExp(filterText);
        if (!mode.testFlag(FilterModeFlag::CaseSensitive))
            regExp.setPatternOptions(QRegularExpression::CaseInsensitiveOption);
        // A half-typed expression matches nothing rather than everything: the
        // pane empties as it is being typed and fills again when it is valid.
        if (!regExp.isValid())
            return [](const QString &) { return false; };

        return [regExp, normal](const QString &text) {
            return regExp.match(text).hasMatch() == normal;
        };
    }

    const auto cs = mode.testFlag(FilterModeFlag::CaseSensitive) ? Qt::CaseSensitive
                                                                 : Qt::CaseInsensitive;
    return [cs, filterText, normal](const QString &text) {
        return text.contains(filterText, cs) == normal;
    };
}

#ifdef WITH_TESTS

class OutputTextTest final : public QObject
{
    Q_OBJECT

private slots:
    // What an output pane hides while a filter is typed into it. This lived
    // inside a QPlainTextEdit, where the only way to ask it anything was to
    // type into a pane and look.
    // The lines a filter shows around what it matched. The widget expressed
    // this by asking the document for block numbers that may not exist and
    // letting the invalid ones do nothing, so neither end of the clamp was
    // visible to anything.
    void testWhatAFilterRevealsAroundAMatch()
    {
        // Nothing asked for, nothing revealed.
        QCOMPARE(contextLines({5}, 10, 0, 0), QList<int>());
        QCOMPARE(contextLines({}, 10, 2, 2), QList<int>());

        // One line either side, in order.
        QCOMPARE(contextLines({5}, 10, 1, 1), QList<int>({4, 6}));

        // Clamped at the top of the document and at the bottom: a match on the
        // first line reveals nothing above it, and one on the last nothing
        // below.
        QCOMPARE(contextLines({0}, 10, 2, 0), QList<int>());
        QCOMPARE(contextLines({9}, 10, 0, 2), QList<int>());
        QCOMPARE(contextLines({1}, 10, 2, 0), QList<int>({0}));

        // Overlapping context around neighbouring matches is one line, not two.
        QCOMPARE(contextLines({2, 4}, 10, 1, 1), QList<int>({1, 3, 5}));

        // And a match is never listed as its own context: it is already shown,
        // and saying so twice would hide the difference between the two.
        QCOMPARE(contextLines({2, 3}, 10, 1, 1), QList<int>({1, 4}));
    }

    // What an output pane does about its character limit. Both of these were
    // arithmetic inside a QPlainTextEdit: to see either you had to produce
    // megabytes of build output and watch.
    void testTheCharacterLimitKeepsBothEnds()
    {
        // Under the limit, nothing happens.
        QCOMPARE(elideChunk("short", 100), QString("short"));

        // Over it, both ends are kept and the middle is said to be gone.
        const QString chunk = QString("a").repeated(50) + QString("b").repeated(50);
        const QString elided = elideChunk(chunk, 20);
        QVERIFY2(elided.startsWith(QString("a").repeated(10)), qPrintable(elided.left(20)));
        QVERIFY2(elided.endsWith(QString("b").repeated(10)), qPrintable(elided.right(20)));
        QVERIFY(elided.contains("[[[..."));

        // And what it says went is what went: 100 characters in, 2 x 10 kept,
        // so 80 elided. Reporting size - limit would say 80 here and 81 for an
        // odd limit, which is the character the two halves lose to rounding.
        QVERIFY2(elided.contains("80"), qPrintable(elided));
        const QString oddLimit = elideChunk(chunk, 21);
        QVERIFY2(oddLimit.contains("80"), qPrintable(oddLimit));

        // Nothing to drop while it all still fits.
        QCOMPARE(blocksToKeep({10, 10}, 20, 5, 100), -1);

        // Otherwise the oldest blocks go, one at a time, until it does.
        QCOMPARE(blocksToKeep({10, 10, 10}, 30, 5, 25), 2);
        QCOMPARE(blocksToKeep({10, 10, 10}, 30, 5, 15), 1);

        // Never the last one: it is where the new text lands.
        QCOMPARE(blocksToKeep({10, 10, 10}, 30, 100, 5), 1);
    }

    // A Qt Quick view ignores QTextBlock::setVisible(), so a filtered view has
    // to be a filtered document. This is that document.
    void testAFilteredCopyKeepsWhatItShows()
    {
        QTextDocument source;
        QTextCursor writer(&source);
        writer.insertText("first: ordinary");
        writer.insertBlock();
        QTextCharFormat red;
        red.setForeground(Qt::red);
        writer.insertText("second: an error", red);
        writer.insertBlock();
        writer.insertText("third: ordinary");
        writer.insertBlock();
        writer.insertText("fourth: an error", red);
        QCOMPARE(source.blockCount(), 4);

        // Only the lines that match.
        QTextDocument filtered;
        copyFiltered(&source, &filtered,
                                   filterPredicate("error", {}), 0, 0);
        QCOMPARE(filtered.blockCount(), 2);
        QCOMPARE(filtered.findBlockByNumber(0).text(), QString("second: an error"));
        QCOMPARE(filtered.findBlockByNumber(1).text(), QString("fourth: an error"));

        // How they were drawn comes with them: a copy that kept only the text
        // would show an error in the colour of ordinary output.
        const QTextCharFormat format =
            filtered.findBlockByNumber(0).begin().fragment().charFormat();
        QCOMPARE(format.foreground().color(), QColor(Qt::red));

        // With context, the lines around a match come too, in order and once
        // each even where two matches ask for the same one.
        QTextDocument withContext;
        copyFiltered(&source, &withContext,
                                   filterPredicate("error", {}), 1, 0);
        QCOMPARE(withContext.blockCount(), 4);
        QCOMPARE(withContext.findBlockByNumber(0).text(), QString("first: ordinary"));
        QCOMPARE(withContext.findBlockByNumber(3).text(), QString("fourth: an error"));

        // An empty filter is the whole document, which is what a pane shows
        // when nothing is typed in its filter field.
        QTextDocument unfiltered;
        copyFiltered(&source, &unfiltered, filterPredicate({}, {}),
                                   0, 0);
        QCOMPARE(unfiltered.blockCount(), source.blockCount());

        // And what it costs, because whether this can be rebuilt on every
        // filter change or has to be maintained as output arrives is the
        // question it was written to answer.
        QTextDocument big;
        QTextCursor bigWriter(&big);
        for (int i = 0; i < 10000; ++i) {
            if (i > 0)
                bigWriter.insertBlock();
            bigWriter.insertText(QString("line %1 of build output").arg(i), i % 5 ? QTextCharFormat() : red);
        }
        QTextDocument bigFiltered;
        QElapsedTimer timer;
        timer.start();
        copyFiltered(&big, &bigFiltered,
                                   filterPredicate("of build", {}), 0, 0);
        const qint64 elapsed = timer.elapsed();
        QCOMPARE(bigFiltered.blockCount(), 10000);
        qInfo() << "filtering 10000 lines took" << elapsed << "ms";
    }

    // Output arrives a chunk at a time, and a build appends thousands of
    // times, so the filtered document is appended to rather than rebuilt. What
    // it must not do is disagree with a rebuild - and the case that would make
    // it is a line that arrives later making an earlier skipped one into
    // context.
    void testAppendingLineByLineAgreesWithFilteringAtOnce()
    {
        const QStringList lines = {
            "starting up",              // no match
            "error: first failure",     // match
            "some detail",              // no match
            "more detail",              // no match
            "error: second failure",    // match
            "error: third failure",     // match, straight after another
            "trailing one",             // no match
            "trailing two",             // no match
        };

        struct Case { const char *filter; int before; int after; };
        const QList<Case> cases = {
            {"", 0, 0},
            {"error", 0, 0},
            {"error", 1, 0},
            {"error", 0, 1},
            {"error", 2, 2},
            {"nothing matches this", 2, 2},
        };

        for (const Case &c : cases) {
            const auto matches = filterPredicate(QString::fromLatin1(c.filter), {});

            QTextDocument whole;
            QTextCursor writer(&whole);
            for (int i = 0; i < lines.size(); ++i) {
                if (i > 0)
                    writer.insertBlock();
                writer.insertText(lines.at(i));
            }

            QTextDocument atOnce;
            copyFiltered(&whole, &atOnce, matches, c.before, c.after);

            // The same lines, but arriving one at a time - and arriving the way
            // an OutputFormatter delivers them, as text followed by a newline.
            // Written the other way round, block first and then text, no line
            // is ever the last one while it is being written, and an
            // incremental filter that mishandles exactly that looks correct.
            QTextDocument source;
            QTextDocument incremental;
            QTextCursor appender(&source);
            FilteredAppendState state;
            for (const QString &line : lines) {
                appender.insertText(line);
                appender.insertBlock();
                appendFiltered(&source, &incremental, matches, c.before, c.after,
                                             state);
            }

            QCOMPARE(source.toPlainText(), whole.toPlainText() + '\n');
            QVERIFY2(incremental.toPlainText() == atOnce.toPlainText(),
                     qPrintable(QString("filter \"%1\" (-%2/+%3):\n  line by line: %4\n  at once:      %5")
                                    .arg(QString::fromLatin1(c.filter))
                                    .arg(c.before).arg(c.after)
                                    .arg(incremental.toPlainText().replace('\n', '|'),
                                         atOnce.toPlainText().replace('\n', '|'))));
        }
    }

    void testWhatAFilterLetsThrough()
    {
        using Flags = FilterModeFlags;
        using Flag = FilterModeFlag;

        // No filter: everything stays, which is not the same as "matches
        // nothing" and is what an empty field has to mean.
        const auto none = filterPredicate({}, {});
        QVERIFY(none("anything at all"));
        QVERIFY(none({}));

        // Plain text, and case-blind unless asked.
        const auto plain = filterPredicate("error", {});
        QVERIFY(plain("an ERROR happened"));
        QVERIFY(!plain("all good"));
        const auto cased = filterPredicate("error", Flags(Flag::CaseSensitive));
        QVERIFY(!cased("an ERROR happened"));
        QVERIFY(cased("an error happened"));

        // Inverted keeps what does not match - and an empty filter is not
        // inverted into hiding everything, which the early return is what
        // guarantees.
        const auto inverted = filterPredicate("error", Flags(Flag::Inverted));
        QVERIFY(!inverted("an error happened"));
        QVERIFY(inverted("all good"));
        const auto invertedEmpty = filterPredicate({}, Flags(Flag::Inverted));
        QVERIFY2(invertedEmpty("anything at all"),
                 "an empty inverted filter hid every line");

        // A regular expression, case-blind unless asked.
        const auto regexp = filterPredicate("e[rd]{2}or", Flags(Flag::RegExp));
        QVERIFY(regexp("an errorish line"));
        QVERIFY(regexp("an ERROR line"));
        QVERIFY(!regexp("all good"));

        // And a half-typed one matches nothing rather than everything: the
        // pane empties as it is typed and fills again when it is valid.
        const auto halfTyped = filterPredicate("error(", Flags(Flag::RegExp));
        QVERIFY2(!halfTyped("an error happened"),
                 "an unfinished regular expression let every line through");
    }

    // Dimming what a previous run left. The widget did this through a cursor
    // it kept and its own palette, so nothing could ask it anything.
    void testDimmingWhatTheLastRunLeft()
    {
        QPalette palette;
        palette.setColor(QPalette::Base, Qt::black);
        palette.setColor(QPalette::Text, Qt::white);
        const QColor dimmed(127, 127, 127); // Halfway, as the widget drew it.

        QTextDocument document;
        QTextCursor startOfNewContent(&document);
        startOfNewContent.setKeepPositionOnInsert(true);

        // Filled the way a pane fills it, so that what the formatter does to
        // the character formats is part of the test rather than assumed.
        Utils::OutputFormatter formatter;
        formatter.setSink(&document);
        const auto append = [&formatter](const QString &text) {
            formatter.appendMessage(text, Utils::NormalMessageFormat);
            formatter.flush();
        };
        const auto colorOf = [&document](int position) {
            QTextCursor cursor(&document);
            cursor.setPosition(position + 1);
            return cursor.charFormat().foreground().color();
        };

        append("first run\n");
        const int firstRun = 0;
        const QColor undimmed = colorOf(firstRun);
        QVERIFY2(undimmed != dimmed, "the fixture starts out already dimmed");

        grayOutContentBefore(startOfNewContent, palette);
        QCOMPARE(colorOf(firstRun), dimmed);

        // The document is left ready for output that is not dimmed. Without
        // the end format being put back, the block opened here carries the
        // dimming, and anything written into it that does not name its own
        // colour - which is not the formatter, but is every plain insert -
        // comes out grey.
        QTextCursor atEnd(&document);
        atEnd.movePosition(QTextCursor::End);
        QCOMPARE(atEnd.charFormat().foreground().color(), undimmed);

        const int secondRun = document.characterCount() - 1;
        append("second run\n");
        QCOMPARE(colorOf(secondRun), undimmed);

        // And dimming again takes the second run and leaves the first alone,
        // rather than dimming what is already dim a second time.
        grayOutContentBefore(startOfNewContent, palette);
        QCOMPARE(colorOf(secondRun), dimmed);
        QCOMPARE(colorOf(firstRun), dimmed);

        QVERIFY2(document.toPlainText().contains("first run")
                     && document.toPlainText().contains("second run"),
                 "dimming a run removed it");
    }

    // Where a chunk of output is cut so it can be written a piece at a time.
    void testABigChunkIsCutAtALineEnd()
    {
        // Short enough to go in one piece.
        QCOMPARE(chunkEndPosition("one line\n", 100), 9);

        // Cut back to just after the last line end, so a whole line goes.
        QCOMPARE(chunkEndPosition("aaa\nbbb\nccc", 8), 8);
        QCOMPARE(chunkEndPosition("aaa\nbbbbbbbb", 8), 4);

        // A line end further back than a thousand characters is not worth
        // waiting for: the cut is where it was asked for. Cutting through an
        // escape code shows it as text, but holding a thousand characters back
        // to avoid it would stall the output.
        const QString noLineEndNearby = "x\n" + QString(3000, 'y');
        QCOMPARE(chunkEndPosition(noLineEndNearby, 2500), 2500);

        // And with no line end at all it is a plain cut.
        QCOMPARE(chunkEndPosition(QString(50, 'z'), 20), 20);
    }

    // How fast output is written out, adjusted by how long the last chunk took
    // to format. Inside the widget this could only be reached by producing
    // output at a rate that made the formatter slow.
    void testTheOutputPaceFollowsWhatTheFormatterCanDo()
    {
        using namespace std::chrono_literals;
        using Pacing = OutputPacing;

        // Slower than the interval: back off. Half the chunk, twice the wait.
        const Pacing slow = pacedBy({10ms, 10000}, 50ms, false);
        QCOMPARE(slow.interval, 20ms);
        QCOMPARE(slow.chunkSize, 5000);

        // Comfortably faster, and only while there is more of the same chunk
        // to write: speed up.
        const Pacing fast = pacedBy({30ms, 10000}, 5ms, true);
        QCOMPARE(fast.interval, 20ms);
        QCOMPARE(fast.chunkSize, 15000);

        // Fast but the chunk is finished: nothing to hurry for.
        QCOMPARE(pacedBy({30ms, 10000}, 5ms, false).interval, 30ms);

        // In between - faster than the interval but not by half - is left
        // alone, so ordinary output does not make the pane oscillate.
        QCOMPARE(pacedBy({30ms, 10000}, 20ms, true).chunkSize, 10000);

        // Neither end runs away: the wait stops at a second and the chunk does
        // not shrink below a thousand characters, or a pane that fell behind
        // once would never catch up.
        QCOMPARE(pacedBy({1000ms, 10000}, 5000ms, false).interval, 1000ms);
        QCOMPARE(pacedBy({10ms, 1000}, 50ms, false).chunkSize, 1000);
        QCOMPARE(pacedBy({1ms, 10000}, 0ms, true).interval, 1ms);
    }

    // When output arriving faster than it can be drawn is thrown away. Inside
    // the widget the only way to reach this was to flood a real build.
    void testWhenPendingOutputIsThrownAway()
    {
        using namespace std::chrono_literals;
        const qsizetype chunk = 1000;

        // A backlog that keeps growing, and is large, is a flood. Ten samples
        // are watched before deciding, so a burst is not mistaken for one.
        PendingOutputState growing;
        for (int i = 1; i <= 10; ++i) {
            QVERIFY2(!shouldDiscardPendingOutput(growing, i * chunk, chunk, 0, 10ms),
                     qPrintable(QString("gave up after only %1 samples").arg(i)));
        }
        QVERIFY(shouldDiscardPendingOutput(growing, 11 * chunk, chunk, 0, 10ms));

        // Growing but small is not: a pane with a little behind it is being
        // kept up with.
        PendingOutputState small;
        for (int i = 1; i <= 20; ++i)
            QVERIFY(!shouldDiscardPendingOutput(small, chunk, chunk, 0, 10ms));

        // And a backlog that shrinks even once starts the count again, because
        // it is being kept up with after all.
        PendingOutputState recovering;
        for (int i = 1; i <= 10; ++i)
            shouldDiscardPendingOutput(recovering, i * chunk, chunk, 0, 10ms);
        QVERIFY(!shouldDiscardPendingOutput(recovering, chunk, chunk, 0, 10ms));
        QVERIFY2(!shouldDiscardPendingOutput(recovering, 11 * chunk, chunk, 0, 10ms),
                 "a backlog that had recovered was discarded on the next sample");

        // The other reason: writing what is waiting would take over a minute.
        // 7000 chunks at 10ms each is about seventy seconds.
        PendingOutputState slow;
        QVERIFY(shouldDiscardPendingOutput(slow, 7000 * chunk, chunk, 10, 10ms));

        // But not before anything has been shown: a pane that has drawn almost
        // nothing yet must not start by announcing that it gave up.
        PendingOutputState slowButNew;
        QVERIFY2(!shouldDiscardPendingOutput(slowButNew, 7000 * chunk, chunk, 9, 10ms),
                 "output was discarded before the pane had shown anything");

        // Just under a minute is not over it.
        PendingOutputState almost;
        QVERIFY(!shouldDiscardPendingOutput(almost, 5000 * chunk, chunk, 10, 10ms));
    }

    // Which lines of the document a task's output occupies. The widget worked
    // this out inside itself from its own block count, so the only way to ask
    // it anything was to run a build and click a task in the Issues pane.
    void testWhichLinesATaskWasReportedFrom()
    {
        // A parser reads output that is already written, so nothing is owed:
        // five blocks, the last of them the empty one the trailing newline
        // leaves, and a task covering the two lines before it.
        QCOMPARE(taskLineRange(5, 2, 0, 0, 0), qMakePair(2, 3));

        // Skipped lines sit between the task's output and the end.
        QCOMPARE(taskLineRange(5, 2, 1, 0, 0), qMakePair(1, 2));

        // The offset walks back over tasks already placed, which is how
        // several found in one chunk end up on different lines.
        QCOMPARE(taskLineRange(5, 1, 0, 0, 0), qMakePair(3, 3));
        QCOMPARE(taskLineRange(5, 1, 0, 1, 0), qMakePair(2, 2));
        QCOMPARE(taskLineRange(5, 1, 0, 2, 0), qMakePair(1, 1));

        // And a task reported directly names output still queued, so its lines
        // are where they will be once that is written, not where the end is
        // now. Getting this wrong points the task at the wrong lines only when
        // output is arriving fast enough to be queued.
        QCOMPARE(taskLineRange(5, 1, 0, 0, 4), qMakePair(7, 7));
    }

    // Retracting lines a pane has already printed - the progress lines a build
    // system overwrites as it goes.
    void testRemovingTheLinesAPaneTakesBack()
    {
        const auto textAfterRemoving = [](const QString &start, bool deleteTrailingLineBreak) {
            QTextDocument document;
            document.setPlainText(start);
            removeLinesPrefixedWith(&document, "Progress:", deleteTrailingLineBreak);
            return document.toPlainText();
        };

        // Every line with the prefix goes, wherever it is, and the rest stays
        // in order.
        QCOMPARE(textAfterRemoving("Progress: 1\nkept\n", true), QString("kept\n"));
        QCOMPARE(textAfterRemoving("kept\nkept two\n", true), QString("kept\nkept two\n"));

        // Only at the start of a line. A line that merely mentions the prefix
        // is output like any other.
        QCOMPARE(textAfterRemoving("says Progress: here\n", true),
                 QString("says Progress: here\n"));

        // Whether the line break goes with it is the caller's choice, and it
        // is the difference between retracting a line and blanking it.
        QCOMPARE(textAfterRemoving("Progress: only\n", true), QString(""));
        QCOMPARE(textAfterRemoving("Progress: only\n", false), QString("\n"));
        QCOMPARE(textAfterRemoving("Progress: 1\nkept\n", false), QString("\nkept\n"));

        // Removing the last line of output takes the empty block a trailing
        // newline leaves behind with it, so the document ends without one.
        // Surprising, and what the panes have always done.
        QCOMPARE(textAfterRemoving("Progress: 1\nkept\nProgress: 2\n", true), QString("kept"));
    }
};

QObject *createOutputTextTest()
{
    return new OutputTextTest;
}

#endif // WITH_TESTS

} // namespace Core::OutputText

#ifdef WITH_TESTS
#include "outputtext.moc"
#endif
