// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "textviewport.h"

#include <QImage>
#include <QPointer>
#include <QQmlEngine>
#include <QQuickPaintedItem>

#include <optional>

namespace TextEditor {

// The document drawn small beside the text it belongs to: one pixel per
// character, coloured by the highlighter, with the part on screen marked. The
// picture itself is Core's - what is here is where it goes, what of it is
// showing, and dragging the marked part to scroll.
class TEXTEDITOR_EXPORT MinimapView : public QQuickPaintedItem
{
    Q_OBJECT
    QML_ELEMENT

    // The view this is a picture of. Everything else - the document, the
    // font, the colours, where it has scrolled to - is read from it, so there
    // is one answer to each rather than two that can disagree.
    Q_PROPERTY(TextEditor::TextViewport *viewport READ viewport WRITE setViewport
                   NOTIFY viewportChanged)
    // Whether the display settings ask for one at all. Off by default.
    Q_PROPERTY(bool wanted READ wanted NOTIFY wantedChanged)

public:
    explicit MinimapView(QQuickItem *parent = nullptr);

    TextViewport *viewport() const;
    void setViewport(TextViewport *viewport);

    bool wanted() const;

    void paint(QPainter *painter) override;

signals:
    void viewportChanged();
    void wantedChanged();

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;

private:
    // The picture is what says where the marked part is, so it is needed to
    // answer a click as well as to draw - building it only when painting
    // would make dragging depend on having been drawn first.
    void ensurePicture();
    void rebuild();
    // Where the marked part sits and how tall it is, in this item.
    QRectF thumb();
    qreal scrolledFraction() const;
    void scrollToThumbTop(qreal top);

    QPointer<TextViewport> m_viewport;
    QImage m_picture;
    bool m_stale = true;
    // How far down the marked part the drag started, so that it does not jump
    // to the pointer when the drag begins.
    std::optional<qreal> m_dragOffset;
    QList<QMetaObject::Connection> m_connections;
};

} // namespace TextEditor
