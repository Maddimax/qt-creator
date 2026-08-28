// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "minimapoverlay.h"

#include "minimapimage.h"

#include <utils/plaintextedit/plaintextedit.h>
#include <utils/plaintextedit/texteditorlayout.h>
#include <utils/qtcassert.h>
#include <utils/theme/theme.h>

#include <QApplication>
#include <QMouseEvent>
#include <QPainter>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QStyle>
#include <QStyleOption>
#include <QTextBlock>
#include <QWheelEvent>

using namespace Core;
using namespace Utils;

MinimapOverlay::MinimapOverlay(PlainTextEdit *editor)
    : QWidget(editor)
    , m_editor(editor)
{
    QTC_ASSERT(editor, return);
    m_doc = editor->document();
    m_vScroll = editor->verticalScrollBar();

    m_scrollbarDefaultWidth = editor->style()->pixelMetric(QStyle::PM_ScrollBarExtent);

    editor->setEditorTextMargin("Core.MinimapWidth", Qt::RightEdge, m_minimapWidth);

    editor->installEventFilter(this);

    connect(m_doc, &QTextDocument::contentsChange, this, &MinimapOverlay::onDocumentChanged);
    connect(
        m_doc->documentLayout(),
        &QAbstractTextDocumentLayout::update,
        this,
        &MinimapOverlay::onDocumentChanged);

    // Minimap is outside the editor's viewport and needs a repaint when the editor is painted
    connect(editor, &PlainTextEdit::updateRequest, this, [this] { update(); }, Qt::QueuedConnection);

    m_updateTimer.setSingleShot(true);
    m_updateTimer.setInterval(30);
    connect(&m_updateTimer, &QTimer::timeout, this, &MinimapOverlay::updateImage);

    setAutoFillBackground(true);

    scheduleUpdate();
}

MinimapOverlay::~MinimapOverlay()
{
    if (m_editor)
        m_editor->setEditorTextMargin("Core.MinimapWidth", Qt::RightEdge, 0);
}

void MinimapOverlay::paintMinimap(QPainter *painter) const
{
    if (m_minimap.isNull() || !m_vScroll)
        return;

    QColor bg = m_editor->palette().brush(QPalette::Base).color();
    painter->fillRect(rect(), bg);

    const QRect geo = rect().adjusted(1, 0, 1, 0);

    ThumbGeometry tg = computeThumbGeometry();
    const qreal &scrollFraction = tg.scrollFraction;
    const QRect &thumbRect = tg.rect;

    const QColor thumbColor = creatorColor(Theme::Token_Foreground_Muted);
    painter->fillRect(thumbRect, thumbColor);

    if (m_minimap.height() > geo.height()) {
        int srcY = qRound(scrollFraction * (m_minimap.height() - geo.height()));
        const QRect srcRect(0, srcY, geo.width(), geo.height());

        painter->drawImage(geo.topLeft(), m_minimap.copy(srcRect));
    } else {
        painter->drawImage(geo.topLeft(), m_minimap);
    }

    QPen pen;
    pen.setWidthF(m_editor->devicePixelRatio());
    pen.setColor(creatorColor(Theme::SplitterColor));
    painter->setPen(pen);
    painter->drawLine(rect().topLeft(), rect().bottomLeft());
}

void MinimapOverlay::scheduleUpdate()
{
    if (!m_updateTimer.isActive())
        m_updateTimer.start();
}

void MinimapOverlay::setOverrideBlockColorFunction(const std::function<std::optional<QColor> (const QTextBlock &)> &func)
{
    m_overrideBlockColor = std::move(func);
    scheduleUpdate();
}

void MinimapOverlay::onDocumentChanged()
{
    scheduleUpdate();
}


void MinimapOverlay::updateImage()
{
    if (!m_editor || !m_editor->isVisible())
        return;

    // A view that can scroll past the end of the document needs the picture to
    // run that far too, or the thumb reaches the bottom before the text does.
    int extraLines = 0;
    if (m_editor->centerOnScroll()) {
        extraLines = m_editor->viewport()->height()
                     / m_editor->editorLayout()->lineSpacing();
    }

    m_minimap = renderMinimap(m_doc,
                              {m_minimapWidth, m_pixelsPerLine, m_lineGap},
                              m_editor->font(),
                              m_editor->palette().brush(QPalette::Text).color(),
                              extraLines,
                              m_overrideBlockColor);
    update();
}


void MinimapOverlay::doMove()
{
    QMetaObject::invokeMethod(
        this,
        [this] {
            const int x = m_editor->width() - m_scrollbarDefaultWidth - m_minimapWidth;
            move(x, 0);
        },
        Qt::QueuedConnection);
}

void MinimapOverlay::doResize()
{
    resize(m_minimapWidth, m_editor->height());
}

