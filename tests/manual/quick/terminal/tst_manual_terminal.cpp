// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "ptyhost.h"
#include <terminal/terminalquickitem.h>

#include <terminal/surfaceintegration.h>


#include <QClipboard>
#include <QCommandLineParser>
#include <QDir>
#include <QElapsedTimer>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QScreen>
#include <QTimer>

#include <mach/mach.h>

#include <algorithm>
#include <cstdio>

using namespace TerminalSolution;

void installEditMenu(); // editmenu.mm; hosts the system "Emoji & Symbols" item

// --- helpers ---------------------------------------------------------------

static double rssMb()
{
    mach_task_basic_info info;
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, (task_info_t) &info, &count)
        != KERN_SUCCESS)
        return -1;
    return double(info.resident_size) / (1024.0 * 1024.0);
}

struct Summary
{
    size_t n = 0;
    double meanMs = 0, p50Ms = 0, p95Ms = 0, p99Ms = 0, maxMs = 0;
};

static Summary summarize(std::vector<qint64> ns)
{
    Summary s;
    s.n = ns.size();
    if (ns.empty())
        return s;
    std::sort(ns.begin(), ns.end());
    double sum = 0;
    for (qint64 v : ns)
        sum += double(v);
    const auto at = [&](double q) {
        return double(ns[qMin(ns.size() - 1, size_t(q * double(ns.size())))]) / 1e6;
    };
    s.meanMs = sum / double(ns.size()) / 1e6;
    s.p50Ms = at(0.50);
    s.p95Ms = at(0.95);
    s.p99Ms = at(0.99);
    s.maxMs = double(ns.back()) / 1e6;
    return s;
}

static void printSummary(const char *label, const Summary &s)
{
    std::printf("  %-10s n=%zu mean=%.3fms p50=%.3f p95=%.3f p99=%.3f max=%.3f\n",
                label, s.n, s.meanMs, s.p50Ms, s.p95Ms, s.p99Ms, s.maxMs);
}

// Full-screen rewrites via cursor addressing: pure cell churn, no scrollback.
static QByteArray makeChurn(int rows, int cols, int frames)
{
    QByteArray out;
    out.reserve(qsizetype(frames) * rows * (cols + (cols / 8) * 12 + 16));
    int counter = 0;
    for (int f = 0; f < frames; ++f) {
        for (int r = 1; r <= rows; ++r) {
            out += "\x1b[" + QByteArray::number(r) + ";1H";
            for (int c = 0; c < cols; c += 8) {
                out += "\x1b[38;5;" + QByteArray::number(16 + counter % 216)
                       + ((counter % 7 == 0) ? ";1m" : "m");
                for (int i = 0; i < qMin(8, cols - c); ++i)
                    out += char('!' + (counter + i) % 93);
                ++counter;
            }
            out += "\x1b[0m";
        }
    }
    return out;
}

// Colored counting lines: scrolling output that grows the scrollback.
static QByteArray makeScrollSeq(int firstLine, int lineCount)
{
    QByteArray out;
    out.reserve(qsizetype(lineCount) * 24);
    for (int i = firstLine; i < firstLine + lineCount; ++i) {
        out += "\x1b[38;5;" + QByteArray::number(16 + i % 216) + "m"
               + QByteArray::number(i) + "\x1b[0m\r\n";
    }
    return out;
}

static QString gridTextRows(TerminalSurface *surface, int fromRow, int rowCount)
{
    QString result;
    const int cols = surface->liveSize().width();
    const int toRow = qMin(surface->fullSize().height(), fromRow + rowCount);
    for (int y = qMax(0, fromRow); y < toRow; ++y) {
        for (int x = 0; x < cols;) {
            const char32_t ch = surface->fetchCharAt(x, y);
            result += ch ? QString::fromUcs4(&ch, 1) : QStringLiteral(" ");
            x += qMax(1, surface->cellWidthAt(x, y)); // skip wide-char continuation cells
        }
        result += QLatin1Char('\n');
    }
    return result;
}

static QString liveGridText(TerminalSurface *surface)
{
    const int live = surface->liveSize().height();
    return gridTextRows(surface, surface->fullSize().height() - live, live);
}

class FrameRecorder
{
public:
    void attach(QQuickWindow *window)
    {
        m_timer.start();
        QObject::connect(window, &QQuickWindow::frameSwapped, window, [this] {
            const qint64 now = m_timer.nsecsElapsed(); // render thread
            if (m_active.load(std::memory_order_relaxed)) {
                std::lock_guard lock(m_mutex);
                if (m_last >= 0)
                    m_deltas.push_back(now - m_last);
            }
            m_last = now;
        }, Qt::DirectConnection);
    }

    void begin()
    {
        std::lock_guard lock(m_mutex);
        m_deltas.clear();
        m_last = -1;
        m_active = true;
    }

    std::vector<qint64> end()
    {
        m_active = false;
        std::lock_guard lock(m_mutex);
        return std::move(m_deltas);
    }

private:
    QElapsedTimer m_timer;
    std::mutex m_mutex;
    std::vector<qint64> m_deltas;
    qint64 m_last = -1;
    std::atomic<bool> m_active{false};
};

class DemoIntegration : public SurfaceIntegration
{
public:
    explicit DemoIntegration(QQuickWindow *window)
        : m_window(window)
    {}

    void onOsc(int, std::string_view, bool, bool) override {}
    void onBell() override { std::printf("[integration] bell\n"); }
    void onTitle(const QString &title) override { m_window->setTitle(title); }
    void onSetClipboard(const QByteArray &text) override
    {
        QGuiApplication::clipboard()->setText(QString::fromUtf8(text));
    }

private:
    QQuickWindow *m_window;
};

// Mirrors how Creator subclasses the widget view: clipboard binding and the
// searchHits() seam live in the embedder.
class TestTerminalItem : public TerminalQuickItem
{
public:
    using TerminalQuickItem::TerminalQuickItem;

