// Copyright (C) 2023 Tasuku Suzuki <tasuku.suzuki@signal-slot.co.jp>
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "guiutils.h"

#include "aspects.h"
#include "hostosinfo.h"
#include "pathchooser.h"
#include "plaintextedit/plaintextedit.h"
#include "qtcassert.h"
#include "qtcolorbutton.h"
#include "shutdownguard.h"

#include <QAbstractButton>
#include <QCheckBox>
#include <QComboBox>
#include <QEvent>
#include <QGroupBox>
#include <QGuiApplication>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPlainTextEdit>
#include <QRadioButton>
#include <QScrollBar>
#include <QSpinBox>
#include <QThread>
#include <QWidget>

#include <atomic>

namespace Utils {

namespace Internal {

static bool isWheelModifier()
{
    return QGuiApplication::keyboardModifiers()
           == (HostOsInfo::isMacHost() ? Qt::MetaModifier : Qt::ControlModifier);
}

class WheelEventFilter : public QObject
{
public:
    bool eventFilter(QObject *watched, QEvent *event) override {
        if (event->type() == QEvent::Wheel && !isWheelModifier()) {
            QWidget *widget = qobject_cast<QWidget *>(watched);
            if (widget && widget->focusPolicy() != Qt::WheelFocus && !widget->hasFocus()) {
                QObject *parent = widget->parentWidget();
                if (parent)
                    return parent->event(event);
            }
        }
        return QObject::eventFilter(watched, event);
    }
};

class FirstShowFilter : public QObject
{
public:
    FirstShowFilter(QWidget *widget, const std::function<void()> &func)
        : QObject(widget)
        , m_func(func)
    {
        widget->installEventFilter(this);
    }

private:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type() == QEvent::Show) {
            watched->removeEventFilter(this);
            // func() may delete the widget, and with it this filter, so keep
            // neither m_func nor this alive across the call.
            const std::function<void()> func = std::move(m_func);
            deleteLater();
            func();
            return false;
        }
        return QObject::eventFilter(watched, event);
    }

    std::function<void()> m_func;
};

} // namespace Internal

void onFirstShow(QWidget *widget, const std::function<void()> &func)
{
    QTC_ASSERT(widget, return);
    QTC_ASSERT(func, return);
    if (widget->isVisible()) {
        func();
        return;
    }
    new Internal::FirstShowFilter(widget, func);
}

void QTCREATOR_UTILS_EXPORT setWheelScrollingWithoutFocusBlocked(QWidget *widget)
{
    static GuardedObject<Internal::WheelEventFilter> instance;
    // Installing duplicated event filter for the same objects just brings the event filter
    // to the front and is otherwise no-op (the second event filter isn't installed).
    widget->installEventFilter(instance.get());
    if (widget->focusPolicy() == Qt::WheelFocus)
        widget->setFocusPolicy(Qt::StrongFocus);
}

static QWidget *(*s_dialogParentGetter)() = nullptr;

void setDialogParentGetter(QWidget *(*getter)())
{
    s_dialogParentGetter = getter;
}

QWidget *dialogParent()
{
    return s_dialogParentGetter ? s_dialogParentGetter() : nullptr;
}

