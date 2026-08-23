// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "terminalquickitem.h"

#include <QClipboard>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QQuickWindow>
#include <QSGRectangleNode>
#include <QSGTextNode>
#include <QtMath>

#include <algorithm>

namespace TerminalSolution {

using namespace std::chrono;
using namespace std::chrono_literals;

TerminalQuickItem::TerminalQuickItem(QQuickItem *parent)
    : QQuickItem(parent)
{
    setFlag(ItemHasContents);
    setAcceptedMouseButtons(Qt::AllButtons);
    setCursor(Qt::IBeamCursor);

    m_font = QFont(QStringLiteral("Menlo"), 12);
    m_font.setFixedPitch(true);
    const QFontMetricsF fm(m_font);
    m_cellSize = QSizeF(fm.averageCharWidth(), qCeil(fm.height()));
    m_charAdvance.fill(-1);

    // A dark xterm-ish palette; Creator feeds these from TerminalSettings.
    const char *colors[] = {"#000000", "#cd3131", "#0dbc79", "#e5e510", "#2472c8",
                            "#bc3fbc", "#11a8cd", "#e5e5e5", "#666666", "#f14c4c",
                            "#23d18b", "#f5f543", "#3b8eea", "#d670d6", "#29b8db",
                            "#ffffff", "#dfdfdf", "#1e1e1e", "#264f78", "#515c6a"};
    for (size_t i = 0; i < m_palette.size(); ++i)
        m_palette[i] = QColor(QLatin1String(colors[i]));

    m_surface = std::make_unique<TerminalSurface>(QSize{80, 24});
    m_surface->setWriteToPty([this](const QByteArray &data) {
        return m_writeToPty ? m_writeToPty(data) : data.size();
    });

    connect(m_surface.get(), &TerminalSurface::invalidated, this, [this](const QRect &) {
        setSelection(std::nullopt);
        scheduleRefresh();
    });
    connect(m_surface.get(), &TerminalSurface::fullSizeChanged, this, [this](const QSize &) {
        scheduleRefresh();
    });
    connect(m_surface.get(), &TerminalSurface::cursorChanged, this,
            [this](const Cursor &, const Cursor &newCursor) {
                m_cursor = newCursor;
                configBlinkTimer();
                scheduleRefresh();
            });
    connect(m_surface.get(), &TerminalSurface::unscroll, this, [this] {
        m_followOutput = true;
        scheduleRefresh();
    });
    connect(m_surface.get(), &TerminalSurface::altscreenChanged, this, [this](bool) {
        scheduleRefresh();
    });
    connect(m_surface.get(), &TerminalSurface::cleared, this, [this] { scheduleRefresh(); });

    m_blinkTimer.setInterval(750);
    connect(&m_blinkTimer, &QTimer::timeout, this, [this] {
        m_cursorBlinkState = hasActiveFocus() ? !m_cursorBlinkState : true;
        ++m_blinkToggles;
        scheduleRefresh();
    });

    m_scrollTimer.setInterval(500);
    connect(&m_scrollTimer, &QTimer::timeout, this, [this] {
        if (m_scrollDirection < 0)
            setScrollOffset(m_scrollOffset - 1);
        else if (m_scrollDirection > 0)
            setScrollOffset(m_scrollOffset + 1);
    });

    m_cursor = m_surface->cursor();
}

TerminalQuickItem::~TerminalQuickItem() = default;

void TerminalQuickItem::setWriteToPty(WriteToPty writeToPty)
{
    m_writeToPty = std::move(writeToPty);
}

void TerminalQuickItem::setResizePty(ResizePty resizePty)
{
    m_resizePty = std::move(resizePty);
}

void TerminalQuickItem::setSurfaceIntegration(SurfaceIntegration *integration)
{
    m_surface->setSurfaceIntegration(integration);
}

int TerminalQuickItem::maxScrollOffset() const
{
    return qMax(0, m_surface->fullSize().height() - m_surface->liveSize().height());
}

void TerminalQuickItem::setScrollOffset(int offset)
{
    const int clamped = qBound(0, offset, maxScrollOffset());
    m_followOutput = clamped == maxScrollOffset();
    if (clamped == m_scrollOffset)
        return;
    m_scrollOffset = clamped;
    emit scrollOffsetChanged();
    scheduleRefresh();
}

void TerminalQuickItem::scheduleRefresh()
{
    polish();
    update();
}

void TerminalQuickItem::configBlinkTimer()
{
    const bool shouldRun = m_cursor.visible && m_cursor.blink && hasActiveFocus()
                           && m_allowBlinking;
    if (shouldRun != m_blinkTimer.isActive()) {
        if (shouldRun) {
            m_cursorBlinkState = true;
            m_blinkTimer.start();
        } else {
            m_blinkTimer.stop();
            m_cursorBlinkState = true;
        }
    }
}

QColor TerminalQuickItem::toQColor(const std::variant<int, QColor> &color) const
{
    if (std::holds_alternative<int>(color)) {
        const int idx = std::get<int>(color);
        if (idx >= 0 && idx < 18)
            return m_palette[idx];
        return m_palette[ColorIndex::Background];
    }
    return std::get<QColor>(color);
}

qreal TerminalQuickItem::topMargin() const
{
    return height() - m_surface->liveSize().height() * m_cellSize.height();
}

QPointF TerminalQuickItem::viewportToGlobal(QPointF p) const
{
    return {p.x(), p.y() - topMargin() + m_scrollOffset * m_cellSize.height()};
}

QPoint TerminalQuickItem::globalToGrid(QPointF p) const
{
    return QPoint(int(p.x() / m_cellSize.width()), int(p.y() / m_cellSize.height()));
}

QPoint TerminalQuickItem::toGridPos(QMouseEvent *event) const
{
    return globalToGrid(event->position() + QPointF(0, -topMargin() + 0.5));
}

void TerminalQuickItem::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() != oldGeometry.size()) {
        applySizeChange();
        setSelection(std::nullopt);
    }
}