    const QList<SearchHit> &searchHits() const override { return m_hits; }
    void setSearchHits(const QList<SearchHit> &hits)
    {
        m_hits = hits;
        scheduleRefresh();
    }

    // Creator marks the current hit by selecting it (TerminalSearch).
    void selectCurrentHit(const SearchHit &hit)
    {
        setSelection(Selection{hit.start, hit.end, true});
    }

    void setClipboard(const QString &text) override
    {
        QGuiApplication::clipboard()->setText(text);
    }

    // Creator's toLink parses file paths and URLs; a canned URL check stands in.
    std::optional<Link> toLink(const QString &text) override
    {
        if (text.startsWith(QLatin1String("http")))
            return Link{text, 0, 0};
        return std::nullopt;
    }

    void linkActivated(const Link &link) override { m_activatedLinks.append(link.text); }
    QStringList activatedLinks() const { return m_activatedLinks; }

    // The window's frame-synchronous hover refresh follows the real pointer
    // and can clear a synthetic link hover between poll cycles; rebuilding
    // the snapshots synchronously keeps the assertions race-free.
    void forcePolish() { updatePolish(); }

private:
    QList<SearchHit> m_hits;
    QStringList m_activatedLinks;
};

static QString selectionText(TerminalSurface *surface, int start, int end)
{
    auto it = surface->iteratorAt(start);
    const auto endIt = surface->iteratorAt(end);
    std::u32string s;
    bool previousWasZero = false;
    for (; it != endIt; ++it) {
        if (it.gridPos().x() == 0 && !s.empty() && previousWasZero)
            s += U'\n';
        if (*it != 0) {
            previousWasZero = false;
            s += *it;
        } else {
            previousWasZero = true;
        }
    }
    return QString::fromUcs4(s.data(), int(s.size()));
}

// Topmost visible document row whose text starts with prefix.
static int findVisibleRow(TerminalSurface *surface, const QString &prefix)
{
    const int full = surface->fullSize().height();
    for (int y = full - surface->liveSize().height(); y < full; ++y) {
        if (y < 0)
            continue;
        if (gridTextRows(surface, y, 1).trimmed().startsWith(prefix))
            return y;
    }
    return -1;
}

static void saveScreenshot(QQuickWindow *window, const QString &name)
{
    const QString dir = QStringLiteral(RESULTS_DIR);
    QDir().mkpath(dir);
    const QString path = dir + QLatin1Char('/') + name;
    if (window->grabWindow().save(path))
        std::printf("[screenshot] %s\n", qPrintable(path));
    else
        std::printf("[screenshot] FAILED to save %s\n", qPrintable(path));
}

static void printEnvironment(QQuickWindow *window, TerminalQuickItem *item)
{
    std::printf("[env] window=%dx%d dpr=%.1f refresh=%.0fHz grid=%dx%d cell=%.2fx%.0f\n",
                window->width(), window->height(), window->devicePixelRatio(),
                window->screen()->refreshRate(),
                item->gridSize().width(), item->gridSize().height(),
                item->cellSize().width(), item->cellSize().height());
}

// --- canned benchmark driver -----------------------------------------------

class CannedDriver : public QObject
{
public:
    CannedDriver(TerminalQuickItem *item, QQuickWindow *window, const QString &mode)
        : m_item(item)
        , m_window(window)
        , m_mode(mode)
    {
        m_recorder.attach(window);
        connect(window, &QQuickWindow::afterAnimating, this, &CannedDriver::onTick);
        QTimer::singleShot(500, this, [this] {
            printEnvironment(m_window, m_item);
            setupPhases();
            startPhase(0);
            m_item->update(); // kick the frame loop
        });
    }

private:
    struct Phase
    {
        QString name;
        std::function<void()> start;
        std::function<bool()> tick; // true = phase done
    };

    void setupPhases()
    {
        const int rows = m_item->gridSize().height();
        const int cols = m_item->gridSize().width();

        if (m_mode == QLatin1String("churn")) {
            m_phases.push_back({QStringLiteral("blast-churn"),
                                [this, rows, cols] {
                                    m_stream = makeChurn(rows, cols, 800);
                                    m_pos = 0;
                                },
                                [this] { return feedChunk(256 * 1024); }});
            m_phases.push_back({QStringLiteral("paced-churn-1-screen-per-frame"),
                                [this, rows, cols] {
                                    m_stream = makeChurn(rows, cols, 700);
                                    m_pos = 0;
                                    m_screenBytes = m_stream.size() / 700;
                                },
                                [this] { return feedChunk(m_screenBytes); }});
        } else { // scrollseq
            m_phases.push_back({QStringLiteral("blast-scrollseq-200k-lines"),
                                [this] {
                                    m_stream = makeScrollSeq(1, 200000);
                                    m_pos = 0;
                                },
                                [this] { return feedChunk(256 * 1024); }});
            m_phases.push_back({QStringLiteral("paced-scrollseq-64-lines-per-frame"),
                                [this] {
                                    m_stream = makeScrollSeq(200001, 38400); // 600 frames
                                    m_pos = 0;
                                    m_lineFeedFrom = 0;
                                },
                                [this] { return feedLines(64); }});
            m_phases.push_back({QStringLiteral("sweep-slow-4-rows-per-frame"),
                                [this] {
                                    m_item->setScrollOffset(m_item->maxScrollOffset());
                                    m_ticksLeft = 1000;
                                },
                                [this] { return sweep(4); }});
            m_phases.push_back({QStringLiteral("sweep-fast-40-rows-per-frame"),
                                [this] {
                                    m_item->setScrollOffset(m_item->maxScrollOffset());
                                    m_ticksLeft = 600;
                                },
                                [this] { return sweep(40); }});
        }
    }

    bool feedChunk(qint64 chunkBytes)
    {
        if (m_pos >= m_stream.size())
            return true;
        const qint64 chunk = qMin<qint64>(chunkBytes, m_stream.size() - m_pos);
        m_item->surface()->dataFromPty(QByteArray::fromRawData(m_stream.constData() + m_pos,
                                                               chunk));
        m_pos += chunk;
        m_bytesFed += chunk;
        return false;
    }

