// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "outputpaneview.h"

#include "coreconstants.h"
#include <QElapsedTimer>
#include <utils/algorithm.h>
#include "coreplugintr.h"
#include "icore.h"
#include <QAction>
#include "icontext.h"
#include "actionmanager/actionmanager.h"
#include <QGuiApplication>
#include <QClipboard>
#include "actionmanager/command.h"
#include <utils/temporarydirectory.h>
#include <utils/textfileformat.h>
#include <utils/filedialogs.h>
#include <utils/fileutils.h>
#include "idocument.h"
#include "editormanager/ieditor.h"
#include "editormanager/editormanager.h"
#include "outputview.h"
#include "messagemanager.h"
#include "ioutputpane.h"

#include <utils/fancylineedit.h>
#include <utils/qtcassert.h>
#include <utils/qtcsettings.h>

#include "find/ifindsupport.h"
#include <utils/aggregate.h>
#include <QSignalSpy>
#include <QTabWidget>
#include <QTest>
#include <QApplication>
#include <QTextBlock>
#include <QTextCursor>
#include <QVBoxLayout>

using namespace Utils;
using namespace std::chrono_literals;

namespace Core {

OutputPaneView::OutputPaneView(const Context &context, const Key &zoomSettingsKey,
                               QWidget *parent)
    : QWidget(parent)
    , m_startOfNewContent(&m_source)
    , m_zoomSettingsKey(zoomSettingsKey)
    , m_outputFileNameHint(Tr::tr("output.txt"))
{
    m_startOfNewContent.setKeepPositionOnInsert(true);
    // Itself as the sink object, so a parser that finds a task can tell this
    // where the task's output went - see OutputTaskSink.
    m_formatter.setSink(&m_source, this);

    setMaxCharCount(Constants::DEFAULT_MAX_CHAR_COUNT);
    if (!context.isEmpty()) {
        IContext::attach(this, context);

        // Registered here rather than left to the view, because a command
        // belongs to a context and only a pane knows which one it is in.
        // Without this the context menu offers Copy and Select All and their
        // shortcuts do nothing, which is what four ported panes were doing.
        auto * const copyAction = new QAction(this);
        ActionManager::registerAction(copyAction, Constants::COPY, context);
        connect(copyAction, &QAction::triggered, this, &OutputPaneView::copy);

        auto * const selectAllAction = new QAction(this);
        ActionManager::registerAction(selectAllAction, Constants::SELECTALL, context);
        connect(selectAllAction, &QAction::triggered, this, &OutputPaneView::selectAll);
    }

    m_queueTimer.setSingleShot(true);
    m_queueTimer.setInterval(10ms);
    connect(&m_queueTimer, &QTimer::timeout, this, &OutputPaneView::writeNextChunk);
    connect(&m_source, &QTextDocument::contentsChange, this,
            [this](int position, int charsRemoved, int) {
                // Only the limit takes anything off the front; everything else
                // this class does to the source refilters for itself.
                if (position == 0 && charsRemoved > 0)
                    m_sourceTrimmed = true;
            });

    auto * const layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    if (!m_zoomSettingsKey.isEmpty()) {
        m_zoom = ICore::settings()->value(m_zoomSettingsKey).toFloat();
        connect(ICore::instance(), &ICore::saveSettingsRequested, this, [this] {
            ICore::settings()->setValueWithDefault(m_zoomSettingsKey, fontZoom(), 0.f);
        });
    }

    view();
}

OutputView *OutputPaneView::view()
{
    if (m_view)
        return m_view;

    m_view = createOutputView();
    if (!m_view)
        return nullptr;

    layout()->addWidget(m_view);
    m_view->setDocument(shownDocument());

    connect(m_view, &OutputView::saveContentsRequested, this, [this] {
        const FilePath file = FileUtils::getSaveFilePath(
            {}, FileUtils::homePath() / m_outputFileNameHint);
        if (file.isEmpty())
            return;
        if (const Result<> res = saveContentsTo(file); !res)
            MessageManager::writeDisrupting(res.error());
    });
    connect(m_view, &OutputView::copyContentsToScratchBufferRequested,
            this, &OutputPaneView::copyContentsToScratchBuffer);
    connect(m_view, &OutputView::clearRequested, this, &OutputPaneView::clear);
    connect(m_view, &OutputView::contextMenuAboutToShow,
            this, &OutputPaneView::contextMenuAboutToShow);
    connect(m_view, &OutputView::linkActivated, this, &OutputPaneView::linkActivated);
    connect(m_view, &OutputView::wheelZoom, this, [this] {
        // The view moved the zoom itself, so this has to catch up before
        // telling anyone: a pane reads fontZoom() to match its other views.
        m_zoom = m_view->fontZoom();
        emit wheelZoom();
    });
    if (m_baseFontSet)
        m_view->setBaseFont(m_baseFont);
    m_view->setWordWrapEnabled(m_wordWrapEnabled);
    if (m_backgroundColor.isValid())
        m_view->setBackgroundColor(m_backgroundColor);
    m_view->setFontZoom(m_zoom);
    m_view->setWheelZoomEnabled(m_wheelZoomEnabled);
    return m_view;
}

void OutputPaneView::showEvent(QShowEvent *event)
{
    // The last moment it can be made, and the first one at which every plugin
    // has certainly initialized. A pane built during startup gets its view
    // here rather than never.
    view();
    QWidget::showEvent(event);
}

OutputPaneView::~OutputPaneView() = default;

void OutputPaneView::appendMessage(const QString &text, OutputFormat format)
{
    // Runs of the same format are joined, so that a chunk can be cut anywhere
    // in them rather than at whatever boundary the caller happened to write.
    if (m_queuedOutput.isEmpty() || m_queuedOutput.last().second != format)
        m_queuedOutput.append({text, format});
    else
        m_queuedOutput.last().first.append(text);

    if (!m_queueTimer.isActive())
        m_queueTimer.start();
}

qsizetype OutputPaneView::totalQueued(const std::function<qsizetype(const QString &)> &measure) const
{
    qsizetype total = 0;
    for (const auto &chunk : m_queuedOutput)
        total += measure(chunk.first);
    return total;
}

void OutputPaneView::setDiscardExcessiveOutput(bool discard)
{
    m_discardExcessiveOutput = discard;
}

void OutputPaneView::discardPendingOutput()
{
    // What a tool wrote goes; what Qt Creator itself said about the run stays,
    // or the reason the output stops would go with it.
    Utils::erase(m_queuedOutput, [](const QPair<QString, OutputFormat> &chunk) {
        return chunk.second != NormalMessageFormat && chunk.second != ErrorMessageFormat;
    });
    writeChunk(Tr::tr("[Discarding excessive amount of pending output.]\n"),
               ErrorMessageFormat, false);
    emit outputDiscarded();
}

void OutputPaneView::writeNextChunk()
{
    QTC_ASSERT(!m_queuedOutput.isEmpty(), return);

    if (m_discardExcessiveOutput) {
        const bool discard = OutputText::shouldDiscardPendingOutput(
            m_pendingState, totalQueued([](const QString &s) { return s.size(); }), m_chunkSize,
            m_formatterCalls, m_queueTimer.intervalAsDuration());
        if (discard) {
            discardPendingOutput();
            m_pendingState.queuedSizeHistory.clear();
        }
        if (m_queuedOutput.isEmpty())
            return;
    }

    auto &chunk = m_queuedOutput.first();
    const qsizetype end = OutputText::chunkEndPosition(chunk.first, m_chunkSize);
    if (end == chunk.first.size()) {
        writeChunk(chunk.first, chunk.second, false);
        m_queuedOutput.removeFirst();
    } else {
        writeChunk(chunk.first.left(end), chunk.second, true);
        chunk.first.remove(0, end);
    }

    if (!m_queuedOutput.isEmpty()) {
        m_queueTimer.start();
    } else if (m_flushRequested) {
        m_formatter.flush();
        m_flushRequested = false;
    }
}

#ifdef WITH_TESTS
void OutputPaneView::writeNextChunkForTest()
{
    if (!m_queuedOutput.isEmpty())
        writeNextChunk();
}
#endif

void OutputPaneView::flush()
{
    // A flush that had to write a large backlog would block for as long as the
    // backlog is long, which is what the queue exists to avoid. Ask for it to
    // happen when the queue drains instead.
    if (totalQueued([](const QString &s) { return s.size(); }) > 5 * m_chunkSize) {
        m_flushRequested = true;
        return;
    }
    m_queueTimer.stop();
    const auto queued = m_queuedOutput;
    m_queuedOutput.clear();
    for (const auto &chunk : queued)
        writeChunk(chunk.first, chunk.second, false);
    m_formatter.flush();
}

void OutputPaneView::reset()
{
    flush();
    if (!m_queuedOutput.isEmpty()) {
        discardPendingOutput();
        m_queuedOutput.clear();
    }
    m_queueTimer.stop();
    m_pendingState.queuedSizeHistory.clear();
    m_formatter.reset();
    m_formatterCalls = 0;
    m_flushRequested = false;
}

void OutputPaneView::writeChunk(const QString &text, OutputFormat format, bool chunkWasSplit)
{
    QString out = text;
    if (m_maxCharCount > 0) {
        if (out.size() > m_maxCharCount) {
            // This one chunk is more than the whole allowance, so nothing that
            // is already there can be kept and the chunk itself has to lose
            // its middle.
            out = OutputText::elideChunk(out, m_maxCharCount);
            m_source.setMaximumBlockCount(int(out.count('\n')) + 1);
        } else {
            QList<int> blockLengths;
            for (QTextBlock block = m_source.firstBlock(); block.isValid(); block = block.next())
                blockLengths << block.length();
            m_source.setMaximumBlockCount(OutputText::blocksToKeep(
                blockLengths, m_source.characterCount(), out.size(), m_maxCharCount));
        }
    }

    m_sourceTrimmed = false;

    QElapsedTimer formatterTimer;
    formatterTimer.start();
    m_formatter.appendMessage(out, format);
    ++m_formatterCalls;

    const OutputText::OutputPacing paced
        = OutputText::pacedBy({m_queueTimer.intervalAsDuration(), m_chunkSize},
                                std::chrono::milliseconds(formatterTimer.elapsed()),
                                chunkWasSplit);
    m_queueTimer.setInterval(paced.interval);
    m_chunkSize = paced.chunkSize;

    if (isFiltering()) {
        // What the filtered copy has already taken is addressed by block
        // number, so losing the front of the source moves every one of them
        // and the copy has to be made again.
        if (m_sourceTrimmed) {
            refilter();
        } else {
            // Otherwise only what arrived is filtered, not the whole document
            // again: a build appends thousands of times.
            OutputText::appendFiltered(&m_source, &m_filtered, currentPredicate(),
                                         m_beforeContext, m_afterContext, m_appendState);
        }
    }
    m_sourceTrimmed = false;
}

void OutputPaneView::setMaxCharCount(qsizetype count)
{
    m_maxCharCount = count;
    m_source.setMaximumBlockCount(int(count / 100));
}

qsizetype OutputPaneView::maxCharCount() const
{
    return m_maxCharCount;
}

void OutputPaneView::grayOutOldContent()
{
    OutputText::grayOutContentBefore(m_startOfNewContent, palette());

    // Dimming changes the colours of text the filtered copy already took, so
    // that copy has to be made again. Only while a filter is set.
    if (isFiltering())
        refilter();
}

void OutputPaneView::registerPositionOf(unsigned taskId, int linkedOutputLines, int skipLines,
                                       int offset, TaskSource source)
{
    if (linkedOutputLines <= 0)
        return;

    // A task reported directly names output that is still queued, so its lines
    // are where they will be once that has been written.
    const int extraLines = source == TaskSource::Parsed
                               ? 0
                               : int(totalQueued([](const QString &s) {
                                     return qsizetype(s.count('\n'));
                                 }));

    m_taskPositions.insert(taskId, taskLineRange(m_source.blockCount(), linkedOutputLines,
                                                 skipLines, offset, extraLines));
}

bool OutputPaneView::knowsPositionOf(unsigned taskId) const
{
    return m_taskPositions.contains(taskId);
}

void OutputPaneView::showPositionOf(unsigned taskId)
{
    OutputView * const output = view();
    QTC_ASSERT(output, return);

    const QPair<int, int> lines = m_taskPositions.value(taskId, {-1, -1});
    if (lines.first < 0)
        return;

    // Selected from the end of the last line back to the start of the first,
    // so the cursor - which is what the view scrolls to - ends up on the
    // first line of the task rather than below its output.
    QTextCursor cursor(m_source.findBlockByNumber(lines.second));
    cursor.movePosition(QTextCursor::EndOfBlock);
    cursor.setPosition(m_source.findBlockByNumber(lines.first).position(),
                       QTextCursor::KeepAnchor);
    output->setTextCursor(cursor);
}

void OutputPaneView::showCursor(const QTextCursor &cursor)
{
    OutputView * const output = view();
    QTC_ASSERT(output, return);
    if (cursor.isNull())
        return;

    output->setTextCursor(cursor);
}

void OutputPaneView::clear()
{
    m_queuedOutput.clear();
    m_queueTimer.stop();
    m_flushRequested = false;
    m_pendingState.queuedSizeHistory.clear();
    m_formatter.clear();
    m_source.clear();
    m_filtered.clear();
    m_appendState = {};
    m_startOfNewContent.setPosition(0);
    m_taskPositions.clear();
}

void OutputPaneView::setPlainText(const QString &text)
{
    clear();
    m_source.setPlainText(text);
}

QTextDocument *OutputPaneView::sourceDocument()
{
    return &m_source;
}

void OutputPaneView::clearLinesPrefixedWith(const QString &prefix, bool deleteTrailingLineBreak)
{
    OutputText::removeLinesPrefixedWith(&m_source, prefix, deleteTrailingLineBreak);

    // Lines went out of the middle, so what the filtered copy holds no longer
    // lines up with where it had got to.
    if (isFiltering())
        refilter();
}

bool OutputPaneView::isFiltering() const
{
    return !m_filterText.isEmpty() || bool(m_extraFilter);
}

OutputText::TextMatchingFunction OutputPaneView::currentPredicate() const
{
    const OutputText::TextMatchingFunction text
        = OutputText::filterPredicate(m_filterText, m_filterMode);
    if (!m_extraFilter)
        return text;

    return [text, extra = m_extraFilter](const QString &line) {
        return extra(line) && text(line);
    };
}

void OutputPaneView::setExtraFilter(const OutputText::TextMatchingFunction &extra)
{
    m_extraFilter = extra;
    refilter();
}

void OutputPaneView::setFilter(const QString &text, OutputText::FilterModeFlags mode,
                               int before, int after)
{
    if (m_filterText == text && m_filterMode == mode && m_beforeContext == before
        && m_afterContext == after) {
        return;
    }
    m_filterText = text;
    m_filterMode = mode;
    m_beforeContext = before;
    m_afterContext = after;
    refilter();
}

void OutputPaneView::setFilter(const QString &text, Qt::CaseSensitivity caseSensitivity,
                               bool regexp, bool inverted, int before, int after)
{
    using Flag = OutputText::FilterModeFlag;
    OutputText::FilterModeFlags mode;
    if (regexp)
        mode |= Flag::RegExp;
    if (caseSensitivity == Qt::CaseSensitive)
        mode |= Flag::CaseSensitive;
    if (inverted)
        mode |= Flag::Inverted;

    setFilter(text, mode, before, after);
}

void OutputPaneView::refilter()
{
    m_filtered.clear();
    m_appendState = {};
    if (isFiltering()) {
        // Filtering everything at once and filtering it a line at a time are
        // the same operation from an empty state, so there is one code path.
        OutputText::appendFiltered(&m_source, &m_filtered, currentPredicate(),
                                     m_beforeContext, m_afterContext, m_appendState);
    }
    if (OutputView * const output = view())
        output->setDocument(shownDocument());
}

QTextDocument *OutputPaneView::shownDocument() const
{
    return isFiltering() ? const_cast<QTextDocument *>(&m_filtered)
                         : const_cast<QTextDocument *>(&m_source);
}

QString OutputPaneView::toPlainText() const
{
    return m_source.toPlainText();
}

void OutputPaneView::setOutputFileNameHint(const QString &fileName)
{
    m_outputFileNameHint = fileName;
}

Result<> OutputPaneView::saveContentsTo(const FilePath &file) const
{
    TextFileFormat format;
    format.setEncoding(EditorManager::defaultTextEncoding());
    format.lineTerminationMode = EditorManager::defaultLineEnding();
    return format.writeFile(file, toPlainText());
}

QString OutputPaneView::scratchBufferNameTemplate(const QString &outputFileNameHint)
{
    QString prefix = FilePath::fromString(outputFileNameHint).baseName();
    if (prefix.isEmpty())
        prefix = "scratch";
    return QString("%1-XXXXXX.txt").arg(prefix);
}

void OutputPaneView::copyContentsToScratchBuffer() const
{
    const auto tempPath
        = FileUtils::scratchBufferFilePath(scratchBufferNameTemplate(m_outputFileNameHint));
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
}

void OutputPaneView::setContextMenuActions(const QList<QAction *> &actions,
                                           bool replaceStandard)
{
    if (OutputView * const output = view())
        output->setContextMenuActions(actions, replaceStandard);
}

int OutputPaneView::documentPositionAt(qreal x, qreal y) const
{
    OutputView * const output = const_cast<OutputPaneView *>(this)->view();
    return output ? output->documentPositionAt(x, y) : -1;
}

void OutputPaneView::copy()
{
    if (OutputView * const output = view())
        output->copy();
}

void OutputPaneView::selectAll()
{
    if (OutputView * const output = view())
        output->selectAll();
}

void OutputPaneView::scrollToBottom()
{
    if (OutputView * const output = view())
        output->scrollToBottom();
}

void OutputPaneView::setWordWrapEnabled(bool enabled)
{
    m_wordWrapEnabled = enabled;
    if (OutputView * const output = view())
        output->setWordWrapEnabled(enabled);
}

void OutputPaneView::setBackgroundColor(const QColor &color)
{
    m_backgroundColor = color;
    m_formatter.setExplicitBackgroundColor(color);
    if (OutputView * const output = view())
        output->setBackgroundColor(color);
}

void OutputPaneView::setBaseFont(const QFont &font)
{
    m_baseFont = font;
    m_baseFontSet = true;
    if (OutputView * const output = view())
        output->setBaseFont(font);
}

void OutputPaneView::setWheelZoomEnabled(bool enabled)
{
    m_wheelZoomEnabled = enabled;
    if (OutputView * const output = view())
        output->setWheelZoomEnabled(enabled);
}

void OutputPaneView::zoomIn(int range)
{
    setFontZoom(m_zoom + range);
}

void OutputPaneView::zoomOut(int range)
{
    setFontZoom(m_zoom - range);
}

void OutputPaneView::resetZoom()
{
    setFontZoom(0);
}

void OutputPaneView::setFontZoom(float zoom)
{
    m_zoom = zoom;
    if (OutputView * const output = view())
        output->setFontZoom(zoom);
}

float OutputPaneView::fontZoom() const
{
    return m_zoom;
}

#ifdef WITH_TESTS

class OutputPaneViewTest final : public QObject
{
    Q_OBJECT

