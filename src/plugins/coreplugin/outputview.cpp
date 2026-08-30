// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "outputview.h"

#include "find/basetextfind.h"

#include <utils/aggregate.h>

#include <QPointer>

namespace Core {
namespace {

// Ctrl+F over an output view. BaseTextFindBase asks for a cursor, a document,
// whether it is read-only and a widget to anchor the find bar to; an output
// view answers all four without being a text widget, so this is the whole of
// it. The same shape as TextEditor's QuickTextFind.
class OutputViewFind final : public BaseTextFindBase
{
public:
    explicit OutputViewFind(OutputView *view)
        : m_view(view)
    {}

private:
    QTextCursor textCursor() const final
    {
        return m_view ? m_view->textCursor() : QTextCursor();
    }

    void setTextCursor(const QTextCursor &cursor) final
    {
        if (m_view)
            m_view->setTextCursor(cursor);
    }

    QTextDocument *document() const final { return m_view ? m_view->document() : nullptr; }

    // Output is not edited, so Replace is offered on nothing.
    bool isReadOnly() const final { return true; }
    QWidget *widget() const final { return m_view; }

    const QPointer<OutputView> m_view;
};

} // namespace

OutputView::OutputView(QWidget *parent)
    : QWidget(parent)
    , m_findSupport(new OutputViewFind(this))
{
    // Aggregated rather than merely owned, so that a FindToolBarPlaceHolder
    // anchored to this view finds it by asking, as it does for OutputWindow.
    Utils::Aggregation::aggregate({this, m_findSupport});
}

OutputView::~OutputView() = default;

IFindSupport *OutputView::findSupport() const
{
    return m_findSupport;
}

static OutputViewFactory s_outputViewFactory;

void setOutputViewFactory(const OutputViewFactory &factory)
{
    s_outputViewFactory = factory;
}

OutputView *createOutputView(QWidget *parent)
{
    return s_outputViewFactory ? s_outputViewFactory(parent) : nullptr;
}

} // namespace Core