void TerminalQuickItem::applySizeChange()
{
    QSize newLiveSize = {qFloor(width() / m_cellSize.width()),
                         qFloor(height() / m_cellSize.height())};
    if (newLiveSize.height() <= 0)
        return;
    newLiveSize.setWidth(qMax(1, newLiveSize.width()));

    if (m_surface->liveSize() == newLiveSize)
        return;

    if (m_resizePty)
        m_resizePty(newLiveSize);
    m_surface->resize(newLiveSize);
    m_surface->flush();
    scheduleRefresh();
}

bool TerminalQuickItem::setSelection(const std::optional<Selection> &selection, bool scroll)
{
    if (selection == m_selection)
        return false;

    m_selection = selection;
    selectionChanged(m_selection);

    if (m_selection && m_selection->final && scroll) {
        const int startRow = m_surface->posToGrid(m_selection->start).y();
        if (startRow < m_scrollOffset
            || startRow >= m_scrollOffset + m_surface->liveSize().height()) {
            setScrollOffset(startRow);
        }
    }

    scheduleRefresh();
    return true;
}

QString TerminalQuickItem::textFromSelection() const
{
    if (!m_selection)
        return {};

    if (m_selection->start == m_selection->end)
        return {};

    CellIterator it = m_surface->iteratorAt(m_selection->start);
    CellIterator end = m_surface->iteratorAt(m_selection->end);

    if (it.position() > end.position())
        return {};

    std::u32string s;
    bool previousWasZero = false;
    for (; it != end; ++it) {
        if (it.gridPos().x() == 0 && !s.empty() && previousWasZero)
            s += U'\n';

        if (*it != 0) {
            previousWasZero = false;
            s += *it;
        } else {
            previousWasZero = true;
        }
    }

    return QString::fromUcs4(s.data(), static_cast<int>(s.size()));
}