    // The shared view: it registers no commands, so a test may build one.
    // The panes around it claim fixed action ids and there is already one of
    // each of those in the running instance.
    // Asked of the document handed to the view, not of the glyphs. What this
    // class decides is *which* document is shown and what is in it; that a
    // view draws the document it is given is Core::OutputView's part, and is
    // checked against the rendered frame in the QuickUi tests. Core does not
    // link Qt Quick, which is the whole point of the seam.
    static QString shownText(const OutputPaneView &view)
    {
        return view.shownDocument()->toPlainText();
    }

private slots:
    void testWhatIsWrittenIsShown()
    {
        OutputPaneView view;
        QVERIFY2(view.view(), "the output pane view has nothing to draw with");

        view.appendMessage("running cmake\n", Utils::GeneralMessageFormat);
        view.appendMessage("it went wrong\n", Utils::ErrorMessageFormat);
        view.flush();
        QVERIFY(shownText(view).contains("it went wrong"));

        // And the view was given that document rather than a copy of it.
        QCOMPARE(view.view()->document(), view.shownDocument());

        view.clear();
        QVERIFY(shownText(view).isEmpty());
    }

    void testAFilterShowsOnlyTheLinesThatMatch()
    {
        OutputPaneView view;
        QVERIFY(view.view());

        view.appendMessage("configuring\n", Utils::GeneralMessageFormat);
        view.appendMessage("error: nothing works\n", Utils::GeneralMessageFormat);
        view.appendMessage("done\n", Utils::GeneralMessageFormat);
        view.flush();

        // A Qt Quick view cannot be filtered by hiding blocks, so what it is
        // shown is a second document holding the lines that pass.
        view.setFilter("error", {});
        QCOMPARE(view.view()->document(), view.shownDocument());
        QVERIFY(!shownText(view).contains("configuring"));
        QVERIFY(shownText(view).contains("error: nothing works"));

        // What arrives while a filter is set is filtered too, without the
        // whole document being done again.
        view.appendMessage("error: still nothing\n", Utils::GeneralMessageFormat);
        view.flush();
        QVERIFY(shownText(view).contains("still nothing"));
        view.appendMessage("almost there\n", Utils::GeneralMessageFormat);
        view.flush();
        QVERIFY(!shownText(view).contains("almost there"));

        // Inverted, the other lines are the ones left.
        view.setFilter("error", OutputText::FilterModeFlag::Inverted);
        QVERIFY(shownText(view).contains("configuring"));
        QVERIFY(!shownText(view).contains("nothing works"));

        // Clearing it puts the whole thing back, including what was hidden.
        view.setFilter({}, {});
        QVERIFY(shownText(view).contains("configuring"));
        QVERIFY(shownText(view).contains("almost there"));
    }

