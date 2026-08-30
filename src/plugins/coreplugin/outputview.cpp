// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "outputview.h"

namespace Core {

OutputView::OutputView(QWidget *parent)
    : QWidget(parent)
{}

OutputView::~OutputView() = default;

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
