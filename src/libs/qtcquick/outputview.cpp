// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "outputview.h"

#include <utils/qtcassert.h>

#include <QQuickItem>
#include <QQuickTextDocument>
#include <QQuickWidget>
#include <QTextDocument>

namespace QtcQuick {

OutputView::OutputView(QWidget *parent)
    : QuickWidget(parent)
{
    setSource(QUrl("qrc:/qt/qml/QtCreator/Ui/OutputView.qml"));
}

void OutputView::setDocument(QTextDocument *document)
{
    if (m_document == document)
        return;
    m_document = document;

    auto * const root = qobject_cast<QQuickItem *>(rootObject());
    QTC_ASSERT(root, return);
    // The item that draws it, by name: the root is what arranges the view, and
    // what a pane hands over belongs to the text area inside it.
    QQuickItem * const area = root->findChild<QQuickItem *>("outputText");
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
    if (QObject * const root = rootObject())
        root->setProperty("baseFont", font);
}

} // namespace QtcQuick