    void testRetractingLinesByPrefix()
    {
        OutputPaneView view;
        view.appendMessage("[cmake] step one\n", Utils::GeneralMessageFormat);
        view.appendMessage("kept\n", Utils::GeneralMessageFormat);
        view.appendMessage("[cmake] step two\n", Utils::GeneralMessageFormat);
        // Deliberately not the last line. Taking the last one takes the empty
        // block a trailing newline leaves with it, and what is appended next
        // then lands on the end of the line before - which the widget did too,
        // and is not this port's to change.
        view.appendMessage("and kept\n", Utils::GeneralMessageFormat);
        view.flush();

        view.clearLinesPrefixedWith("[cmake]", true);
        QVERIFY(!view.shownDocument()->toPlainText().contains("step one"));
        QVERIFY(view.shownDocument()->toPlainText().contains("kept"));

        // And with a filter set, where the filtered copy has to be built
        // again - the lines went out of the middle of what it was following.
        view.appendMessage("[cmake] step three\n", Utils::GeneralMessageFormat);
        view.appendMessage("error: here\n", Utils::GeneralMessageFormat);
        view.appendMessage("last line\n", Utils::GeneralMessageFormat);
        view.flush();
        view.setFilter("e", {});
        QVERIFY(view.shownDocument()->toPlainText().contains("step three"));
        view.clearLinesPrefixedWith("[cmake]", true);
        QVERIFY2(!view.shownDocument()->toPlainText().contains("step three"),
                 "a retracted line stayed in the filtered copy");
        QVERIFY(view.shownDocument()->toPlainText().contains("error: here"));
    }

