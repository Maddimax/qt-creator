// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "qtcquickwidget.h"

#include "qtcquickengine.h"

#include <utils/guiutils.h>
#include <utils/qtcassert.h>
#include <utils/theme/theme.h>

#include <QKeyEvent>
#include <QQuickItem>
#include <QQuickWidget>
#include <QVBoxLayout>
#include <QWindow>

namespace QtcQuick {

namespace {

// A QQuickWidget hands Tab to the scene only when the scene has another item
// to move the focus to. Where it has not - an editor that is the last
// focusable thing on the page - the key goes to the widget focus chain
// instead, and an item that meant to type with it never sees it. So the scene
// is asked first, always, and the widget chain only gets what the scene
// declined.
class TabAwareQuickWidget : public QQuickWidget
{
public:
    using QQuickWidget::QQuickWidget;

protected:
    bool focusNextPrevChild(bool next) override
    {
        const Qt::Key key = next ? Qt::Key_Tab : Qt::Key_Backtab;
        QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier);
        QCoreApplication::sendEvent(quickWindow(), &press);
        if (press.isAccepted()) {
            QKeyEvent release(QEvent::KeyRelease, key, Qt::NoModifier);
            QCoreApplication::sendEvent(quickWindow(), &release);
            return true;
        }
        return QQuickWidget::focusNextPrevChild(next);
    }
};

} // namespace

QuickWidget::QuickWidget(QWidget *parent)
    : QWidget(parent)
    , m_quickWidget(new TabAwareQuickWidget(engine(), this))
{
    m_quickWidget->setResizeMode(QQuickWidget::SizeRootObjectToView);
    m_quickWidget->setClearColor(Utils::creatorColor(Utils::Theme::Token_Background_Default));

    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_quickWidget);

    // Focus given to this wrapper has to end up in the scene: without a proxy
    // it stops on a plain QWidget, so a view that was asked for focus has it
    // and still receives no keys.
    setFocusProxy(m_quickWidget);
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
