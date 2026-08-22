// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "qtcquick_global.h"

#include <QWidget>

QT_BEGIN_NAMESPACE
class QQuickWidget;
class QUrl;
QT_END_NAMESPACE

namespace QtcQuick {

// Hosts a Qt Quick scene inside Qt Creator's widget shell, on the shared
// engine. The QQuickWidget is a child rather than this class' base so that it is
// never a layout's or dock's direct child.
class QTCQUICK_EXPORT QuickWidget : public QWidget
{
    Q_OBJECT

public:
    explicit QuickWidget(QWidget *parent = nullptr);

    void setSource(const QUrl &url);
    QQuickWidget *quickWidget() const;
    QObject *rootObject() const;

private:
    QQuickWidget *m_quickWidget = nullptr;
};

} // namespace QtcQuick