    bool feedLines(int lines)
    {
        if (m_pos >= m_stream.size())
            return true;
        qint64 end = m_pos;
        for (int i = 0; i < lines && end < m_stream.size(); ++i) {
            const qint64 nl = m_stream.indexOf('\n', end);
            end = nl < 0 ? m_stream.size() : nl + 1;
        }
        m_item->surface()->dataFromPty(QByteArray::fromRawData(m_stream.constData() + m_pos,
                                                               end - m_pos));
        m_bytesFed += end - m_pos;
        m_pos = end;
        return false;
    }

    bool sweep(int rowsPerTick)
    {
        if (m_ticksLeft-- <= 0)
            return true;
        int offset = m_item->scrollOffset() - rowsPerTick;
        if (offset < 0)
            offset = m_item->maxScrollOffset();
        m_item->setScrollOffset(offset);
        return false;
    }

    void startPhase(int index)
    {
        m_phaseIndex = index;
        m_bytesFed = 0;
        m_phases[index].start();
        m_item->beginStats();
        m_recorder.begin();
        m_wall.start();
    }

    void finishPhase()
    {
        const double wallMs = double(m_wall.nsecsElapsed()) / 1e6;
        const Summary frames = summarize(m_recorder.end());
        const TerminalQuickItem::Stats stats = m_item->takeStats();

        std::printf("[phase %s] wall=%.0fms bytes=%.2fMB throughput=%.1fMB/s rss=%.0fMB\n",
                    qPrintable(m_phases[m_phaseIndex].name), wallMs,
                    double(m_bytesFed) / 1e6,
                    wallMs > 0 ? double(m_bytesFed) / 1e6 / (wallMs / 1e3) : 0.0,
                    rssMb());
        printSummary("frame:", frames);
        printSummary("polish:", summarize(stats.polishNs));
        printSummary("paintNode:", summarize(stats.paintNodeNs));
        std::printf("  gridDeviationMaxPx=%.3f\n", stats.maxGridDeviationPx);

        if (m_phaseIndex + 1 < int(m_phases.size())) {
            startPhase(m_phaseIndex + 1);
        } else {
            m_done = true;
            saveScreenshot(m_window, QStringLiteral("canned-") + m_mode + QStringLiteral(".png"));
            QCoreApplication::exit(0);
        }
    }

    void onTick()
    {
        if (m_done || m_phaseIndex < 0)
            return;
        if (m_phases[m_phaseIndex].tick())
            finishPhase();
        if (!m_done)
            m_item->update(); // keep the frame loop alive across phases
    }

    TerminalQuickItem *m_item;
    QQuickWindow *m_window;
    QString m_mode;
    std::vector<Phase> m_phases;
    int m_phaseIndex = -1;
    bool m_done = false;

    QByteArray m_stream;
    qint64 m_pos = 0;
    qint64 m_bytesFed = 0;
    qint64 m_screenBytes = 0;
    qint64 m_lineFeedFrom = 0;
    int m_ticksLeft = 0;

    QElapsedTimer m_wall;
    FrameRecorder m_recorder;
};

// --- selftest driver (real shell) ------------------------------------------

class SelfTest : public QObject
{
public:
    SelfTest(TestTerminalItem *item, QQuickWindow *window, PtyHost *pty)
        : m_item(item)
        , m_window(window)
        , m_pty(pty)
    {
        connect(window, &QQuickWindow::frameSwapped, this,
                [this] { m_frameCount.fetch_add(1, std::memory_order_relaxed); },
                Qt::DirectConnection); // render thread
        setupSteps();
        m_poll.setInterval(50);
        connect(&m_poll, &QTimer::timeout, this, &SelfTest::poll);
        QTimer::singleShot(300, this, [this] {
            printEnvironment(m_window, m_item);
            m_window->requestActivate();
            startStep(0);
        });
    }

private:
    struct Step
    {
        QString name;
        std::function<void()> action;
        std::function<bool()> done;
        int timeoutMs;
    };

    void sendText(const QString &text)
    {
        for (const QChar c : text) {
            const bool isReturn = c == QLatin1Char('\r');
            QKeyEvent event(QEvent::KeyPress, isReturn ? Qt::Key_Return : 0, Qt::NoModifier,
                            QString(c));
            QCoreApplication::sendEvent(m_item, &event);
        }
    }

    QPointF cellCenter(int documentRow, int column) const
    {
        const qreal cellW = m_item->cellSize().width();
        const qreal cellH = m_item->cellSize().height();
        const qreal top = m_item->height() - m_item->gridSize().height() * cellH;
        return {column * cellW + cellW / 2,
                top + (documentRow - m_item->scrollOffset()) * cellH + cellH / 2};
    }

