// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "qtciconprovider.h"

#include <utils/icon.h>
#include <utils/theme/theme.h>

#include <QMetaEnum>
#include <QUrlQuery>

namespace QtcQuick {

IconProvider::IconProvider()
    : QQuickImageProvider(QQuickImageProvider::Pixmap)
{}

const char *IconProvider::name()
{
    return "qtcreator";
}

QPixmap IconProvider::requestPixmap(const QString &id, QSize *size, const QSize &requestedSize)
{
    const int queryStart = id.indexOf('?');
    const QString mask = queryStart < 0 ? id : id.left(queryStart);
    const QString query = queryStart < 0 ? QString() : id.mid(queryStart + 1);

    QPixmap pixmap;
    const QString color = QUrlQuery(query).queryItemValue("color");
    if (color.isEmpty()) {
        pixmap = Utils::Icon::fromTheme(mask).pixmap(requestedSize.isValid() ? requestedSize
                                                                            : QSize());
    } else {
        bool known = false;
        const int role = QMetaEnum::fromType<Utils::Theme::Color>()
                             .keyToValue(color.toUtf8().constData(), &known);
        if (known) {
            const Utils::Icon icon({{Utils::FilePath::fromString(':' + mask),
                                     Utils::Theme::Color(role)}}, Utils::Icon::Tint);
            pixmap = icon.pixmap();
        }
    }

    if (size)
        *size = pixmap.size();
    return pixmap;
}

} // namespace QtcQuick
