// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <QtGlobal>

#include <memory>

QT_BEGIN_NAMESPACE
class QObject;
class QPalette;
class QPoint;
class QPointF;
class QString;
class QTextCharFormat;
class QWidget;
QT_END_NAMESPACE

namespace Utils {

class WidgetTextControl;

// Non-QObject mixin: everything WidgetTextControl asks of the surface embedding
// it, so the same control can serve a QWidget host and later a QQuickItem host.
class TextEditingHost
{
public:
    virtual ~TextEditingHost();

    virtual QPalette palette() const = 0;
    virtual int cursorWidthHint() const = 0;
    virtual bool blinkCursorWhenTextSelected() const = 0;
    virtual int startDragDistance() const = 0;
    virtual int doubleClickInterval() const = 0;

    // Drag-and-drop identity of the surface: the QDrag source, also compared
    // against a move-drop's source to decide whether the dragged selection is
    // removed. May be null.
    virtual QObject *dragSource() const = 0;

    virtual void updateInputMethod() = 0;
    virtual void showToolTip(const QPoint &globalPos, const QString &text) = 0;
    virtual void showContextMenu(const QPoint &screenPos, const QPointF &docPos) = 0;

    virtual QTextCharFormat focusIndicatorFormat(const QPalette &palette) const = 0;
    virtual bool fullWidthSelection() const = 0;
};

// The QWidget host, implemented in widgettextcontrolwidgethost.cpp. widget
// parents popups and answers style queries; eventWidget (the context widget
// after the graphics-scene fallback) is the drag identity and gates input
// method updates.
std::unique_ptr<TextEditingHost> createWidgetTextEditingHost(WidgetTextControl *control,
                                                             QWidget *widget,
                                                             QWidget *eventWidget);

} // namespace Utils
