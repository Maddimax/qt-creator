// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <coreplugin/outputview.h>

#include <qtcquick/outputview.h>

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
    }

    void setDocument(QTextDocument *document) override { m_view->setDocument(document); }
    QTextDocument *document() const override { return m_view->document(); }
    void setBaseFont(const QFont &font) override { m_view->setBaseFont(font); }
    void setFontZoom(float zoom) override { m_view->setFontZoom(zoom); }
    float fontZoom() const override { return m_view->fontZoom(); }
    void setWheelZoomEnabled(bool enabled) override { m_view->setWheelZoomEnabled(enabled); }

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
