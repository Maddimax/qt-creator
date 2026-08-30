// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "outputview.h"

#include <utils/qtcassert.h>
#include <utils/stylehelper.h>

#include <QQuickItem>
#include <QQuickTextDocument>
#include <QQuickWidget>
#include <QTextCursor>
#include <QTextDocument>

namespace QtcQuick {

OutputView::OutputView(QWidget *parent)
    : QuickWidget(parent)
{
    setSource(QUrl("qrc:/qt/qml/QtCreator/Ui/OutputView.qml"));

    QObject * const root = rootObject();
    QTC_ASSERT(root, return);
    m_baseFont = root->property("baseFont").value<QFont>();
    connect(root, SIGNAL(linkActivated(QString)), this, SIGNAL(linkActivated(QString)));
    connect(root, SIGNAL(saveContentsRequested()), this, SIGNAL(saveContentsRequested()));
    connect(root, SIGNAL(copyContentsToScratchBufferRequested()),
            this, SIGNAL(copyContentsToScratchBufferRequested()));
    connect(root, SIGNAL(clearRequested()), this, SIGNAL(clearRequested()));
    connect(root, SIGNAL(zoomRequested(double)), this, SLOT(zoomBy(double)));
}

QQuickItem *OutputView::textArea() const
{
    auto * const root = qobject_cast<QQuickItem *>(rootObject());
    QTC_ASSERT(root, return nullptr);
    // The item that draws the text, by name: the root arranges the view, and
    // what a pane hands over belongs to the text area inside it.
    return root->findChild<QQuickItem *>("outputText");
}

void OutputView::setDocument(QTextDocument *document)
{
    if (m_document == document)
        return;
    m_document = document;

    QQuickItem * const area = textArea();
    QTC_ASSERT(area, return);
    auto * const quickDocument = area->property("textDocument").value<QQuickTextDocument *>();
    QTC_ASSERT(quickDocument, return);
    quickDocument->setTextDocument(document);
}

QTextDocument *OutputView::document() const
{
    return m_document;
}

void OutputView::setBaseFont(const QFont &font)
{
    m_baseFont = font;
    applyFont();
}

QFont OutputView::baseFont() const
{
    return m_baseFont;
}

void OutputView::setFontZoom(float zoom)
{
    if (m_zoom == zoom)
        return;
    m_zoom = zoom;
    applyFont();
}

float OutputView::fontZoom() const
{
    return m_zoom;
}

void OutputView::zoomBy(double delta)
{
    const float zoomed = float(m_baseFont.pointSizeF()) + m_zoom + float(delta);
    if (delta < 0 && zoomed < Utils::StyleHelper::minimumZoomedFontSize)
        return;
    setFontZoom(m_zoom + float(delta));
    emit wheelZoom();
}

void OutputView::applyFont()
{
    if (QObject * const root = rootObject())
        root->setProperty("effectiveFont", Utils::StyleHelper::zoomedFont(m_baseFont, m_zoom));
}

void OutputView::setWheelZoomEnabled(bool enabled)
{
    if (QObject * const root = rootObject())
        root->setProperty("wheelZoomEnabled", enabled);
}

QTextCursor OutputView::textCursor() const
{
    QQuickItem * const area = textArea();
    QTC_ASSERT(area && m_document, return {});

    const int position = area->property("cursorPosition").toInt();
    const int selectionStart = area->property("selectionStart").toInt();
    const int selectionEnd = area->property("selectionEnd").toInt();
    // The item reports the selection in document order and says only where the
    // cursor is; the other end is the anchor. Without this a backwards
    // selection comes back reversed, and a find that wrapped would resume from
    // the wrong end of its own match.
    const int anchor = position == selectionStart ? selectionEnd : selectionStart;

    QTextCursor cursor(m_document);
    cursor.setPosition(anchor);
    cursor.setPosition(position, QTextCursor::KeepAnchor);
    return cursor;
}

void OutputView::setTextCursor(const QTextCursor &cursor)
{
    QQuickItem * const area = textArea();
    QTC_ASSERT(area, return);

    if (cursor.hasSelection()) {
        QMetaObject::invokeMethod(area, "select", Q_ARG(int, cursor.anchor()),
                                  Q_ARG(int, cursor.position()));
    } else {
        area->setProperty("cursorPosition", cursor.position());
    }
}

void OutputView::setWordWrapEnabled(bool enabled)
{
    if (QObject * const root = rootObject())
        root->setProperty("wordWrapEnabled", enabled);
}

void OutputView::setBackgroundColor(const QColor &color)
{
    // Only the item: it fills the view, so nothing of what is behind it shows
    // and setting the widget's clear colour too changes nothing anyone sees.
    if (QObject * const root = rootObject())
        root->setProperty("backgroundColor", color);
}

void OutputView::copy()
{
    if (QQuickItem * const area = textArea())
        QMetaObject::invokeMethod(area, "copy");
}

void OutputView::selectAll()
{
    if (QQuickItem * const area = textArea())
        QMetaObject::invokeMethod(area, "selectAll");
}

void OutputView::scrollToBottom()
{
    if (QObject * const root = rootObject())
        QMetaObject::invokeMethod(root, "scrollToBottom");
}

QObject *OutputView::contextMenu() const
{
    QObject * const root = rootObject();
    QTC_ASSERT(root, return nullptr);
    return root->findChild<QObject *>("outputContextMenu");
}

QString OutputView::linkAt(qreal x, qreal y) const
{
    QQuickItem * const area = textArea();
    QTC_ASSERT(area, return {});
    QString href;
    QMetaObject::invokeMethod(area, "linkAt", Q_RETURN_ARG(QString, href),
                              Q_ARG(qreal, x), Q_ARG(qreal, y));
    return href;
}

QString OutputView::hoveredLink() const
{
    QQuickItem * const area = textArea();
    QTC_ASSERT(area, return {});
    return area->property("hoveredLink").toString();
}

} // namespace QtcQuick