    void sendMouse(QEvent::Type type, const QPointF &pos, Qt::MouseButton button,
                   Qt::MouseButtons buttons, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
    {
        QMouseEvent event(type, pos, m_item->mapToScene(pos), m_item->mapToGlobal(pos),
                          button, buttons, modifiers);
        QCoreApplication::sendEvent(m_item, &event);
    }

    void sendShortcut(Qt::Key key, const QString &text)
    {
        QKeyEvent event(QEvent::KeyPress, key, Qt::ControlModifier, text);
        QCoreApplication::sendEvent(m_item, &event);
    }

    void sendHover(const QPointF &pos, Qt::KeyboardModifiers modifiers)
    {
        QHoverEvent event(QEvent::HoverMove, pos, m_item->mapToGlobal(pos), pos, modifiers);
        QCoreApplication::sendEvent(m_item, &event);
    }

    void sendKeyRelease(Qt::Key key)
    {
        QKeyEvent event(QEvent::KeyRelease, key, Qt::NoModifier);
        QCoreApplication::sendEvent(m_item, &event);
    }

    void sendPreedit(const QString &preedit)
    {
        QInputMethodEvent event(preedit, {});
        QCoreApplication::sendEvent(m_item, &event);
    }

    void sendCommit(const QString &commit)
    {
        QInputMethodEvent event;
        event.setCommitString(commit);
        QCoreApplication::sendEvent(m_item, &event);
    }

    void sendZoomWheel(int angleDeltaY)
    {
        const QPointF pos(10, 10);
        QWheelEvent event(pos, m_item->mapToGlobal(pos), QPoint(), QPoint(0, angleDeltaY),
                          Qt::NoButton, Qt::ControlModifier, Qt::NoScrollPhase, false);
        QCoreApplication::sendEvent(m_item, &event);
    }

    void setupSteps()
    {
        TerminalSurface *surface = m_item->surface();

        m_steps.push_back({QStringLiteral("shell prompt appears"),
                           [this] { m_item->beginStats(); },
                           [surface] { return liveGridText(surface).contains(QLatin1Char('%')); },
                           10000});
        m_steps.push_back({QStringLiteral("keyboard round trip (echo spike-$((6*7)) -> spike-42)"),
                           [this] { sendText(QStringLiteral("echo spike-$((6*7))\r")); },
                           [surface] {
                               return liveGridText(surface).contains(QLatin1String("spike-42"));
                           },
                           10000});
        m_steps.push_back({QStringLiteral("DECSCUSR 5 sets blinking cursor prop"),
                           [this] { sendText(QStringLiteral("printf '\\e[5 q'\r")); },
                           [surface] { return surface->cursor().blink; },
                           5000});
        m_steps.push_back({QStringLiteral("cursor blink timer toggles (needs window focus)"),
                           [this] {
                               m_blinkBase = m_item->blinkToggleCount();
                               std::printf("[selftest] item hasActiveFocus=%d blinkTimerActive=%d\n",
                                           m_item->hasActiveFocus(),
                                           m_item->cursorBlinkTimerActive());
                           },
                           [this] { return m_item->blinkToggleCount() >= m_blinkBase + 2; },
                           4000});
        m_steps.push_back({QStringLiteral("DECSCUSR 2 restores steady cursor"),
                           [this] { sendText(QStringLiteral("printf '\\e[2 q'\r")); },
                           [surface] { return !surface->cursor().blink; },
                           5000});
        m_steps.push_back({QStringLiteral("altscreen enter (printf 1049h)"),
                           [this] { sendText(QStringLiteral("printf '\\e[?1049h'\r")); },
                           [surface] { return surface->isInAltScreen(); },
                           5000});
        m_steps.push_back({QStringLiteral("altscreen leave (printf 1049l)"),
                           [this] { sendText(QStringLiteral("printf '\\e[?1049l'\r")); },
                           [surface] { return !surface->isInAltScreen(); },
                           5000});
        m_steps.push_back({QStringLiteral("wide chars render (CJK via fallback font)"),
                           [this] { sendText(QStringLiteral("echo 漢字テストwide\r")); },
                           [surface] {
                               if (!liveGridText(surface).contains(QStringLiteral("漢字テストwide")))
                                   return false;
                               // reported at the end via gridDeviationMaxPx
                               return true;
                           },
                           10000});
        m_steps.push_back({QStringLiteral("live shell: seq 1 50000"),
                           [this] {
                               m_seqTimer.start();
                               sendText(QStringLiteral("seq 1 50000; echo SPIKE-DONE\r"));
                           },
                           [this, surface] {
                               if (!liveGridText(surface).contains(QLatin1String("SPIKE-DONE")))
                                   return false;
                               std::printf("[measure] live seq 1 50000 wall=%.0fms rss=%.0fMB "
                                           "scrollbackRows=%d\n",
                                           double(m_seqTimer.nsecsElapsed()) / 1e6, rssMb(),
                                           m_item->maxScrollOffset());
                               return true;
                           },
                           60000});
        m_steps.push_back({QStringLiteral("scrollback: offset 0 shows the first command"),
                           [this] { m_item->setScrollOffset(0); },
                           [surface] {
                               return gridTextRows(surface, 0, 40)
                                   .contains(QLatin1String("spike-42"));
                           },
                           3000});
        m_steps.push_back({QStringLiteral("scroll back down"),
                           [this] { m_item->setScrollOffset(m_item->maxScrollOffset()); },
                           [this] { return m_item->scrollOffset() == m_item->maxScrollOffset(); },
                           3000});
        // Columns in the marker row: "select-alpha" 0-11, "bravo-target" 13-24.
        m_steps.push_back({QStringLiteral("marker line for the selection tests"),
                           [this] {
                               sendText(QStringLiteral(
                                   "echo select-alpha bravo-target charlie\r"));
                           },
                           [surface] {
                               const int row = findVisibleRow(
                                   surface, QStringLiteral("select-alpha bravo-target charlie"));
                               if (row < 0)
                                   return false;
                               // the prompt below proves the output is complete
                               return gridTextRows(surface, row + 1, 5)
                                   .contains(QLatin1Char('%'));
                           },
                           10000});
        m_steps.push_back({QStringLiteral("mouse drag selects computed cells"),
                           [this, surface] {
                               m_markerRow = findVisibleRow(
                                   surface, QStringLiteral("select-alpha bravo-target charlie"));
                               if (m_markerRow < 0)
                                   return;
                               const QPointF from = cellCenter(m_markerRow, 13);
                               const QPointF to = cellCenter(m_markerRow, 25);
                               sendMouse(QEvent::MouseButtonPress, from, Qt::LeftButton,
                                         Qt::LeftButton);
                               sendMouse(QEvent::MouseMove, cellCenter(m_markerRow, 18),
                                         Qt::NoButton, Qt::LeftButton);
                               sendMouse(QEvent::MouseMove, to, Qt::NoButton, Qt::LeftButton);
                               sendMouse(QEvent::MouseButtonRelease, to, Qt::LeftButton,
                                         Qt::NoButton);
                           },
                           [this, surface] {
                               const auto sel = m_item->selection();
                               if (!sel || !sel->final)
                                   return false;
                               return selectionText(surface, sel->start, sel->end)
                                      == QLatin1String("bravo-target");
                           },
                           5000});
        m_steps.push_back({QStringLiteral("copy shortcut fills the clipboard"),
                           [this] {
                               QGuiApplication::clipboard()->setText(
                                   QStringLiteral("sentinel-unchanged"));
                               sendShortcut(Qt::Key_C, QStringLiteral("c"));
                           },
                           [] {
                               return QGuiApplication::clipboard()->text()
                                      == QLatin1String("bravo-target");
                           },
                           5000});
        m_steps.push_back({QStringLiteral("double-click selects the word under the cursor"),
                           [this] {
                               if (m_markerRow < 0)
                                   return;
                               const QPointF at = cellCenter(m_markerRow, 15);
                               sendMouse(QEvent::MouseButtonDblClick, at, Qt::LeftButton,
                                         Qt::LeftButton);
                               sendMouse(QEvent::MouseButtonRelease, at, Qt::LeftButton,
                                         Qt::NoButton);
                           },
                           [this, surface] {
                               const auto sel = m_item->selection();
                               if (!sel || !sel->final)
                                   return false;
                               return selectionText(surface, sel->start, sel->end)
                                      == QLatin1String("bravo-target");
                           },
                           5000});
        m_steps.push_back({QStringLiteral("paste shortcut round-trips through the pty"),
                           [this] {
                               QGuiApplication::clipboard()->setText(
                                   QStringLiteral("echo paste-sentinel-$((11*13))"));
                               sendShortcut(Qt::Key_V, QStringLiteral("v"));
                               sendText(QStringLiteral("\r"));
                           },
                           [surface] {
                               return liveGridText(surface)
                                   .contains(QLatin1String("paste-sentinel-143"));
                           },
                           10000});
        m_steps.push_back({QStringLiteral("search hits render findmatch and selection colors"),
                           [this, surface] {
                               m_markerRow = findVisibleRow(
                                   surface, QStringLiteral("select-alpha bravo-target charlie"));
                               if (m_markerRow < 0)
                                   return;
                               const int base = surface->gridToPos({0, m_markerRow});
                               const SearchHit alpha{base, base + 12};
                               const SearchHit bravo{base + 13, base + 25};
                               m_item->setSearchHits({alpha, bravo});
                               m_item->selectCurrentHit(bravo);
                           },
                           [this] {
                               if (m_markerRow < 0)
                                   return false;
                               const QColor find = m_item->paletteColor(
                                   int(TerminalQuickItem::ItemColorIdx::FindMatch));
                               const QColor sel = m_item->paletteColor(
                                   int(TerminalQuickItem::ItemColorIdx::Selection));
                               bool haveFind = false;
                               bool haveSel = false;
                               const auto runs = m_item->visibleRuns(m_markerRow);
                               for (const TerminalQuickItem::VisibleRun &run : runs) {
                                   for (const QTextLayout::FormatRange &range : run.formats) {
                                       const QColor bg = range.format.background().color();
                                       haveFind = haveFind || bg == find;
                                       haveSel = haveSel || bg == sel;
                                   }
                               }
                               return haveFind && haveSel;
                           },
                           5000});
        m_steps.push_back({QStringLiteral("CJK glyphs sit on exact grid columns"),
                           [this] {
                               m_item->setSearchHits({});
                               sendText(QStringLiteral("echo \u6f22\u5b57\u30c6\u30b9\u30c8END\r"));
                           },
                           [this, surface] {
                               const int row = findVisibleRow(
                                   surface,
                                   QStringLiteral("\u6f22\u5b57\u30c6\u30b9\u30c8END"));
                               if (row < 0)
                                   return false;
                               // 5 wide chars = 10 columns before "END"
                               const qreal expected = 10 * m_item->cellSize().width();
                               const auto runs = m_item->visibleRuns(row);
                               for (const TerminalQuickItem::VisibleRun &run : runs) {
                                   if (run.text.startsWith(QLatin1String("END")))
                                       return qAbs(run.x - expected) < 0.5;
                               }
                               return false;
                           },
                           10000});
        m_steps.push_back({QStringLiteral("IME preedit paints underlined at the cursor cell"),
                           [this, surface] {
                               m_preeditCell = surface->cursor().position;
                               sendPreedit(QStringLiteral("ime-preedit"));
                           },
                           [this] {
                               const auto runs = m_item->preeditRuns();
                               if (runs.size() != 1)
                                   return false;
                               const TerminalQuickItem::VisibleRun &run = runs.first();
                               if (run.text != QLatin1String("ime-preedit"))
                                   return false;
                               const qreal expectedX = m_preeditCell.x()
                                                       * m_item->cellSize().width();
                               if (qAbs(run.x - expectedX) > 0.5)
                                   return false;
                               return !run.formats.isEmpty()
                                      && run.formats.first().format.fontUnderline();
                           },
                           5000});
        m_steps.push_back({QStringLiteral("IME commit reaches the shell, preedit clears"),
                           [this] {
                               sendCommit(QStringLiteral("echo ime-$((3*19))"));
                               sendText(QStringLiteral("\r"));
                           },
                           [this, surface] {
                               return liveGridText(surface).contains(QLatin1String("ime-57"))
                                      && m_item->preeditString().isEmpty()
                                      && m_item->preeditRuns().isEmpty();
                           },
                           10000});
        m_steps.push_back({QStringLiteral("link marker line appears"),
                           [this] {
                               sendText(QStringLiteral(
                                   "echo http://example.com/spike-link\r"));
                           },
                           [this, surface] {
                               m_linkRow = findVisibleRow(
                                   surface, QStringLiteral("http://example.com/spike-link"));
                               if (m_linkRow < 0)
                                   return false;
                               return gridTextRows(surface, m_linkRow + 1, 5)
                                   .contains(QLatin1Char('%'));
                           },
                           10000});
        // The real pointer can sit inside the window and its frame-synchronous
        // hover refresh clears synthetic link hovers, so each poll re-arms the
        // hover and asserts synchronously (no event loop in between).
        m_steps.push_back({QStringLiteral("modifier hover shows hand cursor and dashed link"),
                           [] {},
                           [this] {
                               sendHover(cellCenter(m_linkRow, 5), Qt::ControlModifier);
                               if (m_item->cursor().shape() != Qt::PointingHandCursor)
                                   return false;
                               const auto link = m_item->linkSelection();
                               if (!link
                                   || link->link.text
                                          != QLatin1String("http://example.com/spike-link"))
                                   return false;
                               m_item->forcePolish();
                               const auto runs = m_item->visibleRuns(m_linkRow);
                               for (const TerminalQuickItem::VisibleRun &run : runs) {
                                   for (const QTextLayout::FormatRange &range : run.formats) {
                                       if (range.format.underlineStyle()
                                           == QTextCharFormat::DashUnderline)
                                           return true;
                                   }
                               }
                               return false;
                           },
                           5000});
        m_steps.push_back({QStringLiteral("modifier click activates the link seam"),
                           [this] {
                               const QPointF at = cellCenter(m_linkRow, 5);
                               sendHover(at, Qt::ControlModifier);
                               sendMouse(QEvent::MouseButtonPress, at, Qt::LeftButton,
                                         Qt::LeftButton, Qt::ControlModifier);
                               sendMouse(QEvent::MouseButtonRelease, at, Qt::LeftButton,
                                         Qt::NoButton, Qt::ControlModifier);
                           },
                           [this] {
                               return m_item->activatedLinks()
                                      == QStringList{QStringLiteral(
                                          "http://example.com/spike-link")};
                           },
                           5000});
        m_steps.push_back({QStringLiteral("modifier release clears the link hover"),
                           [] {},
                           [this] {
                               sendHover(cellCenter(m_linkRow, 5), Qt::ControlModifier);
                               if (!m_item->linkSelection())
                                   return false;
                               sendKeyRelease(Qt::Key_Control);
                               if (m_item->linkSelection())
                                   return false;
                               if (m_item->cursor().shape() != Qt::IBeamCursor)
                                   return false;
                               m_item->forcePolish();
                               const auto runs = m_item->visibleRuns(m_linkRow);
                               for (const TerminalQuickItem::VisibleRun &run : runs) {
                                   for (const QTextLayout::FormatRange &range : run.formats) {
                                       if (range.format.underlineStyle()
                                           == QTextCharFormat::DashUnderline)
                                           return false;
                                   }
                               }
                               return true;
                           },
                           5000});
        m_steps.push_back({QStringLiteral("ctrl+wheel zoom-in grows cells, resizes the pty"),
                           [this] {
                               m_cellWBefore = m_item->cellSize().width();
                               m_gridBefore = m_item->gridSize();
                               m_resizeBase = m_pty->resizeCount();
                               sendZoomWheel(120);
                           },
                           [this] {
                               return m_item->cellSize().width() > m_cellWBefore
                                      && m_item->gridSize().width() < m_gridBefore.width()
                                      && m_pty->resizeCount() > m_resizeBase
                                      && m_pty->lastGrid() == m_item->gridSize();
                           },
                           5000});
        m_steps.push_back({QStringLiteral("ctrl+wheel zoom-out restores the grid"),
                           [this] {
                               m_resizeBase = m_pty->resizeCount();
                               sendZoomWheel(-120);
                           },
                           [this] {
                               return qAbs(m_item->cellSize().width() - m_cellWBefore) < 0.01
                                      && m_item->gridSize() == m_gridBefore
                                      && m_pty->resizeCount() > m_resizeBase
                                      && m_pty->lastGrid() == m_item->gridSize();
                           },
                           5000});
        m_steps.push_back(
            {QStringLiteral("wavy and dashed underline formats reach the layout"),
             [this] {
                 sendText(QStringLiteral("printf 'UL \\e[4:3mWWWW\\e[0m \\e[4:5mDDDD\\e[0m "
                                         "\\e[4mSSSS\\e[0m\\n'\r"));
             },
             [this, surface] {
                 m_ulRow = findVisibleRow(surface, QStringLiteral("UL WWWW DDDD SSSS"));
                 if (m_ulRow < 0)
                     return false;
                 bool haveWave = false, haveDash = false, haveSolid = false;
                 const auto runs = m_item->visibleRuns(m_ulRow);
                 for (const TerminalQuickItem::VisibleRun &run : runs) {
                     for (const QTextLayout::FormatRange &range : run.formats) {
                         switch (range.format.underlineStyle()) {
                         case QTextCharFormat::WaveUnderline: haveWave = true; break;
                         case QTextCharFormat::DashUnderline: haveDash = true; break;
                         case QTextCharFormat::SingleUnderline: haveSolid = true; break;
                         default: break;
                         }
                     }
                 }
                 if (!(haveWave && haveDash && haveSolid))
                     return false;
                 measureUnderlinePixels(); // rendering evidence, reported, not asserted
                 return true;
             },
             10000});
        // ZLE also keeps ECHO off at the ordinary prompt, so grid markers
        // bound the read; echoOff() confirms the hidden-input phase itself.
        m_steps.push_back(
            {QStringLiteral("password prompt swaps the cursor for the lock"),
             [this] {
                 sendText(QStringLiteral(
                     "echo pw-start; read -s line; echo pw-done-$line\r"));
             },
             [this, surface] {
                 if (findVisibleRow(surface, QStringLiteral("pw-start")) < 0
                     || !m_pty->echoOff())
                     return false;
                 m_item->setPasswordMode(true); // Creator wires this from Pty input flags
                 return m_item->passwordLockVisible();
             },
             10000});
        m_steps.push_back(
            {QStringLiteral("leaving password mode restores the cursor"),
             [this] { sendText(QStringLiteral("secret\r")); },
             [this, surface] {
                 if (findVisibleRow(surface, QStringLiteral("pw-done-secret")) < 0)
                     return false;
                 m_item->setPasswordMode(false);
                 return !m_item->passwordLockVisible() && m_item->cursorRectVisible();
             },
             10000});
        // Bounded pty reads must let the frame loop breathe: frames have to be
        // produced while the burst is still printing, not only afterwards.
        m_steps.push_back(
            {QStringLiteral("burst output renders frames during the feed"),
             [this] {
                 m_burstFrameBase = m_frameCount.load(std::memory_order_relaxed);
                 m_seqTimer.start();
                 sendText(QStringLiteral("seq 1 200000; echo BURST-DONE\r"));
             },
             [this, surface] {
                 if (!liveGridText(surface).contains(QLatin1String("BURST-DONE")))
                     return false;
                 const int frames = m_frameCount.load(std::memory_order_relaxed)
                                    - m_burstFrameBase;
                 if (!m_burstPrinted) {
                     m_burstPrinted = true;
                     std::printf("[measure] burst seq 1 200000 wall=%.0fms "
                                 "frames-during-feed=%d\n",
                                 double(m_seqTimer.nsecsElapsed()) / 1e6, frames);
                 }
                 return frames >= 10;
             },
             60000});
        // Guards the raw-control-byte path past the IM/shortcut handling:
        // a real ctrl+c arrives as Key_C + MetaModifier with text \x03 on
        // macOS and must interrupt the foreground command.
        m_steps.push_back(
            {QStringLiteral("ctrl+c interrupts the foreground command"),
             [this] { sendText(QStringLiteral("echo pre-int; sleep 30; echo post-int\r")); },
             [this, surface] {
                 const int preRow = findVisibleRow(surface, QStringLiteral("pre-int"));
                 if (preRow < 0)
                     return false;
                 if (!m_interruptSent) {
                     m_interruptSent = true;
                     QKeyEvent event(QEvent::KeyPress, Qt::Key_C, Qt::MetaModifier,
                                     QString(QChar(0x03)));
                     QCoreApplication::sendEvent(m_item, &event);
                     return false;
                 }
                 // A prompt below pre-int proves the list was aborted; were
                 // post-int coming it would precede the prompt.
                 const QString below = gridTextRows(surface, preRow + 1, 5);
                 return below.contains(QLatin1Char('%'))
                        && !below.contains(QLatin1String("post-int"));
             },
             15000});
    }

    // Pixel evidence for the wavy/dashed verdict: count non-background pixels
    // in the band below the baseline where only underlines paint (the samples
    // have no descenders). solid > 0 proves the probe itself works.
    void measureUnderlinePixels()
    {
        const QImage img = m_window->grabWindow();
        const qreal dpr = m_window->devicePixelRatio();
        const qreal cellW = m_item->cellSize().width();
        const qreal cellH = m_item->cellSize().height();
        const qreal top = m_item->height() - m_item->gridSize().height() * cellH;
        const qreal rowY = top + (m_ulRow - m_item->scrollOffset()) * cellH;
        const qreal ascent = QFontMetricsF(m_item->font()).ascent();
        const QRgb bg = m_item->paletteColor(17).rgb(); // ColorIndex::Background

        const auto bandCount = [&](int fromCol, int cols) {
            int count = 0;
            const int x0 = int(fromCol * cellW * dpr);
            const int x1 = int((fromCol + cols) * cellW * dpr);
            const int y0 = int((rowY + ascent + 1) * dpr);
            const int y1 = int((rowY + cellH) * dpr);
            for (int y = y0; y < y1 && y < img.height(); ++y) {
                for (int x = x0; x < x1 && x < img.width(); ++x) {
                    if (img.pixel(x, y) != bg)
                        ++count;
                }
            }
            return count;
        };
        // columns in "UL WWWW DDDD SSSS": WWWW at 3, DDDD at 8, SSSS at 13
        const int wave = bandCount(3, 4);
        const int dash = bandCount(8, 4);
        const int solid = bandCount(13, 4);
        std::printf("[underline-pixels] wave=%d dash=%d solid=%d\n", wave, dash, solid);

        const QString dir = QStringLiteral(RESULTS_DIR);
        QDir().mkpath(dir);
        const QRect crop(0, qMax(0, int((rowY - cellH) * dpr)), img.width(),
                         int(3 * cellH * dpr));
        img.copy(crop).save(dir + QLatin1String("/selftest-underlines.png"));
    }

    void startStep(int index)
    {
        m_stepIndex = index;
        m_deadline = QDeadlineTimer(m_steps[index].timeoutMs);
        m_steps[index].action();
        m_poll.start();
    }

    void poll()
    {
        const Step &step = m_steps[m_stepIndex];
        bool done = step.done();
        bool failed = false;
        if (!done && m_deadline.hasExpired()) {
            failed = true;
            done = true;
        }
        if (!done)
            return;

        m_poll.stop();
        if (failed)
            ++m_failures;
        std::printf("[selftest] %-55s %s\n", qPrintable(step.name), failed ? "FAIL" : "PASS");

        if (m_stepIndex + 1 < int(m_steps.size())) {
            startStep(m_stepIndex + 1);
        } else {
            const TerminalQuickItem::Stats stats = m_item->takeStats();
            std::printf("[measure] shell session polish mean=%.3fms gridDeviationMaxPx=%.3f "
                        "(cell=%.2fpx)\n",
                        summarize(stats.polishNs).meanMs, stats.maxGridDeviationPx,
                        m_item->cellSize().width());
            saveScreenshot(m_window, QStringLiteral("selftest-shell.png"));
            std::printf("[selftest] %s (%d failures)\n", m_failures ? "FAILED" : "ALL PASSED",
                        m_failures);
            QCoreApplication::exit(m_failures);
        }
    }

    TestTerminalItem *m_item;
    QQuickWindow *m_window;
    PtyHost *m_pty;
    std::vector<Step> m_steps;
    int m_stepIndex = -1;
    int m_failures = 0;
    QTimer m_poll;
    QDeadlineTimer m_deadline;
    QElapsedTimer m_seqTimer;
    int m_blinkBase = 0;
    int m_markerRow = -1;
    int m_linkRow = -1;
    int m_ulRow = -1;
    QPoint m_preeditCell;
    qreal m_cellWBefore = 0;
    QSize m_gridBefore;
    int m_resizeBase = 0;
    std::atomic<int> m_frameCount{0};
    int m_burstFrameBase = 0;
    bool m_burstPrinted = false;
    bool m_interruptSent = false;
};

// --- headless model benchmark ----------------------------------------------

static int benchModel()
{
    const int cols = 194;
    const int rows = 55;

    const auto run = [&](const char *name, const QByteArray &stream) {
        TerminalSurface surface(QSize(cols, rows));
        surface.setWriteToPty([](const QByteArray &data) { return data.size(); });
        QElapsedTimer timer;
        timer.start();
        constexpr qint64 chunkSize = 256 * 1024;
        for (qint64 pos = 0; pos < stream.size(); pos += chunkSize) {
            surface.dataFromPty(QByteArray::fromRawData(
                stream.constData() + pos, qMin(chunkSize, stream.size() - pos)));
        }
        const double wallMs = double(timer.nsecsElapsed()) / 1e6;
        std::printf("[bench-model %s] grid=%dx%d bytes=%.2fMB wall=%.0fms throughput=%.1fMB/s "
                    "scrollbackRows=%d rss=%.0fMB\n",
                    name, cols, rows, double(stream.size()) / 1e6, wallMs,
                    double(stream.size()) / 1e6 / (wallMs / 1e3),
                    surface.fullSize().height() - surface.liveSize().height(), rssMb());
    };
    run("churn", makeChurn(rows, cols, 800));
    run("scrollseq-200k", makeScrollSeq(1, 200000));
    return 0;
}

// --- main --------------------------------------------------------------------

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    installEditMenu();