void TerminalQuickItem::clearSelection()
{
    setSelection(std::nullopt);
}

void TerminalQuickItem::selectAll()
{
    setSelection(
        Selection{0, m_surface->fullSize().width() * m_surface->fullSize().height()});
}

void TerminalQuickItem::copyToClipboard()
{
    if (!m_selection)
        return;

    setClipboard(textFromSelection());
    clearSelection();
}

void TerminalQuickItem::pasteFromClipboard()
{
    const QString clipboardText = QGuiApplication::clipboard()->text(QClipboard::Clipboard);
    if (clipboardText.isEmpty())
        return;

    m_surface->pasteFromClipboard(clipboardText);
}

void TerminalQuickItem::paste(const QString &text)
{
    if (text.isEmpty())
        return;

    m_surface->pasteFromClipboard(text);
}

void TerminalQuickItem::enableMouseTracking(bool enable)
{
    m_allowMouseTracking = enable;
}

// The widget binds these through the ActionManager; the item handles them
// directly with the same per-OS sequences.
static bool matchesShortcut(const QKeyEvent *event, Qt::Key key)
{
    if (event->key() != key)
        return false;
    const Qt::KeyboardModifiers mods = event->modifiers() & ~Qt::KeypadModifier;
#if defined(Q_OS_MACOS)
    return mods == Qt::ControlModifier; // Cmd, as Qt maps it
#elif defined(Q_OS_WIN)
    return mods == Qt::ControlModifier || mods == (Qt::ControlModifier | Qt::ShiftModifier);
#else
    return mods == (Qt::ControlModifier | Qt::ShiftModifier);
#endif
}

bool TerminalQuickItem::handleCopyPasteShortcut(QKeyEvent *event)
{
    // Copy is only enabled with a selection; without one the key reaches the shell.
    if (matchesShortcut(event, Qt::Key_C) && m_selection.has_value()) {
        copyToClipboard();
        return true;
    }
    if (matchesShortcut(event, Qt::Key_V)) {
        pasteFromClipboard();
        return true;
    }
    return false;
}

void TerminalQuickItem::keyPressEvent(QKeyEvent *event)
{
    if (m_blinkTimer.isActive()) { // don't blink during typing
        m_blinkTimer.start();
        m_cursorBlinkState = true;
    }

    event->accept();

    if (handleCopyPasteShortcut(event))
        return;

    if (m_surface->isInAltScreen()) {
        m_surface->sendKey(event);
        return;
    }

    switch (event->key()) {
    case Qt::Key_PageDown:
        setScrollOffset(m_scrollOffset + m_surface->liveSize().height());
        break;
    case Qt::Key_PageUp:
        setScrollOffset(m_scrollOffset - m_surface->liveSize().height());
        break;
    default:
        if (event->key() < Qt::Key_Shift || event->key() > Qt::Key_ScrollLock)
            setScrollOffset(maxScrollOffset());
        m_surface->sendKey(event);
        break;
    }
}

void TerminalQuickItem::wheelEvent(QWheelEvent *event)
{
    qreal rows = 0;
    if (!event->pixelDelta().isNull())
        rows = -event->pixelDelta().y() / m_cellSize.height();
    else
        rows = -event->angleDelta().y() / 120.0 * 3.0;

    m_wheelAccum += rows;
    const int whole = int(m_wheelAccum);
    if (whole != 0) {
        m_wheelAccum -= whole;
        setScrollOffset(m_scrollOffset + whole);
    }

    if (m_allowMouseTracking) {
        if (event->angleDelta().ry() > 0)
            m_surface->mouseButton(Qt::ExtraButton1, true, event->modifiers());
        else if (event->angleDelta().ry() < 0)
            m_surface->mouseButton(Qt::ExtraButton2, true, event->modifiers());
    }

    event->accept();
}