    void testDimmingAndZoomReachTheView()
    {
        OutputPaneView view;
        QVERIFY(view.view());

        view.appendMessage("the last run\n", Utils::GeneralMessageFormat);
        view.flush();
        QTextCursor cursor(view.shownDocument());
        cursor.setPosition(1);
        const QColor before = cursor.charFormat().foreground().color();

        view.grayOutOldContent();
        cursor.setPosition(1);
        QVERIFY2(cursor.charFormat().foreground().color() != before,
                 "starting a new run did not dim what the last one left");

        const float zoom = view.view()->fontZoom();
        view.zoomIn();
        QCOMPARE(view.view()->fontZoom(), zoom + 1);
        view.zoomOut();
        view.zoomOut();
        QCOMPARE(view.view()->fontZoom(), zoom - 1);
    }

    void testAFilterAlsoShowsTheLinesAroundAMatch()
    {
        // General Messages asks for context lines; Build System Output does
        // not. Passing them through was the difference between the two panes,
        // so it is worth its own case.
        OutputPaneView view;
        for (const QString &line : QStringList{"one", "two", "the match", "four", "five"})
            view.appendMessage(line + '\n', Utils::GeneralMessageFormat);
        view.flush();

        view.setFilter("match", {}, 0, 0);
        QCOMPARE(shownText(view), QString("the match"));

        view.setFilter("match", {}, 1, 1);
        QCOMPARE(shownText(view), QString("two\nthe match\nfour"));

        view.setFilter("match", {}, 2, 0);
        QCOMPARE(shownText(view), QString("one\ntwo\nthe match"));
    }