    QCommandLineParser parser;
    parser.addHelpOption();
    const QCommandLineOption cannedOption("canned", "Run canned benchmark.", "churn|scrollseq");
    const QCommandLineOption benchModelOption("bench-model", "Headless surface benchmark.");
    const QCommandLineOption selftestOption("selftest", "Scripted real-shell verification.");
    const QCommandLineOption demoOption("demo", "Run showcase commands and screenshot.");
    parser.addOption(cannedOption);
    parser.addOption(benchModelOption);
    parser.addOption(selftestOption);
    parser.addOption(demoOption);
    parser.process(app);

    if (parser.isSet(benchModelOption))
        return benchModel();

    QQmlApplicationEngine engine;
    qmlRegisterType<TestTerminalItem>("TerminalManualTest", 1, 0, "TerminalQuickItem");
    engine.load(QUrl(QStringLiteral("qrc:/terminalmanual/Main.qml")));
    if (engine.rootObjects().isEmpty())
        return 1;

    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    // The registered type is TestTerminalItem, so the downcast is exact.
    auto *item = static_cast<TestTerminalItem *>(window->findChild<TerminalQuickItem *>("term"));
    if (!item) {
        std::fprintf(stderr, "no terminal item\n");
        return 1;
    }

    DemoIntegration integration(window);
    item->setSurfaceIntegration(&integration);

