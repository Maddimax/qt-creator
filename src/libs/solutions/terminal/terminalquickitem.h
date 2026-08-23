// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "terminalquick_global.h"
#include "terminalsurface.h"

#include <QElapsedTimer>
#include <QFont>
#include <QQuickItem>
#include <QTextLayout>
#include <QTimer>

#include <array>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

namespace TerminalSolution {

class SurfaceIntegration;

class TERMINAL_QUICK_EXPORT TerminalQuickItem : public QQuickItem
{
    Q_OBJECT
    Q_PROPERTY(int scrollOffset READ scrollOffset WRITE setScrollOffset NOTIFY scrollOffsetChanged)

public:
    explicit TerminalQuickItem(QQuickItem *parent = nullptr);
    ~TerminalQuickItem() override;

    TerminalSurface *surface() const { return m_surface.get(); }

    int scrollOffset() const { return m_scrollOffset; }
    void setScrollOffset(int offset);
    int maxScrollOffset() const;

    QSizeF cellSize() const { return m_cellSize; }
    QSize gridSize() const { return m_surface->liveSize(); }

    using WriteToPty = std::function<qint64(const QByteArray &)>;
    void setWriteToPty(WriteToPty writeToPty);
    using ResizePty = std::function<void(const QSize &)>;
    void setResizePty(ResizePty resizePty);

    void setSurfaceIntegration(SurfaceIntegration *integration);

    // Selftest observability
    bool cursorBlinkTimerActive() const { return m_blinkTimer.isActive(); }
    int blinkToggleCount() const { return m_blinkToggles; }

    // Perf instrumentation
    struct Stats
    {
        std::vector<qint64> polishNs;
        std::vector<qint64> paintNodeNs;
        double maxGridDeviationPx = 0; // |layout x advance - grid column x|, worst visible cell
    };
    void beginStats();
    Stats takeStats();

signals:
    void scrollOffsetChanged();

protected:
    QSGNode *updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *) override;
    void updatePolish() override;
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;
    void keyPressEvent(QKeyEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void focusInEvent(QFocusEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;

private:
    void scheduleRefresh();
    void applySizeChange();
    void configBlinkTimer();
    QColor toQColor(const std::variant<int, QColor> &color) const;

    struct RowSnapshot
    {
        qreal y = 0;
        std::unique_ptr<QTextLayout> layout;
    };

    struct CursorSnapshot
    {
        bool visible = false;
        bool focused = false;
        QRectF rect;
        Cursor::Shape shape = Cursor::Shape::Block;
        std::unique_ptr<QTextLayout> cellLayout; // inverted glyph under a block cursor
    };

    std::unique_ptr<TerminalSurface> m_surface;
    WriteToPty m_writeToPty;
    ResizePty m_resizePty;

    QFont m_font;
    QSizeF m_cellSize;
    std::array<QColor, 20> m_palette;

    int m_scrollOffset = 0;
    bool m_followOutput = true;
    qreal m_wheelAccum = 0;

    Cursor m_cursor;
    QTimer m_blinkTimer;
    bool m_cursorBlinkState = true;
    bool m_allowBlinking = true;
    int m_blinkToggles = 0;

    // Snapshots produced in updatePolish() (GUI thread), consumed in
    // updatePaintNode() (render thread, GUI blocked).
    std::vector<RowSnapshot> m_rows;
    CursorSnapshot m_cursorSnapshot;

    std::mutex m_statsMutex;
    Stats m_stats;
    std::atomic<bool> m_statsActive{false};
};

} // namespace TerminalSolution