    void testAPaneHandsOutItsTextWithoutHandingOutItsWidget()
    {
        // The contract that replaced outputWindows(). A reader wants the text,
        // not a QPlainTextEdit it then calls toPlainText() on - which is what
        // stopped any pane from being drawn with anything else.
        IOutputPane *general = nullptr;
        for (IOutputPane * const pane : IOutputPane::allOutputPanes()) {
            if (pane->id() == Utils::Id("GeneralMessages"))
                general = pane;
        }
        QVERIFY2(general, "there is no General Messages pane");

        const QString written = "a line only this test writes";
        MessageManager::writeSilently(written);

        // The pane queues what it is told, so this waits for it to have been
        // written rather than assuming it already has.
        QTRY_VERIFY(general->outputTexts().first().contains(written));
        const QStringList texts = general->outputTexts();
        QCOMPARE(texts.size(), 1);
        QVERIFY2(texts.first().contains(written),
                 "the pane did not hand out the text it was given");

        // The pane's own filter, driven through the line edit it puts in the
        // tool bar - the only way in from outside, and the only thing that
        // calls its updateFilter().
        Utils::FancyLineEdit *filterEdit = nullptr;
        for (QWidget * const widget : general->toolBarWidgets()) {
            if (auto * const edit = qobject_cast<Utils::FancyLineEdit *>(widget))
                filterEdit = edit;
        }
        QVERIFY2(filterEdit, "the pane offers no way to filter it");

        MessageManager::writeSilently("a second line, matching nothing");
        QTRY_VERIFY(general->outputTexts().first().contains("matching nothing"));
        filterEdit->setText(written);
        const QScopeGuard clearFilter([filterEdit] { filterEdit->setText({}); });

        // A filter is about what is shown, not about what the pane holds: a
        // reader asking a pane for its output should get all of it, not
        // whatever happens to be on screen.
        QVERIFY2(general->outputTexts().first().contains("matching nothing"),
                 "a filter took lines out of what the pane hands to a reader");

        // But it does change what is shown. Found by looking for the view
        // holding what this test wrote, because the pane keeps it private and
        // asking outputWidget() for it would reparent the running instance's
        // pane out of its own window.
        OutputPaneView *shown = nullptr;
        for (QWidget * const widget : QApplication::allWidgets()) {
            if (auto * const candidate = qobject_cast<OutputPaneView *>(widget)) {
                if (candidate->toPlainText().contains(written))
                    shown = candidate;
            }
        }
        QVERIFY2(shown, "the General Messages view could not be found");
        QVERIFY2(!shown->shownDocument()->toPlainText().contains("matching nothing"),
                 "the pane's filter never reached its view");
        QVERIFY(shown->shownDocument()->toPlainText().contains(written));
    }

