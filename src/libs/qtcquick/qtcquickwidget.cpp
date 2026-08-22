// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "qtcquickwidget.h"

#include "qtcquickengine.h"

#include <utils/guiutils.h>
#include <utils/qtcassert.h>
#include <utils/theme/theme.h>

#include <QQuickItem>
#include <QQuickWidget>
#include <QVBoxLayout>
#include <QWindow>

namespace QtcQuick {

QuickWidget::QuickWidget(QWidget *parent)
    : QWidget(parent)
    , m_quickWidget(new QQuickWidget(engine(), this))
{
    m_quickWidget->setResizeMode(QQuickWidget::SizeRootObjectToView);
    m_quickWidget->setClearColor(Utils::creatorColor(Utils::Theme::Token_Background_Default));

    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_quickWidget);
}

void QuickWidget::setSource(const QUrl &url)
{
    m_quickWidget->setSource(url);
    QTC_CHECK(m_quickWidget->errors().isEmpty());

    // QML popups and windows have no widget parent to inherit from.
    if (QQuickItem *root = m_quickWidget->rootObject()) {
        const QList<QWindow *> windows = root->findChildren<QWindow *>();
        for (QWindow *window : windows) {
            if (!window->transientParent())
                if (QWidget *parent = Utils::dialogParent())
                    window->setTransientParent(parent->windowHandle());
        }
    }
}

QQuickWidget *QuickWidget::quickWidget() const
{
    return m_quickWidget;
}

QObject *QuickWidget::rootObject() const
{
    return m_quickWidget->rootObject();
}

} // namespace QtcQuick
