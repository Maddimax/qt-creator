// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <coreplugin/outputview.h>

#include <qtcquick/outputview.h>

#include <QTextCursor>
#include <QVBoxLayout>

namespace QuickUi::Internal {

// What lets Core hand out a Qt Quick output view without linking Qt Quick.
// Core declares the interface, QtcQuick draws, and this plugin - the one place
// allowed to know both - joins them.
class QuickOutputView final : public Core::OutputView
{
public:
    explicit QuickOutputView(QWidget *parent = nullptr)
        : Core::OutputView(parent)
    {
        auto * const layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->addWidget(m_view);

        connect(m_view, &QtcQuick::OutputView::linkActivated,
                this, &Core::OutputView::linkActivated);
        connect(m_view, &QtcQuick::OutputView::wheelZoom, this, &Core::OutputView::wheelZoom);
        connect(m_view, &QtcQuick::OutputView::saveContentsRequested,
                this, &Core::OutputView::saveContentsRequested);
        connect(m_view, &QtcQuick::OutputView::copyContentsToScratchBufferRequested,
                this, &Core::OutputView::copyContentsToScratchBufferRequested);
        connect(m_view, &QtcQuick::OutputView::clearRequested,
                this, &Core::OutputView::clearRequested);
        connect(m_view, &QtcQuick::OutputView::contextMenuAboutToShow,
                this, &Core::OutputView::contextMenuAboutToShow);
    }

    void showDocument(QTextDocument *document) override { m_view->setDocument(document); }
    void setBaseFont(const QFont &font) override { m_view->setBaseFont(font); }
    void setFontZoom(float zoom) override { m_view->setFontZoom(zoom); }
    float fontZoom() const override { return m_view->fontZoom(); }
    void setWheelZoomEnabled(bool enabled) override { m_view->setWheelZoomEnabled(enabled); }
    void setWordWrapEnabled(bool enabled) override { m_view->setWordWrapEnabled(enabled); }
    void setBackgroundColor(const QColor &color) override { m_view->setBackgroundColor(color); }
    void setContextMenuActions(const QList<QAction *> &actions, bool replaceStandard) override
    { m_view->setContextMenuActions(actions, replaceStandard); }
    int documentPositionAt(qreal x, qreal y) const override
    { return m_view->documentPositionAt(x, y); }
    void copy() override { m_view->copy(); }
    void selectAll() override { m_view->selectAll(); }
    void scrollToBottom() override { m_view->scrollToBottom(); }
    QTextCursor textCursor() const override { return m_view->textCursor(); }
    void setTextCursor(const QTextCursor &cursor) override { m_view->setTextCursor(cursor); }

private:
    QtcQuick::OutputView * const m_view = new QtcQuick::OutputView;
};

// Installed on Core so that a pane, which can only see Core, gets one of
// these when it asks Core::createOutputView().
inline void installOutputViewFactory()
{
    Core::setOutputViewFactory([](QWidget *parent) -> Core::OutputView * {
        return new QuickOutputView(parent);
    });
}

} // namespace QuickUi::Internal