void TerminalQuickItem::mousePressEvent(QMouseEvent *event)
{
    forceActiveFocus();

    if (m_allowMouseTracking) {
        m_surface->mouseMove(toGridPos(event), event->modifiers());
        m_surface->mouseButton(event->button(), true, event->modifiers());
    }

    m_scrollDirection = 0;
    m_activeMouseSelectStart = viewportToGlobal(event->position());

    if (event->button() == Qt::LeftButton && event->modifiers() & Qt::ControlModifier) {
        // The widget activates links here; links are not ported yet.
        return;
    }

    if (event->button() == Qt::LeftButton) {
        if (m_selection && system_clock::now() - m_lastDoubleClick < 500ms) {
            m_selectLineMode = true;
            const Selection newSelection{
                m_surface->gridToPos({0, m_surface->posToGrid(m_selection->start).y()}),
                m_surface->gridToPos({m_surface->liveSize().width(),
                                      m_surface->posToGrid(m_selection->end).y()}),
                false};
            setSelection(newSelection);
        } else {
            m_selectLineMode = false;
            const int pos = m_surface->gridToPos(
                globalToGrid(viewportToGlobal(event->position())));
            setSelection(Selection{pos, pos, false});
        }
        event->accept();
    } else if (event->button() == Qt::RightButton) {
        if (event->modifiers() & Qt::ShiftModifier) {
            contextMenuRequested(event->position().toPoint());
        } else if (m_selection) {
            copyToClipboard();
            setSelection(std::nullopt);
        } else {
            pasteFromClipboard();
        }
    } else if (event->button() == Qt::MiddleButton) {
        QClipboard *clipboard = QGuiApplication::clipboard();
        if (clipboard->supportsSelection()) {
            const QString selectionText = clipboard->text(QClipboard::Selection);
            if (!selectionText.isEmpty())
                m_surface->pasteFromClipboard(selectionText);
        } else {
            m_surface->pasteFromClipboard(textFromSelection());
        }
    }
}

void TerminalQuickItem::mouseMoveEvent(QMouseEvent *event)
{
    if (m_allowMouseTracking)
        m_surface->mouseMove(toGridPos(event), event->modifiers());

    if (!m_selection || !(event->buttons() & Qt::LeftButton))
        return;

    Selection newSelection = *m_selection;

    int scrollVelocity = 0;
    if (event->position().y() < 0)
        scrollVelocity = int(event->position().y());
    else if (event->position().y() > height())
        scrollVelocity = int(event->position().y() - height());

    if ((scrollVelocity != 0) != m_scrollTimer.isActive()) {
        if (scrollVelocity != 0)
            m_scrollTimer.start();
        else
            m_scrollTimer.stop();
    }

    m_scrollDirection = scrollVelocity;

    if (m_scrollTimer.isActive() && scrollVelocity != 0) {
        const milliseconds scrollInterval = 1000ms / qAbs(scrollVelocity);
        if (m_scrollTimer.intervalAsDuration() != scrollInterval)
            m_scrollTimer.setInterval(scrollInterval);
    }

    QPointF posBounded = event->position();
    posBounded.setX(qBound<qreal>(0, posBounded.x(), width()));

    int start = m_surface->gridToPos(globalToGrid(m_activeMouseSelectStart));
    int newEnd = m_surface->gridToPos(globalToGrid(viewportToGlobal(posBounded)));

    if (start > newEnd)
        std::swap(start, newEnd);
    if (start < 0)
        start = 0;

    if (m_selectLineMode) {
        newSelection.start = m_surface->gridToPos({0, m_surface->posToGrid(start).y()});
        newSelection.end = m_surface->gridToPos(
            {m_surface->liveSize().width(), m_surface->posToGrid(newEnd).y()});
    } else {
        newSelection.start = start;
        newSelection.end = newEnd;
    }

    setSelection(newSelection);
}

