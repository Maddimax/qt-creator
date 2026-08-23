// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "terminalquickitem.h"

#include <QFontMetricsF>
#include <QQuickWindow>
#include <QSGRectangleNode>
#include <QSGTextNode>
#include <QtMath>

namespace TerminalSolution {

TerminalQuickItem::TerminalQuickItem(QQuickItem *parent)
    : QQuickItem(parent)
{
    setFlag(ItemHasContents);
    setAcceptedMouseButtons(Qt::AllButtons);

    m_font = QFont(QStringLiteral("Menlo"), 12);
    m_font.setFixedPitch(true);
    const QFontMetricsF fm(m_font);
    m_cellSize = QSizeF(fm.averageCharWidth(), qCeil(fm.height()));

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

void TerminalQuickItem::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() != oldGeometry.size())
        applySizeChange();
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

void TerminalQuickItem::keyPressEvent(QKeyEvent *event)
{
    if (m_blinkTimer.isActive()) { // don't blink during typing
        m_blinkTimer.start();
        m_cursorBlinkState = true;
    }

    event->accept();

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
    event->accept();
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
    const qreal topMargin = height() - live.height() * cellH;

    const int startRow = m_scrollOffset;
    const int endRow = qMin(fullHeight, startRow + live.height());

    double maxDeviation = 0;

    for (int row = startRow; row < endRow; ++row) {
        QString text;
        QList<QTextLayout::FormatRange> formats;
        QTextCharFormat runFormat;
        bool haveRun = false;
        int runStart = 0;
        int interestingLen = 0; // string length up to the last non-default cell
        int interestingCols = 0;

        const auto flushRun = [&](int endPos) {
            if (haveRun && endPos > runStart)
                formats.append({runStart, endPos - runStart, runFormat});
        };

        for (int x = 0; x < live.width();) {
            const TerminalCell cell = m_surface->fetchCell(x, row);

            QTextCharFormat fmt;
            fmt.setForeground(toQColor(cell.foregroundColor));
            const bool defaultBg = std::holds_alternative<int>(cell.backgroundColor)
                                   && std::get<int>(cell.backgroundColor)
                                          == ColorIndex::Background;
            if (!defaultBg)
                fmt.setBackground(toQColor(cell.backgroundColor));
            if (cell.bold)
                fmt.setFontWeight(QFont::Bold);
            if (cell.italic)
                fmt.setFontItalic(true);
            if (cell.underlineStyle != QTextCharFormat::NoUnderline)
                fmt.setUnderlineStyle(cell.underlineStyle);
            if (cell.strikeOut)
                fmt.setFontStrikeOut(true);

            if (!haveRun || fmt != runFormat) {
                flushRun(text.size());
                runFormat = fmt;
                runStart = text.size();
                haveRun = true;
            }

            const int cellCols = qMax(1, int(cell.width));
            text += cell.text.isEmpty() ? QStringLiteral(" ") : cell.text;
            x += cellCols;

            const bool interesting = !cell.text.isEmpty() || !defaultBg
                                     || cell.underlineStyle != QTextCharFormat::NoUnderline
                                     || cell.strikeOut;
            if (interesting) {
                interestingLen = text.size();
                interestingCols = x;
            }
        }
        flushRun(text.size());

        // Trim the tail of default-colored blanks; the background node covers it.
        if (interestingLen < text.size()) {
            text.truncate(interestingLen);
            while (!formats.isEmpty() && formats.last().start >= interestingLen)
                formats.removeLast();
            if (!formats.isEmpty()) {
                QTextLayout::FormatRange &last = formats.last();
                last.length = qMin(last.length, interestingLen - last.start);
            }
        }

        if (text.isEmpty())
            continue;

        auto layout = std::make_unique<QTextLayout>();
        layout->setFont(m_font);
        layout->setCacheEnabled(true);
        QTextOption option;
        option.setWrapMode(QTextOption::NoWrap);
        option.setTextDirection(Qt::LeftToRight);
        layout->setTextOption(option);
        layout->setText(text);
        layout->setFormats(formats);
        layout->beginLayout();
        QTextLine line = layout->createLine();
        line.setLineWidth(1e6);
        line.setPosition(QPointF(0, 0));
        layout->endLayout();

        if (m_statsActive) {
            const double dev = std::abs(line.naturalTextWidth() - interestingCols * cellW);
            maxDeviation = qMax(maxDeviation, dev);
        }

        m_rows.push_back({topMargin + (row - startRow) * cellH, std::move(layout)});
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
                                       topMargin + (cursor.position.y() - startRow) * cellH,
                                       cw * cellW,
                                       cellH);
        if (cursor.shape == Cursor::Shape::Block && m_cursorSnapshot.focused) {
            const TerminalCell cell = m_surface->fetchCell(cursor.position.x(),
                                                           cursor.position.y());
            if (!cell.text.isEmpty()) {
                auto cellLayout = std::make_unique<QTextLayout>();
                cellLayout->setFont(m_font);
                cellLayout->setCacheEnabled(true);
                cellLayout->setText(cell.text);
                QTextLayout::FormatRange range;
                range.start = 0;
                range.length = cell.text.size();
                range.format.setForeground(toQColor(cell.backgroundColor));
                cellLayout->setFormats({range});
                cellLayout->beginLayout();
                QTextLine cellLine = cellLayout->createLine();
                cellLine.setLineWidth(1e6);
                cellLine.setPosition(QPointF(0, 0));
                cellLayout->endLayout();
                m_cursorSnapshot.cellLayout = std::move(cellLayout);
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
        for (const RowSnapshot &row : m_rows)
            textNode->addTextLayout(QPointF(0, row.y), row.layout.get());
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