    void testTheOldestOutputGoesWhenThereIsTooMuch()
    {
        OutputPaneView view;

        // The default is the one the widget applied, so a pane that never asks
        // for a limit still has one - which is what the first two panes ported
        // quietly lost until this.
        QCOMPARE(view.maxCharCount(), qsizetype(Constants::DEFAULT_MAX_CHAR_COUNT));

        // Lines long enough that the character allowance is what bites. With
        // short ones the coarse block cap setMaxCharCount() applies - a
        // hundredth of the allowance - is reached first, and then this passes
        // without the per-append accounting running at all.
        view.setMaxCharCount(1000);
        const QString padding(180, 'x');
        // Written a line at a time, as a build writes it. Handed over all at
        // once it is a single chunk larger than the whole allowance, which is
        // elided in the middle instead - a different case, tested below.
        for (int i = 0; i < 30; ++i) {
            view.appendMessage(QString("line %1 of output %2\n").arg(i).arg(padding),
                               Utils::GeneralMessageFormat);
            view.flush();
        }

        const QString kept = view.toPlainText();
        QVERIFY2(kept.size() <= 1200, qPrintable(QString("kept %1 characters of an allowance "
                                                         "of 1000").arg(kept.size())));

        // The end is what is kept: output is read from the bottom.
        QVERIFY2(kept.contains("line 29 of"), "the newest output was dropped");
        QVERIFY2(!kept.contains("line 0 of"), "the oldest output was kept");
    }

    void testTheCharacterLimitIsAlsoABlockLimit()
    {
        // A pane that used to say how many *lines* it wanted - Squish asked a
        // QPlainTextEdit for ten thousand blocks - has to say it in characters
        // now. A hundred characters a line is the conversion, and it is only
        // right because setMaxCharCount() sets the block cap to a hundredth of
        // what it is given.
        OutputPaneView view;
        view.setMaxCharCount(10000 * 100);
        QCOMPARE(view.sourceDocument()->maximumBlockCount(), 10000);

        // And the two limits are both real: the block cap alone would let a
        // few very long lines outgrow the allowance, which is what the
        // per-append accounting is for.
        QCOMPARE(view.maxCharCount(), qsizetype(10000 * 100));
    }

    void testAChunkTooBigToKeepLosesItsMiddle()
    {
        OutputPaneView view;
        view.setMaxCharCount(200);

        const QString huge = QString("start ") + QString(500, 'x') + " end\n";
        view.appendMessage(huge, Utils::GeneralMessageFormat);
        view.flush();

        const QString kept = view.toPlainText();
        QVERIFY2(kept.contains("start"), "the beginning of the chunk went");
        QVERIFY2(kept.contains("end"), "the end of the chunk went");
        QVERIFY2(kept.contains("..."), "nothing said that anything was left out");
        QVERIFY(kept.size() < huge.size());
    }

    void testTheFilteredCopyFollowsOutputBeingDropped()
    {
        // The two-document design's own hazard: the filtered copy follows the
        // source by block number, and every one of those moves when the front
        // of the source goes.
        OutputPaneView view;
        view.setMaxCharCount(300);
        view.appendMessage("keep: the first one\n", Utils::GeneralMessageFormat);
        view.flush();
        view.setFilter("keep", {});
        QVERIFY(view.shownDocument()->toPlainText().contains("the first one"));

        for (int i = 0; i < 100; ++i) {
            view.appendMessage(QString("keep: number %1\n").arg(i), Utils::GeneralMessageFormat);
            view.appendMessage(QString("drop: number %1\n").arg(i), Utils::GeneralMessageFormat);
            view.flush();
        }

        const QString filtered = view.shownDocument()->toPlainText();
        QVERIFY2(filtered.contains("keep: number 99"), "the newest matching line was not shown");
        QVERIFY2(!filtered.contains("drop:"), "a line the filter rejects was shown");

        // Every line it shows is one the source still has.
        const QString source = view.toPlainText();
        for (const QString &line : filtered.split('\n', Qt::SkipEmptyParts)) {
            QVERIFY2(source.contains(line),
                     qPrintable("the filtered copy still shows a dropped line: " + line));
        }
    }

    void testOutputIsQueuedRatherThanWrittenAsItArrives()
    {
        OutputPaneView view;

        // Nothing is drawn on the way in. A pane that wrote every chunk the
        // moment it arrived would stop responding while a build runs.
        view.appendMessage("first\n", Utils::StdOutFormat);
        view.appendMessage("second\n", Utils::StdOutFormat);
        QVERIFY2(view.toPlainText().isEmpty(), "output was written before the queue ran");

        // It arrives on its own, without anything asking for it.
        QTRY_VERIFY(view.toPlainText().contains("second"));
        QVERIFY(view.toPlainText().contains("first"));

        // And asking for it explicitly writes what is left immediately.
        view.appendMessage("third\n", Utils::StdOutFormat);
        view.flush();
        QVERIFY(view.toPlainText().contains("third"));
    }

    void testThrowingAwayOutputThatCannotBeKeptUpWith()
    {
        OutputPaneView view;

        // The same flood put to both, so that "off" means the setting and not
        // the fixture. Written faster than chunks are taken off the queue, so
        // the backlog grows however hard the pane works at it.
        const auto flood = [](OutputPaneView &target) {
            for (int i = 0; i < 40; ++i) {
                target.appendMessage(QString(50000, 'x') + '\n', Utils::StdOutFormat);
                target.writeNextChunkForTest();
            }
        };

        // Off unless asked: the pane does not decide on its own to lose a
        // user's build output.
        QSignalSpy quiet(&view, &OutputPaneView::outputDiscarded);
        flood(view);
        QCOMPARE(quiet.count(), 0);

        OutputPaneView discarding;
        discarding.setDiscardExcessiveOutput(true);
        QSignalSpy discarded(&discarding, &OutputPaneView::outputDiscarded);
        flood(discarding);

        QVERIFY2(discarded.count() > 0, "a flood was never discarded");

        // And it says so, rather than the output simply stopping.
        discarding.flush();
        QVERIFY2(discarding.toPlainText().contains("Discarding excessive amount"),
                 "output was thrown away without saying so");
    }