void TerminalQuickItem::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_allowMouseTracking) {
        m_surface->mouseMove(toGridPos(event), event->modifiers());
        m_surface->mouseButton(event->button(), false, event->modifiers());
    }

    m_scrollTimer.stop();

    if (m_selection && event->button() == Qt::LeftButton) {
        if (m_selection->end - m_selection->start == 0)
            setSelection(std::nullopt);
        else
            setSelection(Selection{m_selection->start, m_selection->end, true});
    }
}

void TerminalQuickItem::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton)
        return;

    if (m_allowMouseTracking) {
        m_surface->mouseMove(toGridPos(event), event->modifiers());
        m_surface->mouseButton(event->button(), true, event->modifiers());
        m_surface->mouseButton(event->button(), false, event->modifiers());
    }

    const TextAndOffsets hit = textAt(event->position());
    setSelection(Selection{hit.start, hit.end, true});

    m_lastDoubleClick = system_clock::now();

    event->accept();
}

TerminalQuickItem::TextAndOffsets TerminalQuickItem::textAt(const QPointF &pos) const
{
    auto it = m_surface->iteratorAt(globalToGrid(viewportToGlobal(pos)));
    auto itRev = m_surface->rIteratorAt(globalToGrid(viewportToGlobal(pos)));

    const std::u32string whiteSpaces = U" \t\x00a0";

    const bool inverted = whiteSpaces.find(*it) != std::u32string::npos || *it == 0;

    auto predicate = [inverted, whiteSpaces](const std::u32string::value_type &ch) {
        if (inverted)
            return ch != 0 && whiteSpaces.find(ch) == std::u32string::npos;
        return ch == 0 || whiteSpaces.find(ch) != std::u32string::npos;
    };

    auto itRight = std::find_if(it, m_surface->end(), predicate);
    auto itLeft = std::find_if(itRev, m_surface->rend(), predicate);

    std::u32string text;
    std::copy(itLeft.base(), it, std::back_inserter(text));
    std::copy(it, itRight, std::back_inserter(text));
    std::transform(text.begin(), text.end(), text.begin(), [](const char32_t &ch) {
        return ch == 0 ? U' ' : ch;
    });

    return {(itLeft.base()).position(), itRight.position(), text};
}

void TerminalQuickItem::focusInEvent(QFocusEvent *event)
{
    QQuickItem::focusInEvent(event);
    m_surface->sendFocus(true);
    configBlinkTimer();
    scheduleRefresh();
}

void TerminalQuickItem::focusOutEvent(QFocusEvent *event)
{
    QQuickItem::focusOutEvent(event);
    m_surface->sendFocus(false);
    configBlinkTimer();
    scheduleRefresh();
}

void TerminalQuickItem::beginStats()
{
    std::lock_guard lock(m_statsMutex);
    m_stats = {};
    m_statsActive = true;
}

TerminalQuickItem::Stats TerminalQuickItem::takeStats()
{
    m_statsActive = false;
    std::lock_guard lock(m_statsMutex);
    return std::move(m_stats);
}

QList<TerminalQuickItem::VisibleRun> TerminalQuickItem::visibleRuns(int documentRow) const
{
    QList<VisibleRun> result;
    for (const RowSnapshot &row : m_rows) {
        if (row.documentRow != documentRow)
            continue;
        for (const RunSnapshot &run : row.runs)
            result.append({run.x, run.layout->text(), run.layout->formats()});
    }
    return result;
}

// A cell can join a batched layout only if its glyph advance is exactly one
// cell; everything else (wide chars, fallback fonts, clusters) gets its own
// layout pinned to its grid column.
bool TerminalQuickItem::gridTrueChar(QChar c)
{
    const char16_t u = c.unicode();
    if (size_t(u) >= m_charAdvance.size())
        return false;
    qreal &advance = m_charAdvance[u];
    if (advance < 0)
        advance = QFontMetricsF(m_font).horizontalAdvance(c);
    return std::abs(advance - m_cellSize.width()) < 0.01;
}

