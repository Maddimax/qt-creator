// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "minimapview.h"

#include "codesource.h"
#include "displaysettings.h"
#include "fontsettings.h"
#include "textdocument.h"
#include "textviewport.h"

#include <coreplugin/find/minimapimage.h>

#include <utils/theme/theme.h>

#include <QPainter>
#include <QTextDocument>

namespace TextEditor {

// What the widget editor draws one line of the document as.
static Core::MinimapMetrics metricsFor(qreal width)
{
    Core::MinimapMetrics metrics;
    metrics.width = qMax(0, int(width));
    return metrics;
}

MinimapView::MinimapView(QQuickItem *parent)
    : QQuickPaintedItem(parent)
{
    setAcceptedMouseButtons(Qt::LeftButton);
    // Turning it on or off is a settings change like any other, and so is the
    // theme the picture is drawn in.
    connect(&displaySettings(), &Utils::AspectContainer::changed, this, [this] {
        emit wantedChanged();
        m_stale = true;
        update();
    });
}

TextViewport *MinimapView::viewport() const
{
    return m_viewport;
}

void MinimapView::setViewport(TextViewport *viewport)
{
    if (m_viewport == viewport)
        return;

    for (const QMetaObject::Connection &connection : std::as_const(m_connections))
        disconnect(connection);
    m_connections.clear();

    m_viewport = viewport;
    if (m_viewport) {
        // The picture is of the document, so it is stale when the document is;
        // where the marked part sits changes far more often and only needs a
        // repaint.
        m_connections << connect(m_viewport, &TextViewport::documentChanged, this, [this] {
            hookDocument();
            m_stale = true;
            update();
        });
        // Where the view has scrolled to moves the marked part, which is a
        // repaint. Nothing about the picture has changed.
        m_connections << connect(m_viewport, &TextViewport::scrollYChanged, this, [this] {
            update();
        });
    }
    hookDocument();
    m_stale = true;
    update();
    emit viewportChanged();
}

void MinimapView::hookDocument()
{
    for (const QMetaObject::Connection &connection : std::as_const(m_documentConnections))
        disconnect(connection);
    m_documentConnections.clear();

    TextDocument * const doc = m_viewport && m_viewport->document()
                                   ? m_viewport->document()->textDocument()
                                   : nullptr;
    if (!doc)
        return;

    // What the picture is of, and what it is drawn with. Not the view's
    // metrics: those are worked out again on every layout, and redrawing a
    // whole document because the caret moved is not a repaint anybody asked
    // for.
    m_documentConnections << connect(doc->document(), &QTextDocument::contentsChanged, this, [this] {
        m_stale = true;
        update();
    });
    m_documentConnections << connect(doc, &TextDocument::fontSettingsChanged, this, [this] {
        m_stale = true;
        update();
    });
}

int MinimapView::pictureHeight() const
{
    return m_picture.height();
}

bool MinimapView::wanted() const
{
    return displaySettings().displayMinimap();
}

void MinimapView::ensurePicture()
{
    if (m_stale)
        rebuild();
}

void MinimapView::rebuild()
{
    m_stale = false;
    m_picture = QImage();

    TextDocument * const doc = m_viewport && m_viewport->document()
                                   ? m_viewport->document()->textDocument()
                                   : nullptr;
    if (!doc || !wanted() || width() <= 0) {
        emit pictureChanged();
        return;
    }

    m_picture = Core::renderMinimap(doc->document(),
                                    metricsFor(width()),
                                    m_viewport->font(),
                                    doc->fontSettings().toTextCharFormat(C_TEXT).foreground().color());
    emit pictureChanged();
}

qreal MinimapView::scrolledFraction() const
{
    if (!m_viewport)
        return 0;
    const qreal most = m_viewport->contentHeight() - m_viewport->height();
    return most > 0 ? qBound(qreal(0), m_viewport->scrollY() / most, qreal(1)) : 0;
}

QRectF MinimapView::thumb()
{
    ensurePicture();
    if (!m_viewport || m_viewport->lineHeight() <= 0)
        return {};

    // One line of the picture against one line of the text: that ratio is what
    // turns a screenful into a height on the bar.
    const qreal scale = metricsFor(width()).lineHeight() / m_viewport->lineHeight();
    const qreal tall = m_viewport->height() * scale;
    const qreal shown = qMin<qreal>(m_picture.height(), height());
    const qreal top = scrolledFraction() * qMax(qreal(0), shown - tall);
    return QRectF(0, top, width(), tall);
}

void MinimapView::paint(QPainter *painter)
{
    ensurePicture();
    if (m_picture.isNull() || !m_viewport)
        return;

    painter->fillRect(QRectF(0, 0, width(), height()), m_viewport->backgroundColor());
    painter->fillRect(thumb(), Utils::creatorColor(Utils::Theme::Token_Foreground_Muted));

    // A picture taller than the room for it scrolls with the text, so the part
    // being read stays in view.
    if (m_picture.height() > height()) {
        const int from = qRound(scrolledFraction() * (m_picture.height() - height()));
        painter->drawImage(QPointF(0, 0),
                           m_picture.copy(0, from, m_picture.width(), qRound(height())));
    } else {
        painter->drawImage(QPointF(0, 0), m_picture);
    }
}

void MinimapView::scrollToThumbTop(qreal top)
{
    if (!m_viewport)
        return;
    const qreal most = m_viewport->contentHeight() - m_viewport->height();
    if (most <= 0)
        return;

    // How far the marked part can travel, which is not the height of the item
    // once the picture is shorter than it.
    ensurePicture();
    const qreal shown = qMin<qreal>(m_picture.height(), height());
    const qreal span = shown - thumb().height();
    if (span <= 0)
        return;
    m_viewport->setScrollY(qBound(qreal(0), top / span, qreal(1)) * most);
}

void MinimapView::mousePressEvent(QMouseEvent *event)
{
    // Only the marked part is a handle. The widget editor ignores a press
    // anywhere else, rather than jumping to it.
    const QRectF marked = thumb();
    if (marked.contains(event->position()))
        m_dragOffset = event->position().y() - marked.top();
    event->accept();
}

void MinimapView::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_dragOffset)
        return;
    scrollToThumbTop(event->position().y() - *m_dragOffset);
    event->accept();
}

void MinimapView::mouseReleaseEvent(QMouseEvent *event)
{
    m_dragOffset.reset();
    event->accept();
}

void MinimapView::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickPaintedItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.width() != oldGeometry.width())
        m_stale = true;
}

} // namespace TextEditor
