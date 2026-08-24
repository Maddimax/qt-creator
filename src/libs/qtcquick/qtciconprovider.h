// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "qtcquick_global.h"

#include <QQuickImageProvider>

QT_BEGIN_NAMESPACE
class QIcon;
QT_END_NAMESPACE

namespace QtcQuick {

// The URL an Image can load a ready-made QIcon from. QML has no QIcon, and a
// model row that shows one - a kit, a device, a To-Do keyword - has an icon
// rather than the mask and colour it was built from. The icon is kept for as
// long as the application runs: the callers are rows showing icons that are
// themselves static, so what accumulates is one entry per distinct icon.
QTCQUICK_EXPORT QString iconUrl(const QIcon &icon);

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