// All layout happens here, on the GUI thread. updatePaintNode() below only
// consumes the snapshots while the GUI thread is blocked in sync.
void TerminalQuickItem::updatePolish()
{
    QElapsedTimer timer;
    timer.start();

    m_rows.clear();
    m_cursorSnapshot = {};

    if (width() <= 0 || height() <= 0)
        return;

    const QSize live = m_surface->liveSize();
    const int fullHeight = m_surface->fullSize().height();
    const int maxScroll = qMax(0, fullHeight - live.height());
    if (m_followOutput)
        m_scrollOffset = maxScroll;
    m_scrollOffset = qBound(0, m_scrollOffset, maxScroll);

    const qreal cellW = m_cellSize.width();
    const qreal cellH = m_cellSize.height();
    const qreal marginTop = height() - live.height() * cellH;

    const int startRow = m_scrollOffset;
    const int endRow = qMin(fullHeight, startRow + live.height());

    double maxDeviation = 0;
    static const QString oneSpace = QStringLiteral(" ");

    const QList<SearchHit> &hits = searchHits();
    QList<SearchHit>::const_iterator hitIt
        = std::lower_bound(hits.constBegin(), hits.constEnd(), startRow,
                           [this](const SearchHit &hit, int row) {
                               return m_surface->posToGrid(hit.start).y() < row;
                           });

    const auto makeLayout = [this](const QString &t, const QList<QTextLayout::FormatRange> &f) {
        auto layout = std::make_unique<QTextLayout>();
        layout->setFont(m_font);
        layout->setCacheEnabled(true);
        QTextOption option;
        option.setWrapMode(QTextOption::NoWrap);
        option.setTextDirection(Qt::LeftToRight);
        layout->setTextOption(option);
        layout->setText(t);
        layout->setFormats(f);
        layout->beginLayout();
        QTextLine line = layout->createLine();
        line.setLineWidth(1e6);
        line.setPosition(QPointF(0, 0));
        layout->endLayout();
        return layout;
    };

    for (int row = startRow; row < endRow; ++row) {
        RowSnapshot rowSnap;
        rowSnap.documentRow = row;
        rowSnap.y = marginTop + (row - startRow) * cellH;

        QString text;
        QList<QTextLayout::FormatRange> formats;
        QTextCharFormat runFormat;
        bool haveRun = false;
        int runStart = 0;
        int layoutStartCol = 0;
        int interestingLen = 0;  // string length up to the last non-default cell
        int interestingCols = 0; // grid column just past that cell

        const auto closeRun = [&](int endPos) {
            if (haveRun && endPos > runStart)
                formats.append({runStart, endPos - runStart, runFormat});
        };

        // Emit the pending batched layout at its exact grid x, trimming the
        // tail of default-colored blanks; the background node covers those.
        const auto flushLayout = [&] {
            closeRun(text.size());
            haveRun = false;
            if (interestingLen > 0) {
                QString t = text;
                QList<QTextLayout::FormatRange> f = formats;
                if (interestingLen < t.size()) {
                    t.truncate(interestingLen);
                    while (!f.isEmpty() && f.last().start >= interestingLen)
                        f.removeLast();
                    if (!f.isEmpty()) {
                        QTextLayout::FormatRange &last = f.last();
                        last.length = qMin(last.length, interestingLen - last.start);
                    }
                }
                auto layout = makeLayout(t, f);
                if (m_statsActive) {
                    const double dev = std::abs(layout->lineAt(0).naturalTextWidth()
                                                - (interestingCols - layoutStartCol) * cellW);
                    maxDeviation = qMax(maxDeviation, dev);
                }
                rowSnap.runs.push_back({layoutStartCol * cellW, std::move(layout)});
            }
            text.clear();
            formats.clear();
            interestingLen = 0;
        };

        const int rowPos = m_surface->gridToPos({0, row});

        for (int x = 0; x < live.width();) {
            const TerminalCell cell = m_surface->fetchCell(x, row);
            const int pos = rowPos + x;

            bool findHit = false;
            while (hitIt != hits.constEnd()) {
                if (pos < hitIt->start)
                    break;
                if (pos >= hitIt->end) {
                    ++hitIt;
                    continue;
                }
                findHit = true;
                break;
            }
            const bool selected = m_selection && pos >= m_selection->start
                                  && pos < m_selection->end;

            QTextCharFormat fmt;
            fmt.setForeground(toQColor(cell.foregroundColor));
            const bool defaultBg = std::holds_alternative<int>(cell.backgroundColor)
                                   && std::get<int>(cell.backgroundColor)
                                          == ColorIndex::Background;
            bool hasBg = false;
            if (selected) { // the current find hit is the selection, so it wins
                fmt.setBackground(m_palette[size_t(ItemColorIdx::Selection)]);
                hasBg = true;
            } else if (findHit) {
                fmt.setBackground(m_palette[size_t(ItemColorIdx::FindMatch)]);
                hasBg = true;
            } else if (!defaultBg) {
                fmt.setBackground(toQColor(cell.backgroundColor));
                hasBg = true;
            }
            if (cell.bold)
                fmt.setFontWeight(QFont::Bold);
            if (cell.italic)
                fmt.setFontItalic(true);
            if (cell.underlineStyle != QTextCharFormat::NoUnderline)
                fmt.setUnderlineStyle(cell.underlineStyle);
            if (cell.strikeOut)
                fmt.setFontStrikeOut(true);

            const int cellCols = qMax(1, int(cell.width));
            const QString &cellText = cell.text.isEmpty() ? oneSpace : cell.text;
            const bool interesting = !cell.text.isEmpty() || hasBg
                                     || cell.underlineStyle != QTextCharFormat::NoUnderline
                                     || cell.strikeOut;
            const bool gridTrue = cellCols == 1 && cellText.size() == 1
                                  && gridTrueChar(cellText.at(0));

            if (!gridTrue) {
                flushLayout();
                QTextLayout::FormatRange range;
                range.start = 0;
                range.length = cellText.size();
                range.format = fmt;
                auto layout = makeLayout(cellText, {range});
                qreal xPos = x * cellW;
                const qreal span = cellCols * cellW;
                const qreal natural = layout->lineAt(0).naturalTextWidth();
                if (natural > span)
                    xPos += (span - natural) / 2; // center the overhang, like the widget
                rowSnap.runs.push_back({xPos, std::move(layout)});
            } else {
                if (text.isEmpty())
                    layoutStartCol = x;
                if (!haveRun || fmt != runFormat) {
                    closeRun(text.size());
                    runFormat = fmt;
                    runStart = text.size();
                    haveRun = true;
                }
                text += cellText;
                if (interesting) {
                    interestingLen = text.size();
                    interestingCols = x + cellCols;
                }
            }
            x += cellCols;
        }
        flushLayout();

        if (!rowSnap.runs.empty())
            m_rows.push_back(std::move(rowSnap));
    }

    // Cursor snapshot; position is in full (scrollback included) coordinates.
    const Cursor cursor = m_surface->cursor();
    const bool rowVisible = cursor.position.y() >= startRow && cursor.position.y() < endRow;
    const bool blinkVisible = !cursor.blink || m_cursorBlinkState || !m_allowBlinking
                              || !m_blinkTimer.isActive();
    if (cursor.visible && rowVisible && blinkVisible) {
        const int cw = qMax(1, m_surface->cellWidthAt(cursor.position.x(), cursor.position.y()));
        m_cursorSnapshot.visible = true;
        m_cursorSnapshot.focused = hasActiveFocus();
        m_cursorSnapshot.shape = cursor.shape;
        m_cursorSnapshot.rect = QRectF(cursor.position.x() * cellW,
                                       marginTop + (cursor.position.y() - startRow) * cellH,
                                       cw * cellW,
                                       cellH);
        if (cursor.shape == Cursor::Shape::Block && m_cursorSnapshot.focused) {
            const TerminalCell cell = m_surface->fetchCell(cursor.position.x(),
                                                           cursor.position.y());
            if (!cell.text.isEmpty()) {
                QTextLayout::FormatRange range;
                range.start = 0;
                range.length = cell.text.size();
                range.format.setForeground(toQColor(cell.backgroundColor));
                m_cursorSnapshot.cellLayout = makeLayout(cell.text, {range});
            }
        }
    }

    if (m_statsActive) {
        std::lock_guard lock(m_statsMutex);
        m_stats.polishNs.push_back(timer.nsecsElapsed());
        m_stats.maxGridDeviationPx = qMax(m_stats.maxGridDeviationPx, maxDeviation);
    }
}