    void testSavingWhatThePaneHolds()
    {
        OutputPaneView view;
        view.appendMessage("first line\n", Utils::GeneralMessageFormat);
        view.appendMessage("second line\n", Utils::GeneralMessageFormat);
        view.flush();

        Utils::TemporaryDirectory temp("outputpaneview-test");
        QVERIFY(temp.isValid());
        const Utils::FilePath file = temp.filePath("saved.txt");

        const Utils::Result<> saved = view.saveContentsTo(file);
        QVERIFY2(saved, qPrintable(saved ? QString() : saved.error()));

        const Utils::Result<QByteArray> written = file.fileContents(-1);
        QVERIFY(written);
        QCOMPARE(QString::fromUtf8(*written), view.toPlainText());

        // Everything, not what a filter happens to be showing: a file of the
        // output is not a file of the current search.
        view.setFilter("second", {});
        QVERIFY(!view.shownDocument()->toPlainText().contains("first line"));
        QVERIFY(view.saveContentsTo(file));
        QVERIFY2(QString::fromUtf8(*file.fileContents(-1)).contains("first line"),
                 "saving while filtered wrote only the lines on screen");
    }

    void testWhatAScratchBufferOfTheOutputIsCalled()
    {
        // The hint is a file name, and the scratch buffer is named after it so
        // that two panes' buffers can be told apart in the editor's tabs.
        QCOMPARE(OutputPaneView::scratchBufferNameTemplate("compile-output.txt"),
                 QString("compile-output-XXXXXX.txt"));

        // A hint with no base name would otherwise give a file called
        // "-XXXXXX.txt", which names nothing.
        QCOMPARE(OutputPaneView::scratchBufferNameTemplate({}),
                 QString("scratch-XXXXXX.txt"));
        QCOMPARE(OutputPaneView::scratchBufferNameTemplate(".txt"),
                 QString("scratch-XXXXXX.txt"));

        // And a pane that never set one still gets a usable name.
        OutputPaneView view;
        QCOMPARE(OutputPaneView::scratchBufferNameTemplate(Tr::tr("output.txt")),
                 QString("output-XXXXXX.txt"));
    }

    // The shape the Serial Terminal and Application Output panes have: one
    // view per connection or run, side by side in a tab widget. Whether views
    // built that way are really independent is not obvious - they share a
    // formatter type, a queue, a factory and a settings key - and the failure
    // if they were not would be output appearing in the wrong tab.
    void testCopyAndSelectAllHaveShortcutsAgain()
    {
        // Core::OutputWindow registered these against the pane's context in
        // its constructor. Nothing did it for a Qt Quick pane, so the context
        // menu offered Copy and Select All while Ctrl+C did nothing - and four
        // panes had already been ported that way.
        OutputPaneView view(Context("Test.OutputPaneView.Shortcuts"));
        view.resize(300, 200);
        view.show();
        QVERIFY(view.view());

        const QList<QAction *> actions = view.findChildren<QAction *>();
        QCOMPARE(actions.size(), 2);

        view.appendMessage("some output to select\n", Utils::GeneralMessageFormat);
        view.flush();
        QTRY_VERIFY(!view.view()->document()->isEmpty());

        // Both are triggered, twice: they are told apart by what they do rather
        // than by the order they were found in, and Copy does nothing until
        // something has been selected.
        QVERIFY(!view.view()->textCursor().hasSelection());
        QGuiApplication::clipboard()->clear();

        for (QAction * const action : actions)
            action->trigger();
        QVERIFY2(view.view()->textCursor().hasSelection(),
                 "neither shortcut selected the output");
        QCOMPARE(view.view()->textCursor().selectedText().trimmed(),
                 QString("some output to select"));

        for (QAction * const action : actions)
            action->trigger();
        QVERIFY2(QGuiApplication::clipboard()->text().contains("some output to select"),
                 "the output was selected but never copied");
    }

    void testAViewWithNoContextClaimsNoCommands()
    {
        // Two views cannot register the same command in the same context, so a
        // pane with one view per run gives each its own and a caller with no
        // context gets no shortcuts rather than a clash.
        OutputPaneView anonymous;
        QCOMPARE(anonymous.findChildren<QAction *>().size(), 0);
    }

    void testASecondFilterOnTopOfTheTextOne()
    {
        // Application Output hides the logging categories the user switched
        // off, and it does that as well as filtering by text rather than
        // instead of it.
        OutputPaneView view;
        for (const QString &line : QStringList{"qt.core: a note", "qt.gui: a note",
                                               "qt.core: a warning", "plain output"}) {
            view.appendMessage(line + '\n', Utils::GeneralMessageFormat);
        }
        view.flush();
        QCOMPARE(view.shownDocument()->toPlainText().count('\n'), 4);

        // On its own, it decides what is shown.
        view.setExtraFilter([](const QString &line) { return line.startsWith("qt.core"); });
        QCOMPARE(view.shownDocument()->toPlainText(),
                 QString("qt.core: a note\nqt.core: a warning"));

        // With a text filter as well, a line has to satisfy both.
        view.setFilter("warning", {});
        QCOMPARE(view.shownDocument()->toPlainText(), QString("qt.core: a warning"));

        // Dropping the text filter leaves the other one in place.
        view.setFilter({}, {});
        QCOMPARE(view.shownDocument()->toPlainText(),
                 QString("qt.core: a note\nqt.core: a warning"));

        // And dropping it puts everything back - and puts the *source*
        // document back on screen, rather than a copy of it that happens to
        // hold everything.
        view.setExtraFilter({});
        QCOMPARE(view.shownDocument()->toPlainText().count('\n'), 4);
        QCOMPARE(view.view()->document(), view.shownDocument());

        // What arrives afterwards is filtered too, not just what was there.
        view.setExtraFilter([](const QString &line) { return line.startsWith("qt.core"); });
        view.appendMessage("qt.gui: later\n", Utils::GeneralMessageFormat);
        view.appendMessage("qt.core: later\n", Utils::GeneralMessageFormat);
        view.flush();
        QVERIFY(view.shownDocument()->toPlainText().contains("qt.core: later"));
        QVERIFY2(!view.shownDocument()->toPlainText().contains("qt.gui: later"),
                 "a line the second filter rejects was shown when it arrived later");
    }

