// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "terminalquick_global.h"
#include "terminalsurface.h"

#include <QElapsedTimer>
#include <QFont>
#include <QImage>
#include <QQuickItem>
#include <QTextLayout>
#include <QTimer>

#include <array>
#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

namespace TerminalSolution {

class SurfaceIntegration;

class TERMINAL_QUICK_EXPORT TerminalQuickItem : public QQuickItem
{
    Q_OBJECT
    Q_PROPERTY(int scrollOffset READ scrollOffset WRITE setScrollOffset NOTIFY scrollOffsetChanged)

public:
    enum class ItemColorIdx {
        Foreground = ColorIndex::Foreground,
        Background = ColorIndex::Background,
        Selection,
        FindMatch,
    };

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

    struct Selection
    {
        int start;
        int end;
        bool final{false};

        bool operator!=(const Selection &other) const
        {
            return start != other.start || end != other.end || final != other.final;
        }

        bool operator==(const Selection &other) const { return !operator!=(other); }
    };

    std::optional<Selection> selection() const { return m_selection; }
    void clearSelection();
    void selectAll();

    void copyToClipboard();
    void pasteFromClipboard();
    void paste(const QString &text);

    void enableMouseTracking(bool enable);

    void setFont(const QFont &font);
    QFont font() const { return m_font; }
    void setColors(const std::array<QColor, 20> &colors);

    void zoomIn();
    void zoomOut();

    void setPasswordMode(bool passwordMode);

    void setAllowBlinkingCursor(bool allow);
    bool allowBlinkingCursor() const { return m_allowBlinking; }

    struct Link
    {
        QString text;
        int targetLine = 0;
        int targetColumn = 0;
    };

    struct LinkSelection : public Selection
    {
        Link link;

        bool operator!=(const LinkSelection &other) const
        {
            return link.text != other.link.text || link.targetLine != other.link.targetLine
                   || link.targetColumn != other.link.targetColumn || Selection::operator!=(other);
        }
    };

    void copyLinkToClipboard();

    virtual std::optional<Link> toLink(const QString &text)
    {
        Q_UNUSED(text)
        return std::nullopt;
    }
    virtual void linkActivated(const Link &link) { Q_UNUSED(link) }

    virtual const QList<SearchHit> &searchHits() const
    {
        static QList<SearchHit> noHits;
        return noHits;
    }

    virtual void setClipboard(const QString &text) { Q_UNUSED(text) }
    virtual void selectionChanged(const std::optional<Selection> &newSelection)
    {
        Q_UNUSED(newSelection)
    }
    virtual void contextMenuRequested(const QPoint &pos) { Q_UNUSED(pos) }

    // Selftest observability
    bool cursorBlinkTimerActive() const { return m_blinkTimer.isActive(); }
    int blinkToggleCount() const { return m_blinkToggles; }
    QColor paletteColor(int index) const { return m_palette[size_t(index) % m_palette.size()]; }
    QString preeditString() const { return m_preEditString; }
    std::optional<LinkSelection> linkSelection() const { return m_linkSelection; }
    bool passwordLockVisible() const
    {
        return m_cursorSnapshot.passwordLock && !m_cursorSnapshot.lockImage.isNull();
    }
    bool cursorRectVisible() const
    {
        return m_cursorSnapshot.visible && !m_cursorSnapshot.passwordLock;
    }

    struct VisibleRun
    {
        qreal x = 0;
        QString text;
        QList<QTextLayout::FormatRange> formats;
    };
    QList<VisibleRun> visibleRuns(int documentRow) const;
    QList<VisibleRun> preeditRuns() const;

    // Perf instrumentation
    struct Stats
    {
        std::vector<qint64> polishNs;
        std::vector<qint64> paintNodeNs;
        double maxGridDeviationPx = 0; // |layout advance - grid span| within multi-cell layouts
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
    void keyReleaseEvent(QKeyEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void focusInEvent(QFocusEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void hoverMoveEvent(QHoverEvent *event) override;
    void inputMethodEvent(QInputMethodEvent *event) override;
    QVariant inputMethodQuery(Qt::InputMethodQuery query) const override;

    bool setSelection(const std::optional<Selection> &selection, bool scroll = true);
    QString textFromSelection() const;

    void scheduleRefresh();

private:
    void applySizeChange();
    void configBlinkTimer();
    QColor toQColor(const std::variant<int, QColor> &color) const;

    qreal topMargin() const;
    QPointF viewportToGlobal(QPointF p) const;
    QPoint globalToGrid(QPointF p) const;
    QPoint toGridPos(QMouseEvent *event) const;

    struct TextAndOffsets
    {
        int start;
        int end;
        std::u32string text;
    };
    TextAndOffsets textAt(const QPointF &pos) const;

    bool checkLinkAt(const QPointF &pos);
    void updateLinkHover(const QPointF &pos, Qt::KeyboardModifiers modifiers);

    bool handleCopyPasteShortcut(QKeyEvent *event);
    bool gridTrueChar(QChar c);

    struct RunSnapshot
    {
        qreal x = 0;
        std::unique_ptr<QTextLayout> layout;
    };

    struct RowSnapshot
    {
        int documentRow = -1;
        qreal y = 0;
        std::vector<RunSnapshot> runs;
    };

    struct CursorSnapshot
    {
        bool visible = false;
        bool focused = false;
        QRectF rect;
        Cursor::Shape shape = Cursor::Shape::Block;
        std::unique_ptr<QTextLayout> cellLayout; // inverted glyph under a block cursor
        bool passwordLock = false;
        QImage lockImage;
    };

    std::unique_ptr<TerminalSurface> m_surface;
    WriteToPty m_writeToPty;
    ResizePty m_resizePty;

    QFont m_font;
    QSizeF m_cellSize;
    std::array<QColor, 20> m_palette;
    std::array<qreal, 0x300> m_charAdvance; // lazily measured, -1 = unknown

    int m_scrollOffset = 0;
    bool m_followOutput = true;
    qreal m_wheelAccum = 0;

    std::optional<Selection> m_selection;
    std::optional<LinkSelection> m_linkSelection;
    QString m_preEditString;
    bool m_passwordMode = false;
    QImage m_lockImage; // scaled passwordlock.png, cached per cell height
    QPointF m_activeMouseSelectStart; // full-buffer pixel coordinates
    bool m_selectLineMode = false;
    bool m_allowMouseTracking = true;
    std::chrono::system_clock::time_point m_lastDoubleClick{std::chrono::system_clock::now()};
    QTimer m_scrollTimer;
    int m_scrollDirection = 0;

    Cursor m_cursor;
    QTimer m_blinkTimer;
    bool m_cursorBlinkState = true;
    bool m_allowBlinking = true;
    int m_blinkToggles = 0;

    // Snapshots produced in updatePolish() (GUI thread), consumed in
    // updatePaintNode() (render thread, GUI blocked).
    std::vector<RowSnapshot> m_rows;
    RowSnapshot m_preeditRow;
    CursorSnapshot m_cursorSnapshot;

    std::mutex m_statsMutex;
    Stats m_stats;
    std::atomic<bool> m_statsActive{false};
};

} // namespace TerminalSolution