MinimapOverlay::ThumbGeometry MinimapOverlay::computeThumbGeometry() const
{
    const qreal scale = miniLineHeight() / qreal(m_vScroll->singleStep());
    const qreal scrollFraction = m_vScroll->maximum() > 0
                                     ? qreal(m_vScroll->value()) / m_vScroll->maximum()
                                     : qreal(m_vScroll->value()) / m_vScroll->pageStep();

    const QRect geo = rect().adjusted(1, 0, 1, 0);
    const int thumbHeight = qRound(m_vScroll->pageStep() * scale);
    const int thumbTop
        = geo.top()
          + qRound(scrollFraction * (qMin(m_minimap.height(), geo.height()) - thumbHeight));

    ThumbGeometry tg;
    tg.scrollFraction = scrollFraction;
    tg.rect = QRect(rect().left(), thumbTop, m_minimapWidth, thumbHeight);
    return tg;
}

void MinimapOverlay::mousePressEvent(QMouseEvent *event)
{
    if (!m_vScroll)
        return;

    const QPoint clickPos = event->pos();

    const ThumbGeometry tg = computeThumbGeometry();
    const QRect &thumb = tg.rect;

    if (thumb.contains(clickPos)) {
        m_dragOffset = clickPos.y() - thumb.top();
    }

    event->accept();
}

void MinimapOverlay::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_vScroll || !m_dragOffset)
        return;

    m_dragging = true;
    const int scrollPosition = minimapPixelPosToRangeValue(event->pos().y() - *m_dragOffset);
    m_vScroll->setValue(qBound(m_vScroll->minimum(), scrollPosition, m_vScroll->maximum()));

    event->accept();
}

void MinimapOverlay::mouseReleaseEvent(QMouseEvent *event)
{
    if (!m_vScroll)
        return;

    const bool wasDragging = m_dragging;
    m_dragOffset.reset();
    m_dragging = false;

    if (!wasDragging) {
        const ThumbGeometry tg = computeThumbGeometry();
        const QRect &thumb = tg.rect;
        if (event->modifiers() == Qt::AltModifier) {
            // paging
            const int delta = (event->pos().y() < thumb.center().y()) ? -m_vScroll->pageStep()
                                                                      : m_vScroll->pageStep();
            const int scrollPosition = m_vScroll->value() + delta;
            m_vScroll->setValue(qBound(m_vScroll->minimum(), scrollPosition, m_vScroll->maximum()));
        } else {
            // jumping
            const qreal scale = qreal(m_vScroll->singleStep()) / miniLineHeight();
            const int clickPosition = m_vScroll->value()
                                      + (event->pos().y() - thumb.center().y()) * scale;
            m_vScroll->setValue(qBound(m_vScroll->minimum(), clickPosition, m_vScroll->maximum()));
        }
    }

    event->accept();
}

int MinimapOverlay::minimapPixelPosToRangeValue(int pos) const
{
    const QRect geo = rect().adjusted(1, 0, 1, 0);
    const ThumbGeometry tg = computeThumbGeometry();
    const QRect &thumb = tg.rect;

    const int thumbMin = geo.top();
    const int thumbMax = geo.top() + qMin(m_minimap.height(), geo.height()) - thumb.height() + 1;

    return QStyle::sliderValueFromPosition(
        m_vScroll->minimum(), m_vScroll->maximum(), pos - thumbMin, thumbMax - thumbMin, false);
}

void MinimapOverlay::wheelEvent(QWheelEvent *event)
{
    if (!m_vScroll)
        return;

    QPointF mappedPos = m_vScroll->mapFromGlobal(event->globalPosition());
    mappedPos.setX(m_scrollbarDefaultWidth / 2);
    QWheelEvent forwarded(
        mappedPos,
        event->globalPosition(),
        event->pixelDelta(),
        event->angleDelta(),
        event->buttons(),
        event->modifiers(),
        event->phase(),
        event->inverted());

    QApplication::sendEvent(m_vScroll, &forwarded);
    event->accept();
}

void MinimapOverlay::paintEvent(QPaintEvent *paintEvent)
{
    QWidget::paintEvent(paintEvent);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, false);

    paintMinimap(&painter);
}

bool MinimapOverlay::eventFilter(QObject *object, QEvent *event)
{
    switch (event->type()) {
    case QEvent::Move:
        doMove();
        break;
    case QEvent::Resize:
        doResize();
        break;
    case QEvent::ZOrderChange:
        raise();
        break;
    case QEvent::Show:
        doResize();
        doMove();
        show();
        scheduleUpdate();
        break;
    case QEvent::Hide:
        hide();
        break;
    default:
        break;
    }

    return QWidget::eventFilter(object, event);
}
