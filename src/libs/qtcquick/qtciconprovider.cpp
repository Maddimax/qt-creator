// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "qtciconprovider.h"

#include <utils/icon.h>
#include <utils/theme/theme.h>

#include <QHash>
#include <QIcon>
#include <QMetaEnum>
#include <QUrlQuery>

namespace QtcQuick {

// What iconUrl() handed out, by QIcon::cacheKey(), which is the same for every
// copy of one icon.
static QHash<qint64, QIcon> &registeredIcons()
{
    static QHash<qint64, QIcon> icons;
    return icons;
}

static const QLatin1String iconPrefix{"@icon/"};

QString iconUrl(const QIcon &icon)
{
    if (icon.isNull())
        return {};
    const qint64 key = icon.cacheKey();
    registeredIcons().insert(key, icon);
    return QString("image://%1/%2%3").arg(QLatin1String(IconProvider::name()), iconPrefix)
           .arg(key);
}

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
    if (mask.startsWith(iconPrefix)) {
        const QIcon icon = registeredIcons().value(mask.mid(iconPrefix.size()).toLongLong());
        // A QIcon has no size of its own, so one has to be asked for. What the
        // Image wants where it says, otherwise the largest the icon actually
        // has - scaling one up is what makes a 16-pixel icon look smeared.
        const QSize wanted = requestedSize.isValid() ? requestedSize
                                                     : icon.availableSizes().value(0);
        pixmap = icon.pixmap(wanted.isEmpty() ? QSize(16, 16) : wanted);
        if (size)
            *size = pixmap.size();
        return pixmap;
    }

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
