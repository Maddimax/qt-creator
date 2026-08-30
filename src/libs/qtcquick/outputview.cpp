// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "outputview.h"

#include <utils/qtcassert.h>
#include <utils/stylehelper.h>

#include <QQuickItem>
#include <QQuickTextDocument>
#include <QQuickWidget>
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