static void installDirtyTriggerHelper(QObject *object, bool check)
{
    QTC_ASSERT(object, return);

    const auto action = check ? checkSettingsDirty : markSettingsDirty;

    // Keep this before QAbstractButton
    if (auto ob = qobject_cast<QtColorButton *>(object)) {
        QObject::connect(ob, &QtColorButton::colorChanged, action);
        return;
    }
    if (auto ob = qobject_cast<QAbstractButton *>(object)) {
        QObject::connect(ob, &QAbstractButton::pressed, action);
        return;
    }
    if (auto ob = qobject_cast<QLineEdit *>(object)) {
        QObject::connect(ob, &QLineEdit::textChanged, action);
        return;
    }
    if (auto ob = qobject_cast<QComboBox *>(object)) {
        QObject::connect(ob, &QComboBox::currentIndexChanged, action);
        QObject::connect(ob, &QComboBox::currentTextChanged, action);
        return;
    }
    if (auto ob = qobject_cast<QSpinBox *>(object)) {
        QObject::connect(ob, &QSpinBox::valueChanged, action);
        return;
    }
    if (auto ob = qobject_cast<QPlainTextEdit *>(object)) {
        QObject::connect(ob, &QPlainTextEdit::textChanged, action);
        return;
    }
    if (auto ob = qobject_cast<PlainTextEdit *>(object)) {
        QObject::connect(ob, &PlainTextEdit::textChanged, action);
        return;
    }
    if (auto ob = qobject_cast<QAbstractItemModel *>(object)) {
        QObject::connect(ob, &QAbstractItemModel::rowsInserted, action);
        QObject::connect(ob, &QAbstractItemModel::rowsRemoved, action);
        return;
    }
    if (auto ob = qobject_cast<BaseAspect *>(object)) {
        QObject::connect(ob, &BaseAspect::volatileValueChanged, action);
        return;
    }

    QTC_CHECK(false);
}

void installMarkSettingsDirtyTrigger(QObject *object)
{
    installDirtyTriggerHelper(object, false);
}

void installCheckSettingsDirtyTrigger(QObject *object)
{
    installDirtyTriggerHelper(object, true);
}

void installMarkSettingsDirtyTriggerRecursively(QObject *object)
{
    QTC_ASSERT(object, return);

    if (isIgnoredForDirtyHook(object))
        return;

    QList<QObject *> children = {object};

    while (!children.isEmpty()) {
        QObject *child = children.takeLast();
        if (isIgnoredForDirtyHook(child))
            continue;

        children += child->findChildren<QObject *>(Qt::FindDirectChildrenOnly);

        if (child->metaObject() == &QWidget::staticMetaObject)
            continue;

        if (child->metaObject() == &QLabel::staticMetaObject)
            continue;

        if (child->metaObject() == &QScrollBar::staticMetaObject)
            continue;

        if (child->metaObject() == &QMenu::staticMetaObject)
            continue;

        auto markDirty = [child] {
            if (isIgnoredForDirtyHook(child))
                return;
            markSettingsDirty();
        };

        if (auto ob = qobject_cast<QLineEdit *>(child)) {
            QObject::connect(ob, &QLineEdit::textEdited, markDirty);
            continue;
        }
        if (auto ob = qobject_cast<QComboBox *>(child)) {
            QObject::connect(ob, &QComboBox::currentIndexChanged, markDirty);
            continue;
        }
        if (auto ob = qobject_cast<QSpinBox *>(child)) {
            QObject::connect(ob, &QSpinBox::valueChanged, markDirty);
            continue;
        }
        if (auto ob = qobject_cast<QGroupBox *>(child)) {
            QObject::connect(ob, &QGroupBox::toggled, markDirty);
            continue;
        }
        if (auto ob = qobject_cast<QCheckBox *>(child)) {
            QObject::connect(ob, &QCheckBox::toggled, markDirty);
            continue;
        }
        if (auto ob = qobject_cast<QRadioButton *>(child)) {
            QObject::connect(ob, &QRadioButton::toggled, markDirty);
            continue;
        }
        if (auto ob = qobject_cast<QListWidget *>(child)) {
            QObject::connect(ob, &QListWidget::itemChanged, markDirty);
            QObject::connect(ob->model(), &QAbstractItemModel::rowsInserted, markDirty);
            QObject::connect(ob->model(), &QAbstractItemModel::rowsRemoved, markDirty);
            continue;
        }
        if (auto ob = qobject_cast<QRadioButton *>(child)) {
            QObject::connect(ob, &QRadioButton::toggled, markDirty);
            continue;
        }
    }
}

} // namespace Utils