QSGNode *TerminalQuickItem::updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *)
{
    QElapsedTimer timer;
    timer.start();

    QSGNode *root = oldNode ? oldNode : new QSGNode;
    while (QSGNode *child = root->firstChild())
        delete child;

    QQuickWindow *win = window();
    if (!win)
        return root;

    QSGRectangleNode *background = win->createRectangleNode();
    background->setRect(boundingRect());
    background->setColor(m_palette[ColorIndex::Background]);
    root->appendChildNode(background);

    if (!m_rows.empty()) {
        QSGTextNode *textNode = win->createTextNode();
        textNode->setRenderType(QSGTextNode::NativeRendering);
        for (const RowSnapshot &row : m_rows) {
            for (const RunSnapshot &run : row.runs)
                textNode->addTextLayout(QPointF(run.x, row.y), run.layout.get());
        }
        root->appendChildNode(textNode);
    }

    if (m_cursorSnapshot.visible) {
        const QRectF r = m_cursorSnapshot.rect.adjusted(0.5, 0.5, -0.5, -0.5);
        const QColor color = m_palette[ColorIndex::Foreground];
        const auto addRect = [&](const QRectF &rect) {
            QSGRectangleNode *node = win->createRectangleNode();
            node->setRect(rect);
            node->setColor(color);
            root->appendChildNode(node);
        };
        if (!m_cursorSnapshot.focused) { // hollow outline
            addRect({r.topLeft(), QSizeF(r.width(), 1)});
            addRect({r.bottomLeft() - QPointF(0, 1), QSizeF(r.width(), 1)});
            addRect({r.topLeft(), QSizeF(1, r.height())});
            addRect({r.topRight() - QPointF(1, 0), QSizeF(1, r.height())});
        } else {
            switch (m_cursorSnapshot.shape) {
            case TerminalSolution::Cursor::Shape::Block:
                addRect(r);
                if (m_cursorSnapshot.cellLayout) {
                    QSGTextNode *cellNode = win->createTextNode();
                    cellNode->setRenderType(QSGTextNode::NativeRendering);
                    cellNode->addTextLayout(m_cursorSnapshot.rect.topLeft(),
                                            m_cursorSnapshot.cellLayout.get());
                    root->appendChildNode(cellNode);
                }
                break;
            case TerminalSolution::Cursor::Shape::Underline:
                addRect({r.bottomLeft() - QPointF(0, 2), QSizeF(r.width(), 2)});
                break;
            case TerminalSolution::Cursor::Shape::LeftBar:
                addRect({r.topLeft(), QSizeF(2, r.height())});
                break;
            }
        }
    }

    if (m_statsActive) {
        std::lock_guard lock(m_statsMutex);
        m_stats.paintNodeNs.push_back(timer.nsecsElapsed());
    }

    return root;
}

} // namespace TerminalSolution
