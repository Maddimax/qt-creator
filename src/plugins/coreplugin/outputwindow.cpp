// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "outputwindow.h"

#include "actionmanager/actionmanager.h"
#include "coreconstants.h"
#include "coreplugintr.h"
#include "editormanager/editormanager.h"
#include "find/basetextfind.h"
#include "icore.h"
#include "messagemanager.h"

#include <utils/filedialogs.h>
#include <utils/aggregate.h>
#include <utils/algorithm.h>
#include <utils/fileutils.h>
#include <utils/outputformatter.h>
#include <utils/qtcassert.h>
#include <utils/theme/theme.h>

#include <QAction>
#include <QCursor>
#include <QElapsedTimer>
#include <QHash>
#include <QLoggingCategory>
#include <QMenu>
#include <QMimeData>
#include <QPair>
#include <QPointer>
#include <QRegularExpression>
#include <QScrollBar>
#include <QSyntaxHighlighter>
#include <QTextBlock>
#include <QTimer>

#ifdef WITH_TESTS
#include <QTest>
#endif

#include <numeric>

#ifdef WITH_TESTS
#include <QTest>
#endif

using namespace Utils;
using namespace std::chrono_literals;

const qsizetype defaultChunkSize = 10000;
const qsizetype minChunkSize = 1000;

const auto defaultInterval = 10ms;
const auto maxInterval = 1000ms;

static Q_LOGGING_CATEGORY(chunkLog, "qtc.core.outputChunking", QtWarningMsg)
static Q_LOGGING_CATEGORY(filterLog, "qtc.core.outputFiltering", QtWarningMsg)

namespace Core {
namespace Internal {

class OutputWindowPrivate
{
public:
    explicit OutputWindowPrivate(QTextDocument *document)
        : startOfNewContentCursor(document)
        , cursor(document)
    {
        startOfNewContentCursor.setKeepPositionOnInsert(true);
    }

    qsizetype totalQueuedValue(const std::function<qsizetype(const QString &)> &getValue) const
    {
        return std::accumulate(
            queuedOutput.cbegin(),
            queuedOutput.cend(),
            0,
            [&](qsizetype val, const QPair<QString, OutputFormat> &c) {
                return val + getValue(c.first);
            });
    }

    //: default file name suggested for saving text from output views
    QString outputFileNameHint{::Core::Tr::tr("output.txt")};

    Key settingsKey;
    OutputFormatter formatter;
    QList<QPair<QString, OutputFormat>> queuedOutput;
    QTimer queueTimer;
    QList<qsizetype> queuedSizeHistory;
    int formatterCalls = 0;
    bool discardExcessiveOutput = false;

