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
#include <QPointer>
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
    TabAwareQuickWidget(QQmlEngine *engine, QWidget *parent)
        : QQuickWidget(engine, parent)
    {
        // The scene handles Tab itself, so the key never reaches
        // focusNextPrevChild() once focus is inside it. Watching the window is
        // the only place the walk can be seen before the scene acts on it.
        quickWindow()->installEventFilter(this);
    }

protected:
    // Where tabbing in put the caret: a scene's focus chain is a ring, so
    // arriving back here is what "the scene has run out" looks like.
    void focusInEvent(QFocusEvent *event) override
    {
        QQuickWidget::focusInEvent(event);
        m_entryItem = quickWindow() ? quickWindow()->activeFocusItem() : nullptr;
    }

    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (watched == quickWindow() && event->type() == QEvent::KeyPress) {
            auto * const key = static_cast<QKeyEvent *>(event);
            const bool forward = key->key() == Qt::Key_Tab;
            const bool backward = key->key() == Qt::Key_Backtab;
            if ((forward || backward) && !(key->modifiers() & ~Qt::ShiftModifier)) {
                QQuickItem * const from = quickWindow()->activeFocusItem();
                // Asked, not done: nextItemInFocusChain() says where the ring
                // would go without going there, so the walk can be stopped
                // before the scene takes the key for itself.
                QQuickItem * const to = from ? from->nextItemInFocusChain(forward) : nullptr;
                // Back where the walk began. For a scene with one item that
                // is also "did not move", and the two are told apart by
                // whether the item takes part in tab navigation at all: a
                // field does and has nothing more to offer, an editor does
                // not and is typing an indent with this.
                if (to && to == m_entryItem
                    && (to != from || from->activeFocusOnTab())) {
                    key->accept();
                    QQuickWidget::focusNextPrevChild(forward);
                    return true;
                }
            }
        }
        return QQuickWidget::eventFilter(watched, event);
    }

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

private:
    QPointer<QQuickItem> m_entryItem;
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
    // And it has to be a tab stop to be walked into: a plain QWidget is
    // NoFocus, which tab navigation skips however focusable the scene is.
    setFocusPolicy(Qt::StrongFocus);
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
