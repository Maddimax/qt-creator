// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "qtcquick_global.h"

#include <QQuickImageProvider>

namespace QtcQuick {

// Serves Qt Creator's icons to QML as
//     image://qtcreator/<mask path>?color=<Theme::Color key>
// The mask path is a resource path without the leading colon, so
//     image://qtcreator/utils/images/filtericon.png?color=Token_Text_Muted
// is the toolbar filter icon tinted with a theme colour. Without a colour the
// icon is looked up with Utils::Icon::fromTheme().
class QTCQUICK_EXPORT IconProvider : public QQuickImageProvider
{
public:
    IconProvider();

    QPixmap requestPixmap(const QString &id, QSize *size, const QSize &requestedSize) override;

    static const char *name();
};

} // namespace QtcQuick