    bool flushRequested = false;
    bool scrollToBottom = true;
    bool linksActive = true;
    bool zoomEnabled = false;
    float originalFontSize = 0.;
    bool originalReadOnly = false;
    qsizetype maxCharCount = Core::Constants::DEFAULT_MAX_CHAR_COUNT;
    Qt::MouseButton mouseButtonPressed = Qt::NoButton;
    QTextCursor startOfNewContentCursor;
    QTextCursor cursor;
    QString filterText;
    QTextBlock lastFilteredBlock;
    qsizetype chunkSize = defaultChunkSize;
    QPalette originalPalette;
    OutputWindow::FilterModeFlags filterMode = OutputWindow::FilterModeFlag::Default;
    int beforeContext = 0;
    int afterContext = 0;
    QTimer scrollTimer;
    QElapsedTimer lastMessage;
    QHash<unsigned int, QPair<int, int>> taskPositions;
    IFindSupport *findSupport = nullptr;
};

} // namespace Internal

/*******************/

OutputWindow::OutputWindow(Context context, const Key &settingsKey, QWidget *parent)
    : OutputWindow(context, settingsKey, /*aggregateFindSupport=*/true, parent)
{}

OutputWindow::OutputWindow(
    Context context, const Key &settingsKey, bool aggregateFindSupport, QWidget *parent)
    : QPlainTextEdit(parent)
    , d(new Internal::OutputWindowPrivate(document()))
{
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    //setCenterOnScroll(false);
    setFrameShape(QFrame::NoFrame);
    setMouseTracking(true);
    setUndoRedoEnabled(false);
    d->formatter.setSink(document(), this);

    d->queueTimer.setSingleShot(true);
    d->queueTimer.setInterval(defaultInterval);
    connect(&d->queueTimer, &QTimer::timeout, this, &OutputWindow::handleNextOutputChunk);

    d->settingsKey = settingsKey;

    IContext::attach(this, context);

    auto undoAction = new QAction(this);
    auto redoAction = new QAction(this);
    auto cutAction = new QAction(this);
    auto copyAction = new QAction(this);
    auto pasteAction = new QAction(this);
    auto selectAllAction = new QAction(this);

    ActionManager::registerAction(undoAction, Constants::UNDO, context);
    ActionManager::registerAction(redoAction, Constants::REDO, context);
    ActionManager::registerAction(cutAction, Constants::CUT, context);
    ActionManager::registerAction(copyAction, Constants::COPY, context);
    ActionManager::registerAction(pasteAction, Constants::PASTE, context);
    ActionManager::registerAction(selectAllAction, Constants::SELECTALL, context);

    connect(undoAction, &QAction::triggered, this, &QPlainTextEdit::undo);
    connect(redoAction, &QAction::triggered, this, &QPlainTextEdit::redo);
    connect(cutAction, &QAction::triggered, this, &QPlainTextEdit::cut);
    connect(copyAction, &QAction::triggered, this, &QPlainTextEdit::copy);
    connect(pasteAction, &QAction::triggered, this, &QPlainTextEdit::paste);
    connect(selectAllAction, &QAction::triggered, this, &QPlainTextEdit::selectAll);
    connect(this, &QPlainTextEdit::blockCountChanged, this, [this] {
        if (shouldFilterNewContentOnBlockCountChanged())
            filterNewContent();
    });

    connect(this, &QPlainTextEdit::undoAvailable, undoAction, &QAction::setEnabled);
    connect(this, &QPlainTextEdit::redoAvailable, redoAction, &QAction::setEnabled);
    connect(this, &QPlainTextEdit::copyAvailable, cutAction, &QAction::setEnabled);  // OutputWindow never read-only
    connect(this, &QPlainTextEdit::copyAvailable, copyAction, &QAction::setEnabled);
    connect(Core::ICore::instance(), &Core::ICore::saveSettingsRequested, this, [this] {
        if (!d->settingsKey.isEmpty())
            Core::ICore::settings()->setValueWithDefault(d->settingsKey, fontZoom(), 0.f);
    });

    connect(outputFormatter(), &OutputFormatter::openInEditorRequested, this, [](const Link &link) {
        EditorManager::openEditorAt(link);
    });

    connect(verticalScrollBar(), &QAbstractSlider::actionTriggered,
            this, &OutputWindow::updateAutoScroll);

    // For when "Find" changes the position; see QTCREATORBUG-26100.
    connect(this, &QPlainTextEdit::selectionChanged, this, &OutputWindow::updateAutoScroll,
            Qt::QueuedConnection);

    undoAction->setEnabled(false);
    redoAction->setEnabled(false);
    cutAction->setEnabled(false);
    copyAction->setEnabled(false);

    d->scrollTimer.setInterval(10ms);
    d->scrollTimer.setSingleShot(true);
    connect(&d->scrollTimer, &QTimer::timeout,
            this, &OutputWindow::scrollToBottom);
    d->lastMessage.start();

    d->originalFontSize = font().pointSizeF();

    if (!d->settingsKey.isEmpty()) {
        float zoom = Core::ICore::settings()->value(d->settingsKey).toFloat();
        setFontZoom(zoom);
    }

    // Let selected text be colored as if the text edit was editable,
    // otherwise the highlight for searching is too light
    QPalette p = palette();
    QColor activeHighlight = p.color(QPalette::Active, QPalette::Highlight);
    p.setColor(QPalette::Highlight, activeHighlight);
    QColor activeHighlightedText = p.color(QPalette::Active, QPalette::HighlightedText);
    p.setColor(QPalette::HighlightedText, activeHighlightedText);
    setPalette(p);

    d->findSupport = new BaseTextFind(this);
    if (aggregateFindSupport)
        Aggregation::aggregate({this, d->findSupport});
    else
        d->findSupport->setParent(this);
}

OutputWindow::~OutputWindow()
{
    delete d;
}

IFindSupport *OutputWindow::findSupport() const
{
    return d->findSupport;
}

void OutputWindow::mousePressEvent(QMouseEvent *e)
{
    d->mouseButtonPressed = e->button();
    QPlainTextEdit::mousePressEvent(e);
}

void OutputWindow::handleLink(const QPoint &pos)
{
    const QString href = anchorAt(pos);
    if (!href.isEmpty())
        d->formatter.handleLink(href);
}

void OutputWindow::adaptContextMenu(QMenu *, const QPoint &) {}

void OutputWindow::resetLastFilteredBlockNumber()
{
    d->lastFilteredBlock = {};
}

bool OutputWindow::shouldFilterNewContentOnBlockCountChanged() const
{
    return !d->filterText.isEmpty();
}

void OutputWindow::mouseReleaseEvent(QMouseEvent *e)
{
    if (d->linksActive && d->mouseButtonPressed == Qt::LeftButton)
        handleLink(e->pos());

    // Mouse was released, activate links again
    d->linksActive = true;
    d->mouseButtonPressed = Qt::NoButton;

    QPlainTextEdit::mouseReleaseEvent(e);
}

void OutputWindow::mouseMoveEvent(QMouseEvent *e)
{
    // Cursor was dragged to make a selection, deactivate links
    if (d->mouseButtonPressed != Qt::NoButton && textCursor().hasSelection())
        d->linksActive = false;

    if (!d->linksActive || anchorAt(e->pos()).isEmpty())
        viewport()->setCursor(Qt::IBeamCursor);
    else
        viewport()->setCursor(Qt::PointingHandCursor);
    QPlainTextEdit::mouseMoveEvent(e);
}

void OutputWindow::resizeEvent(QResizeEvent *e)
{
    //Keep scrollbar at bottom of window while resizing, to ensure we keep scrolling
    //This can happen if window is resized while building, or if the horizontal scrollbar appears
    QPlainTextEdit::resizeEvent(e);
    if (d->scrollToBottom)
        scrollToBottom();
}

void OutputWindow::keyPressEvent(QKeyEvent *ev)
{
    QPlainTextEdit::keyPressEvent(ev);

    //Ensure we scroll also on Ctrl+Home or Ctrl+End
    if (ev->matches(QKeySequence::MoveToStartOfDocument))
        verticalScrollBar()->triggerAction(QAbstractSlider::SliderToMinimum);
    else if (ev->matches(QKeySequence::MoveToEndOfDocument))
        verticalScrollBar()->triggerAction(QAbstractSlider::SliderToMaximum);
}

void OutputWindow::setLineParsers(const QList<OutputLineParser *> &parsers)
{
    reset();
    d->formatter.setLineParsers(parsers);
}

OutputFormatter *OutputWindow::outputFormatter() const
{
    return &d->formatter;
}

void OutputWindow::showEvent(QShowEvent *e)
{
    QPlainTextEdit::showEvent(e);
    if (d->scrollToBottom)
        scrollToBottom();
}

void OutputWindow::wheelEvent(QWheelEvent *e)
{
    if (d->zoomEnabled) {
        if (e->modifiers() & Qt::ControlModifier) {
            float delta = e->angleDelta().y() / 120.f;

            // Workaround for QTCREATORBUG-22721, remove when properly fixed in Qt
            const float newSize = float(font().pointSizeF()) + delta;
            if (delta < 0.f && newSize < 4.f)
                return;

            zoomInF(delta);
            emit wheelZoom();
            return;
        }
    }
    QAbstractScrollArea::wheelEvent(e);
    updateAutoScroll();
    updateMicroFocus();
}

void OutputWindow::contextMenuEvent(QContextMenuEvent *event)
{
    QMenu *menu = createStandardContextMenu(event->pos());
    menu->setAttribute(Qt::WA_DeleteOnClose);

    adaptContextMenu(menu, event->pos());

    menu->addSeparator();
    QAction *saveAction = menu->addAction(Tr::tr("Save Contents..."));
    connect(saveAction, &QAction::triggered, this, [this] {
        const FilePath file = FileUtils::getSaveFilePath(
            {}, FileUtils::homePath() / d->outputFileNameHint);
        if (!file.isEmpty()) {
            TextFileFormat format;
            format.setEncoding(EditorManager::defaultTextEncoding());
            format.lineTerminationMode = EditorManager::defaultLineEnding();
            if (const Result<> res = format.writeFile(file, toPlainText()); !res)
                MessageManager::writeDisrupting(res.error());
        }
    });
    saveAction->setEnabled(!document()->isEmpty());
    QAction *openAction = menu->addAction(Tr::tr("Copy Contents to Scratch Buffer"));
    connect(openAction, &QAction::triggered, this, [this] {
        QString scratchBufferPrefix = FilePath::fromString(d->outputFileNameHint).baseName();
        if (scratchBufferPrefix.isEmpty())
            scratchBufferPrefix = "scratch";
        const auto tempPath = FileUtils::scratchBufferFilePath(
            QString::fromUtf8("%1-XXXXXX.txt").arg(scratchBufferPrefix));
        if (!tempPath) {
            MessageManager::writeDisrupting(tempPath.error());
            return;
        }
        IEditor * const editor = EditorManager::openEditor(*tempPath);
        if (!editor) {
            MessageManager::writeDisrupting(
                Tr::tr("Failed to open editor for \"%1\".").arg(tempPath->toUserOutput()));
            return;
        }
        editor->document()->setTemporary(true);
        editor->document()->setContents(toPlainText().toUtf8());
    });
    openAction->setEnabled(!document()->isEmpty());

    menu->addSeparator();
    QAction *clearAction = menu->addAction(Tr::tr("Clear"));
    connect(clearAction, &QAction::triggered, this, [this] { clear(); });
    clearAction->setEnabled(!document()->isEmpty());

    menu->popup(event->globalPos());
}

void OutputWindow::setBaseFont(const QFont &newFont)
{
    float zoom = fontZoom();
    d->originalFontSize = newFont.pointSizeF();
    QFont tmp = newFont;
    float newZoom = qMax(d->originalFontSize + zoom, 4.0f);
    tmp.setPointSizeF(newZoom);
    setFont(tmp);
}

float OutputWindow::fontZoom() const
{
    return font().pointSizeF() - d->originalFontSize;
}

void OutputWindow::setFontZoom(float zoom)
{
    QFont f = font();
    if (f.pointSizeF() == d->originalFontSize + zoom)
        return;
    float newZoom = qMax(d->originalFontSize + zoom, 4.0f);
    f.setPointSizeF(newZoom);
    setFont(f);
}

void OutputWindow::setWheelZoomEnabled(bool enabled)
{
    d->zoomEnabled = enabled;
}

bool OutputWindow::updateFilterProperties(
        const QString &filterText,
        Qt::CaseSensitivity caseSensitivity,
        bool isRegexp,
        bool isInverted,
        int beforeContext,
        int afterContext
        )
{
    FilterModeFlags flags;
    flags.setFlag(FilterModeFlag::CaseSensitive, caseSensitivity == Qt::CaseSensitive)
            .setFlag(FilterModeFlag::RegExp, isRegexp)
            .setFlag(FilterModeFlag::Inverted, isInverted);
    if (d->filterMode == flags
        && d->filterText == filterText
        && d->beforeContext == beforeContext
        && d->afterContext == afterContext)
        return false;
    resetLastFilteredBlockNumber();
    if (d->filterText != filterText) {
        const bool filterTextWasEmpty = d->filterText.isEmpty();
        d->filterText = filterText;

        // Update textedit's background color
        if (filterText.isEmpty() && !filterTextWasEmpty) {
            setPalette(d->originalPalette);
            setReadOnly(d->originalReadOnly);
        }
        if (!filterText.isEmpty() && filterTextWasEmpty) {
            d->originalReadOnly = isReadOnly();
            setReadOnly(true);
            const auto newBgColor = [this] {
                const QColor currentColor = palette().color(QPalette::Base);
                const int factor = 120;
                return currentColor.value() < 128 ? currentColor.lighter(factor)
                                                  : currentColor.darker(factor);
            };
            QPalette p = palette();
            p.setColor(QPalette::Base, newBgColor());
            setPalette(p);
        }
    }
    d->filterMode = flags;
    d->beforeContext = beforeContext;
    d->afterContext = afterContext;
    filterNewContent();
    return true;
}

void OutputWindow::setOutputFileNameHint(const QString &fileName)
{
    d->outputFileNameHint = fileName;
}

OutputWindow::TextMatchingFunction OutputWindow::filterPredicate(const QString &filterText,
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

void OutputWindow::copyFiltered(const QTextDocument *source, QTextDocument *target,
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

void OutputWindow::appendFiltered(const QTextDocument *source, QTextDocument *target,
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

    for (int number = state.lastConsidered + 1; number < source->blockCount(); ++number) {
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

QString OutputWindow::elideChunk(const QString &chunk, qsizetype maxCharCount)
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

int OutputWindow::blocksToKeep(const QList<int> &blockLengths, qsizetype existingChars,
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

QList<int> OutputWindow::contextLines(const QList<int> &matchedLines, int lineCount,
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

OutputWindow::TextMatchingFunction OutputWindow::makeMatchingFilterFunction() const
{
    return filterPredicate(d->filterText, d->filterMode);
}

void OutputWindow::filterNewContent()
{
    qCDebug(filterLog) << "filtering new content, last filtered block was"
                       << d->lastFilteredBlock.blockNumber();

    const auto findNextMatchFilter = makeMatchingFilterFunction();
    QTC_ASSERT(findNextMatchFilter, return);
    QTextBlock lastBlock = d->lastFilteredBlock;
    const int requiredBacklog = std::max(d->beforeContext, d->afterContext);
    for (int i = 0; i < requiredBacklog && lastBlock.isValid(); ++i)
        lastBlock = lastBlock.previous();
    if (!lastBlock.isValid())
        lastBlock = document()->begin();
    std::vector<int> matchedBlocks;

    qCDebug(filterLog) << "starting to filter at block" << lastBlock.blockNumber();

    // Find matching text blocks for the current filter.
    for (; lastBlock != document()->end(); lastBlock = lastBlock.next()) {
        const bool isMatch = findNextMatchFilter(lastBlock.text());

        if (isMatch)
            matchedBlocks.emplace_back(lastBlock.blockNumber());

        lastBlock.setVisible(isMatch);
    }

    // Reveal the context lines before and after the match.
    if (!d->filterText.isEmpty()) {
        const QList<int> matched(matchedBlocks.begin(), matchedBlocks.end());
        const QList<int> context = contextLines(matched, document()->blockCount(),
                                                d->beforeContext, d->afterContext);
        for (const int blockNumber : context)
            document()->findBlockByNumber(blockNumber).setVisible(true);
    }

    d->lastFilteredBlock = document()->lastBlock();

    // FIXME: Why on earth is this necessary? We should probably do something else instead...
    setDocument(document());

    if (d->scrollToBottom)
        scrollToBottom();
}

void OutputWindow::handleNextOutputChunk()
{
    QTC_ASSERT(!d->queuedOutput.isEmpty(), return);

    discardExcessiveOutput();
    if (d->queuedOutput.isEmpty())
        return;

    auto &chunk = d->queuedOutput.first();

    // We want to break off the chunks along line breaks, if possible.
    // Otherwise we can get ugly temporary artifacts e.g. for ANSI escape codes.
    qsizetype actualChunkSize = std::min(d->chunkSize, chunk.first.size());
    const qsizetype minEndPos = std::max(qsizetype(0), actualChunkSize - 1000);
    for (int i = actualChunkSize - 1; i >= minEndPos; --i) {
        if (chunk.first.at(i) == '\n') {
            actualChunkSize = i + 1;
            break;
        }
    }

    qCDebug(chunkLog) << "next queued chunk has" << chunk.first.size() << "bytes";
    if (actualChunkSize == chunk.first.size()) {
        qCDebug(chunkLog) << "chunk can be written in one go";
        handleOutputChunk(chunk.first, chunk.second, ChunkCompleteness::Complete);
        d->queuedOutput.removeFirst();
    } else {
        qCDebug(chunkLog) << "chunk needs to be split";
        handleOutputChunk(chunk.first.left(actualChunkSize), chunk.second, ChunkCompleteness::Split);
        chunk.first.remove(0, actualChunkSize);
    }
    if (!d->queuedOutput.isEmpty())
        d->queueTimer.start();
    else if (d->flushRequested) {
        d->formatter.flush();
        d->flushRequested = false;
    }
}

void OutputWindow::handleOutputChunk(
    const QString &output, OutputFormat format, ChunkCompleteness completeness)
{
    QString out = output;
    int maxBlockCount = -1;
    if (out.size() > d->maxCharCount) {
        // Current chunk alone exceeds limit, we need to cut it.
        out = elideChunk(out, d->maxCharCount);
        maxBlockCount = out.count('\n') + 1;
    } else {
        QList<int> blockLengths;
        for (QTextBlock tb = document()->firstBlock(); tb.isValid(); tb = tb.next())
            blockLengths << tb.length();
        maxBlockCount = blocksToKeep(blockLengths, document()->characterCount(), out.size(),
                                     d->maxCharCount);
    }
    qCDebug(chunkLog) << "new max block count:" << maxBlockCount;
    setMaximumBlockCount(maxBlockCount);

    const int oldBlockCount = document()->blockCount();
    const QTextBlock oldLastBlock = document()->lastBlock();
    const int oldBlockLength = oldLastBlock.length();

    QElapsedTimer formatterTimer;
    formatterTimer.start();
    d->formatter.appendMessage(out, format);
    ++d->formatterCalls;
    qCDebug(chunkLog) << "formatter took" << formatterTimer.elapsed() << "ms";
    if (formatterTimer.elapsed() > d->queueTimer.interval()) {
        d->queueTimer.setInterval(std::min(maxInterval, d->queueTimer.intervalAsDuration() * 2));
        d->chunkSize = std::max(minChunkSize, d->chunkSize / 2);
        qCDebug(chunkLog) << "increasing interval to" << d->queueTimer.interval()
                          << "ms and lowering chunk size to" << d->chunkSize << "bytes";
    } else if (completeness == ChunkCompleteness::Split
               && formatterTimer.elapsed() < d->queueTimer.interval() / 2) {
        d->queueTimer.setInterval(std::max(1ms, d->queueTimer.intervalAsDuration() * 2 / 3));
        d->chunkSize = d->chunkSize * 1.5;
        qCDebug(chunkLog) << "lowering interval to" << d->queueTimer.interval()
                          << "ms and increasing chunk size to" << d->chunkSize << "bytes";
    }

    // We already filter on block count changes, but there are other cases where re-filtering
    // becomes necessary:
    //   - New content was added without a newline.
    //   - New content "moved out" old content because the maximum block count was reached.
    if (document()->blockCount() == oldBlockCount
        && (document()->lastBlock() != oldLastBlock || oldLastBlock.length() != oldBlockLength)
        && shouldFilterNewContentOnBlockCountChanged()) {
        filterNewContent();
    }

    if (d->scrollToBottom) {
        if (d->lastMessage.elapsed() < 5) {
            d->scrollTimer.start();
        } else {
            d->scrollTimer.stop();
            scrollToBottom();
        }
    }

    d->lastMessage.start();
    enableUndoRedo();
}

void OutputWindow::discardExcessiveOutput()
{
    // Unless the user instructs us to, we do not mess with the output.
    if (!d->discardExcessiveOutput)
        return;

    // Criterion 1: Are we being flooded?
    // If the pending output has been growing for the last ten times the output formatter
    // was invoked and it is considerably larger than the chunk size, we discard it.
    const qsizetype queuedSize = totalQueuedSize();
    if (!d->queuedSizeHistory.isEmpty() && d->queuedSizeHistory.last() > queuedSize)
        d->queuedSizeHistory.clear();
    d->queuedSizeHistory << queuedSize;
    bool discard = d->queuedSizeHistory.size() > int(10) && queuedSize > 5 * d->chunkSize;
    if (discard)
        qCDebug(chunkLog) << "discarding output due to size";

    // Criterion 2: Are we too slow?
    // If it would take longer than a minute to print the pending output and we have
    // already presented a reasonable amount of output to the user, we discard it.
    if (!discard) {
        discard = d->formatterCalls >= 10
                  && (queuedSize / d->chunkSize) * d->queueTimer.intervalAsDuration() > 60s;
        if (discard)
            qCDebug(chunkLog) << "discarding output due to time";
    }

    if (discard) {
        discardPendingToolOutput();
        d->queuedSizeHistory.clear();
        return;
    }
}

void OutputWindow::discardPendingToolOutput()
{
    Utils::erase(d->queuedOutput, [](const std::pair<QString, OutputFormat> &chunk) {
        return chunk.second != NormalMessageFormat && chunk.second != ErrorMessageFormat;
    });
    d->formatter.appendMessage(Tr::tr("[Discarding excessive amount of pending output.]\n"),
                               ErrorMessageFormat);
    emit outputDiscarded();
}

void OutputWindow::updateAutoScroll()
{
    d->scrollToBottom = verticalScrollBar()->sliderPosition() >= verticalScrollBar()->maximum() - 1;
}

qsizetype OutputWindow::totalQueuedSize() const
{
    return d->totalQueuedValue([](const QString &s) { return s.size(); });
}

qsizetype OutputWindow::totalQueuedLines() const
{
    return d->totalQueuedValue([](const QString &s) { return s.count('\n'); });
}

void OutputWindow::setMaxCharCount(qsizetype count)
{
    d->maxCharCount = count;
    setMaximumBlockCount(count / 100);
}

qsizetype OutputWindow::maxCharCount() const
{
    return d->maxCharCount;
}

void OutputWindow::appendMessage(const QString &output, OutputFormat format)
{
    if (d->queuedOutput.isEmpty() || d->queuedOutput.last().second != format)
        d->queuedOutput.push_back({output, format});
    else
        d->queuedOutput.last().first.append(output);
    if (!d->queueTimer.isActive())
        d->queueTimer.start();
}

void OutputWindow::registerPositionOf(unsigned taskId, int linkedOutputLines, int skipLines,
                                      int offset, TaskSource taskSource)
{
    if (linkedOutputLines <= 0)
        return;

    // For Tasks that result from an OutputLineParser, the corresponding content is the last
    // one written to the text edit, otherwise it's the last queued output.
    const int extraLines = taskSource == TaskSource::Parsed ? 0 : totalQueuedLines();

    const int blocknumber = document()->blockCount() - offset;

    // -1 because OutputFormatter has already added the newline.
    const int firstLine = blocknumber - linkedOutputLines - skipLines - 1 + extraLines;

    const int lastLine = firstLine + linkedOutputLines - 1;

    d->taskPositions.insert(taskId, {firstLine, lastLine});
}

bool OutputWindow::knowsPositionOf(unsigned taskId) const
{
    return d->taskPositions.contains(taskId);
}

void OutputWindow::showPositionOf(unsigned taskId)
{
    QPair<int, int> position = d->taskPositions.value(taskId);
    QTextCursor newCursor(document()->findBlockByNumber(position.second));

    // Move cursor to end of last line of interest:
    newCursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::MoveAnchor);
    setTextCursor(newCursor);

    // Move cursor and select lines:
    newCursor.setPosition(document()->findBlockByNumber(position.first).position(),
                          QTextCursor::KeepAnchor);
    setTextCursor(newCursor);

    // Center cursor now:
    centerCursor();
}

QMimeData *OutputWindow::createMimeDataFromSelection() const
{
    const auto mimeData = new QMimeData;
    QString content;
    const int selStart = textCursor().selectionStart();
    const int selEnd = textCursor().selectionEnd();
    const QTextBlock firstBlock = document()->findBlock(selStart);
    const QTextBlock lastBlock = document()->findBlock(selEnd);
    for (QTextBlock curBlock = firstBlock; curBlock != lastBlock; curBlock = curBlock.next()) {
        if (!curBlock.isVisible())
            continue;
        if (curBlock == firstBlock)
            content += curBlock.text().mid(selStart - firstBlock.position());
        else
            content += curBlock.text();
        content += '\n';
    }
    if (lastBlock.isValid() && lastBlock.isVisible()) {
        if (firstBlock == lastBlock)
            content = textCursor().selectedText();
        else
            content += lastBlock.text().mid(0, selEnd - lastBlock.position());
    }
    mimeData->setText(content);
    return mimeData;
}

void OutputWindow::clear()
{
    d->formatter.clear();
    d->scrollToBottom = true;
    d->taskPositions.clear();
    d->startOfNewContentCursor.setPosition(0);
}

void OutputWindow::clearLinesPrefixedWith(const QString& prefix, bool deleteTrailingLineBreak)
{
    QTextDocument *doc = document();

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

void OutputWindow::flush()
{
    if (totalQueuedSize() > 5 * d->chunkSize) {
        d->flushRequested = true;
        return;
    }
    d->queueTimer.stop();
    for (const auto &chunk : std::as_const(d->queuedOutput))
        handleOutputChunk(chunk.first, chunk.second, ChunkCompleteness::Complete);
    d->queuedOutput.clear();
    d->formatter.flush();
}

void OutputWindow::reset()
{
    flush();
    if (!d->queuedOutput.isEmpty()) {
        discardPendingToolOutput();
        flush();

        // For the unlikely case that we ourselves have sent excessive amount of output
        // via NormalMessageFormat or ErrorMessageFormat.
        d->queuedOutput.clear();
    }
    d->queueTimer.stop();
    d->queuedSizeHistory.clear();
    d->formatter.reset();
    d->formatterCalls = 0;
    d->scrollToBottom = true;
    d->flushRequested = false;
}

void OutputWindow::scrollToBottom()
{
    verticalScrollBar()->setValue(verticalScrollBar()->maximum());
    // QPlainTextEdit destroys the first calls value in case of multiline
    // text, so make sure that the scroll bar actually gets the value set.
    // Is a noop if the first call succeeded.
    verticalScrollBar()->setValue(verticalScrollBar()->maximum());
}

void OutputWindow::grayOutOldContent()
{
    if (!d->cursor.atEnd())
        d->cursor.movePosition(QTextCursor::End);
    QTextCharFormat endFormat = d->cursor.charFormat();

    d->cursor.setPosition(d->startOfNewContentCursor.position());
    d->cursor.movePosition(QTextCursor::End, QTextCursor::KeepAnchor, 1);

    QTextCharFormat format;
    const QColor bkgColor = palette().base().color();
    const QColor fgdColor = palette().text().color();
    double bkgFactor = 0.50;
    double fgdFactor = 1.-bkgFactor;
    format.setForeground(QColor((bkgFactor * bkgColor.red() + fgdFactor * fgdColor.red()),
                             (bkgFactor * bkgColor.green() + fgdFactor * fgdColor.green()),
                             (bkgFactor * bkgColor.blue() + fgdFactor * fgdColor.blue()) ));
    d->cursor.mergeCharFormat(format);

    d->cursor.movePosition(QTextCursor::End);
    d->cursor.setCharFormat(endFormat);
    d->startOfNewContentCursor.setPosition(d->cursor.position());
    d->cursor.insertBlock(QTextBlockFormat());
}

void OutputWindow::enableUndoRedo()
{
    setMaximumBlockCount(0);
    setUndoRedoEnabled(true);
}

void OutputWindow::setWordWrapEnabled(bool wrap)
{
    if (wrap)
        setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    else
        setWordWrapMode(QTextOption::NoWrap);
}

void OutputWindow::setDiscardExcessiveOutput(bool discard)
{
    d->discardExcessiveOutput = discard;
}

#ifdef WITH_TESTS

class OutputFilterTest final : public QObject
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
        QCOMPARE(OutputWindow::contextLines({5}, 10, 0, 0), QList<int>());
        QCOMPARE(OutputWindow::contextLines({}, 10, 2, 2), QList<int>());

        // One line either side, in order.
        QCOMPARE(OutputWindow::contextLines({5}, 10, 1, 1), QList<int>({4, 6}));

        // Clamped at the top of the document and at the bottom: a match on the
        // first line reveals nothing above it, and one on the last nothing
        // below.
        QCOMPARE(OutputWindow::contextLines({0}, 10, 2, 0), QList<int>());
        QCOMPARE(OutputWindow::contextLines({9}, 10, 0, 2), QList<int>());
        QCOMPARE(OutputWindow::contextLines({1}, 10, 2, 0), QList<int>({0}));

        // Overlapping context around neighbouring matches is one line, not two.
        QCOMPARE(OutputWindow::contextLines({2, 4}, 10, 1, 1), QList<int>({1, 3, 5}));

        // And a match is never listed as its own context: it is already shown,
        // and saying so twice would hide the difference between the two.
        QCOMPARE(OutputWindow::contextLines({2, 3}, 10, 1, 1), QList<int>({1, 4}));
    }

    // What an output pane does about its character limit. Both of these were
    // arithmetic inside a QPlainTextEdit: to see either you had to produce
    // megabytes of build output and watch.
    void testTheCharacterLimitKeepsBothEnds()
    {
        // Under the limit, nothing happens.
        QCOMPARE(OutputWindow::elideChunk("short", 100), QString("short"));

        // Over it, both ends are kept and the middle is said to be gone.
        const QString chunk = QString("a").repeated(50) + QString("b").repeated(50);
        const QString elided = OutputWindow::elideChunk(chunk, 20);
        QVERIFY2(elided.startsWith(QString("a").repeated(10)), qPrintable(elided.left(20)));
        QVERIFY2(elided.endsWith(QString("b").repeated(10)), qPrintable(elided.right(20)));
        QVERIFY(elided.contains("[[[..."));

        // And what it says went is what went: 100 characters in, 2 x 10 kept,
        // so 80 elided. Reporting size - limit would say 80 here and 81 for an
        // odd limit, which is the character the two halves lose to rounding.
        QVERIFY2(elided.contains("80"), qPrintable(elided));
        const QString oddLimit = OutputWindow::elideChunk(chunk, 21);
        QVERIFY2(oddLimit.contains("80"), qPrintable(oddLimit));

        // Nothing to drop while it all still fits.
        QCOMPARE(OutputWindow::blocksToKeep({10, 10}, 20, 5, 100), -1);

        // Otherwise the oldest blocks go, one at a time, until it does.
        QCOMPARE(OutputWindow::blocksToKeep({10, 10, 10}, 30, 5, 25), 2);
        QCOMPARE(OutputWindow::blocksToKeep({10, 10, 10}, 30, 5, 15), 1);

        // Never the last one: it is where the new text lands.
        QCOMPARE(OutputWindow::blocksToKeep({10, 10, 10}, 30, 100, 5), 1);
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
        OutputWindow::copyFiltered(&source, &filtered,
                                   OutputWindow::filterPredicate("error", {}), 0, 0);
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
        OutputWindow::copyFiltered(&source, &withContext,
                                   OutputWindow::filterPredicate("error", {}), 1, 0);
        QCOMPARE(withContext.blockCount(), 4);
        QCOMPARE(withContext.findBlockByNumber(0).text(), QString("first: ordinary"));
        QCOMPARE(withContext.findBlockByNumber(3).text(), QString("fourth: an error"));

        // An empty filter is the whole document, which is what a pane shows
        // when nothing is typed in its filter field.
        QTextDocument unfiltered;
        OutputWindow::copyFiltered(&source, &unfiltered, OutputWindow::filterPredicate({}, {}),
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
        OutputWindow::copyFiltered(&big, &bigFiltered,
                                   OutputWindow::filterPredicate("of build", {}), 0, 0);
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
            const auto matches = OutputWindow::filterPredicate(QString::fromLatin1(c.filter), {});

            QTextDocument whole;
            QTextCursor writer(&whole);
            for (int i = 0; i < lines.size(); ++i) {
                if (i > 0)
                    writer.insertBlock();
                writer.insertText(lines.at(i));
            }

            QTextDocument atOnce;
            OutputWindow::copyFiltered(&whole, &atOnce, matches, c.before, c.after);

            // The same lines, but arriving one at a time.
            QTextDocument source;
            QTextDocument incremental;
            QTextCursor appender(&source);
            OutputWindow::FilteredAppendState state;
            for (int i = 0; i < lines.size(); ++i) {
                if (i > 0)
                    appender.insertBlock();
                appender.insertText(lines.at(i));
                OutputWindow::appendFiltered(&source, &incremental, matches, c.before, c.after,
                                             state);
            }

            QCOMPARE(source.toPlainText(), whole.toPlainText());
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
        using Flags = OutputWindow::FilterModeFlags;
        using Flag = OutputWindow::FilterModeFlag;

        // No filter: everything stays, which is not the same as "matches
        // nothing" and is what an empty field has to mean.
        const auto none = OutputWindow::filterPredicate({}, {});
        QVERIFY(none("anything at all"));
        QVERIFY(none({}));

        // Plain text, and case-blind unless asked.
        const auto plain = OutputWindow::filterPredicate("error", {});
        QVERIFY(plain("an ERROR happened"));
        QVERIFY(!plain("all good"));
        const auto cased = OutputWindow::filterPredicate("error", Flags(Flag::CaseSensitive));
        QVERIFY(!cased("an ERROR happened"));
        QVERIFY(cased("an error happened"));

        // Inverted keeps what does not match - and an empty filter is not
        // inverted into hiding everything, which the early return is what
        // guarantees.
        const auto inverted = OutputWindow::filterPredicate("error", Flags(Flag::Inverted));
        QVERIFY(!inverted("an error happened"));
        QVERIFY(inverted("all good"));
        const auto invertedEmpty = OutputWindow::filterPredicate({}, Flags(Flag::Inverted));
        QVERIFY2(invertedEmpty("anything at all"),
                 "an empty inverted filter hid every line");

        // A regular expression, case-blind unless asked.
        const auto regexp = OutputWindow::filterPredicate("e[rd]{2}or", Flags(Flag::RegExp));
        QVERIFY(regexp("an errorish line"));
        QVERIFY(regexp("an ERROR line"));
        QVERIFY(!regexp("all good"));

        // And a half-typed one matches nothing rather than everything: the
        // pane empties as it is typed and fills again when it is valid.
        const auto halfTyped = OutputWindow::filterPredicate("error(", Flags(Flag::RegExp));
        QVERIFY2(!halfTyped("an error happened"),
                 "an unfinished regular expression let every line through");
    }
};

QObject *createOutputFilterTest()
{
    return new OutputFilterTest;
}

#endif // WITH_TESTS

} // namespace Core

#ifdef WITH_TESTS
#include "outputwindow.moc"
#endif