    void testViewsInATabWidgetAreIndependent()
    {
        QTabWidget tabs;
        auto * const first = new OutputPaneView({}, {}, &tabs);
        auto * const second = new OutputPaneView({}, {}, &tabs);
        tabs.addTab(first, "first");
        tabs.addTab(second, "second");
        tabs.resize(400, 200);
        tabs.show();

        first->appendMessage("belongs to the first\n", Utils::GeneralMessageFormat);
        second->appendMessage("belongs to the second\n", Utils::GeneralMessageFormat);
        first->flush();
        second->flush();

        QCOMPARE(first->toPlainText(), QString("belongs to the first\n"));
        QCOMPARE(second->toPlainText(), QString("belongs to the second\n"));

        // Each draws its own document rather than the one made last.
        QVERIFY(first->view());
        QVERIFY(second->view());
        QVERIFY2(first->view() != second->view(), "the two tabs share one view");
        QCOMPARE(first->view()->document(), first->shownDocument());
        QCOMPARE(second->view()->document(), second->shownDocument());

        // A filter in one tab does not hide the other's output.
        first->setFilter("nothing matches", {});
        QVERIFY(first->shownDocument()->toPlainText().isEmpty());
        QCOMPARE(second->shownDocument()->toPlainText(), QString("belongs to the second\n"));

        // Nor does zooming one, which a pane does per tab and then fans out
        // itself - it is not something the views do to each other.
        first->setFontZoom(6);
        QCOMPARE(first->fontZoom(), 6.0f);
        QCOMPARE(second->fontZoom(), 0.0f);

        // And a task in one is not known to the other.
        first->registerPositionOf(7, 1, 0, 0, OutputPaneView::TaskSource::Parsed);
        QVERIFY(first->knowsPositionOf(7));
        QVERIFY2(!second->knowsPositionOf(7), "a task registered in one tab was known to another");
    }

    void testShowingWhereATaskWasReportedFrom()
    {
        OutputPaneView view;
        QVERIFY(view.view());

        view.appendMessage("configuring\n", Utils::GeneralMessageFormat);
        view.appendMessage("main.cpp:1: error: no\n", Utils::GeneralMessageFormat);
        view.appendMessage("   here it is\n", Utils::GeneralMessageFormat);
        view.flush();

        QVERIFY(!view.knowsPositionOf(42));
        view.registerPositionOf(42, 2, 0, 0, OutputPaneView::TaskSource::Parsed);
        QVERIFY(view.knowsPositionOf(42));

        // Clicking the task selects the lines it was reported from, which is
        // what makes the view scroll to them.
        view.showPositionOf(42);
        const QTextCursor selected = view.view()->textCursor();
        QCOMPARE(selected.selectedText().replace(QChar::ParagraphSeparator, '\n'),
                 QString("main.cpp:1: error: no\n   here it is"));

        // The cursor ends on the first line of the task, not below its output:
        // the view scrolls to wherever the cursor is.
        QCOMPARE(selected.position(),
                 view.shownDocument()->findBlockByNumber(1).position());

        // Nothing was registered for a task that has none, and asking does not
        // move the selection.
        view.showPositionOf(43);
        QCOMPARE(view.view()->textCursor().selectedText(), selected.selectedText());

        view.clear();
        QVERIFY2(!view.knowsPositionOf(42), "positions survived the output being cleared");
    }

    void testFindSupportIsReachableFromTheOutputArea()
    {
        // The find tool bar is anchored to the area, not to the view inside
        // it, and finds its way down by asking. Losing that is invisible:
        // Ctrl+F simply does nothing.
        OutputPaneView view;
        view.appendMessage("a needle in here\n", Utils::GeneralMessageFormat);
        view.flush();

        IFindSupport *find = Utils::Aggregation::query<IFindSupport>(view.view());
        QVERIFY2(find, "nothing under the output area answers as find support");
        QCOMPARE(find->findStep("needle", {}), IFindSupport::Found);
    }

    void testShowingSomewhereAPaneWorkedOutForItself()
    {
        // A pane that can say where in its own document something is - the
        // debugger log finding the answer to a command - hands over a cursor
        // and the view selects and shows it.
        OutputPaneView view;
        view.appendMessage("first\nsecond\nthird\n", Utils::GeneralMessageFormat);
        view.flush();

        QTextDocument * const document = view.sourceDocument();
        QTRY_COMPARE(document->blockCount(), 4); // three lines and the unfinished one

        QTextCursor wanted(document->findBlockByNumber(1));
        wanted.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
        view.showCursor(wanted);

        QVERIFY(view.view());
        QCOMPARE(view.view()->textCursor().selectedText(), QString("second"));

        // A null cursor leaves the selection where it was, rather than
        // collapsing it to the top of the document.
        view.showCursor({});
        QCOMPARE(view.view()->textCursor().selectedText(), QString("second"));
    }
};

QObject *createOutputPaneViewTest()
{
    return new OutputPaneViewTest;
}

#endif // WITH_TESTS

} // namespace Core

#ifdef WITH_TESTS
#include "outputpaneview.moc"
#endif