    std::unique_ptr<CannedDriver> cannedDriver;
    std::unique_ptr<SelfTest> selfTest;
    std::unique_ptr<PtyHost> pty;

    if (parser.isSet(cannedOption)) {
        cannedDriver = std::make_unique<CannedDriver>(item, window, parser.value(cannedOption));
    } else {
        pty = std::make_unique<PtyHost>();
        QObject::connect(pty.get(), &PtyHost::dataAvailable, item,
                         [item](const QByteArray &data) { item->surface()->dataFromPty(data); });
        QObject::connect(pty.get(), &PtyHost::finished, &app, [] { QCoreApplication::quit(); });
        item->setWriteToPty([&pty](const QByteArray &data) { return pty->write(data); });
        item->setResizePty([&pty](const QSize &size) { pty->resize(size); });
        if (!pty->start(item->gridSize())) {
            std::fprintf(stderr, "forkpty failed\n");
            return 1;
        }
        if (parser.isSet(selftestOption))
            selfTest = std::make_unique<SelfTest>(item, window, pty.get());
        if (parser.isSet(demoOption)) {
            // Showcase: colours, styles, unicode and real repo output, then a
            // screenshot. Delays, not assertions - this produces a picture.
            QTimer::singleShot(1200, item, [&pty] {
                const char *cmds[] = {
                    "clear\r",
                    "printf '\\e[1;38;5;75m  Qt Creator :: Qt Quick terminal spike"
                    "\\e[0m  (QQuickItem over TerminalSurface, QSGTextNode)\\n\\n'\r",
                    "for i in {0..255}; do printf \"\\e[48;5;${i}m \"; done; "
                    "printf '\\e[0m\\n\\n'\r",
                    "printf '\\e[1mbold\\e[0m \\e[3mitalic\\e[0m "
                    "\\e[4munderline\\e[0m \\e[7mreverse\\e[0m "
                    "\\e[9mstrike\\e[0m \\e[38;5;208morange\\e[0m "
                    "\\e[92mgreen\\e[0m \\e[96mcyan\\e[0m "
                    "\xe2\x94\x8c\xe2\x94\x80 \xe6\xbc\xa2\xe5\xad\x97"
                    "\xe3\x83\x86\xe3\x82\xb9\xe3\x83\x88 "
                    "\xe2\x94\x80\xe2\x94\x90\\n\\n'\r",
                    "CLICOLOR_FORCE=1 ls -G "
                    "~/projects/qt/qtc-work/master/src/libs/solutions/terminal/\r",
                    "git -C ~/projects/qt/qtc-work/master log --graph --color=always "
                    "--pretty='%C(auto)%h%d %s' -12\r",
                };
                for (const char *cmd : cmds)
                    pty->write(cmd);
            });
            auto *w = window;
            QTimer::singleShot(4500, w, [w] {
                saveScreenshot(w, QStringLiteral("demo-shell.png"));
                QCoreApplication::quit();
            });
        }
    }

    return app.exec();
}
