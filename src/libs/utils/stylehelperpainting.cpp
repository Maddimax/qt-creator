// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "stylehelperpainting.h"

#include "qtcassert.h"
#include "stylehelper.h"

#include <QApplication>
#include <QCommonStyle>
#include <QLabel>
#include <QPainter>
#include <QPixmapCache>
#include <QStyleOption>

#include <qmath.h>

QT_BEGIN_NAMESPACE
// Note, this is exported but in a private header as qtopengl depends on it.
// We should consider adding this as a public helper function.
void qt_blurImage(QPainter *p, QImage &blurImage, qreal radius, bool quality, bool alphaOnly,
                  int transposed = 0);
QT_END_NAMESPACE

namespace Utils {

void StyleHelper::drawArrow(QStyle::PrimitiveElement element, QPainter *painter, const QStyleOption *option)
{
    if (option->rect.width() <= 1 || option->rect.height() <= 1)
        return;

    const qreal devicePixelRatio = painter->device()->devicePixelRatio();
    const bool enabled = option->state & QStyle::State_Enabled;
    QRect r = option->rect;
    int size = qMin(r.height(), r.width());
    QPixmap pixmap;
    const QString pixmapName = QString::asprintf("StyleHelper::drawArrow-%d-%d-%d-%f",
                       element, size, enabled, devicePixelRatio);
    if (!QPixmapCache::find(pixmapName, &pixmap)) {
        QImage image(size * devicePixelRatio, size * devicePixelRatio, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        QPainter painter(&image);

        QStyleOption tweakedOption(*option);
        tweakedOption.state = QStyle::State_Enabled;

        const QCommonStyle *const style = qobject_cast<QCommonStyle *>(QApplication::style());
        auto drawCommonStyleArrow = [&tweakedOption,
                                     element,
                                     &painter,
                                     style](const QRect &rect, const QColor &color) -> void {
            if (!style)
                return;

            // Workaround for QTCREATORBUG-28470
            QPalette pal = tweakedOption.palette;
            pal.setBrush(QPalette::Base, pal.text()); // Base and Text differ, causing a detachment.
                                                      // Inspired by tst_QPalette::cacheKey()
            pal.setColor(QPalette::ButtonText, color.rgb());

            tweakedOption.palette = pal;
            tweakedOption.rect = rect;
            painter.setOpacity(color.alphaF());
            style->QCommonStyle::drawPrimitive(element, &tweakedOption, &painter);
        };

        if (!enabled) {
            drawCommonStyleArrow(image.rect(), creatorColor(Theme::IconsDisabledColor));
        } else {
            if (creatorTheme()->flag(Theme::ToolBarIconShadow))
                drawCommonStyleArrow(image.rect().translated(0, devicePixelRatio), toolBarDropShadowColor());
            drawCommonStyleArrow(image.rect(), creatorColor(Theme::IconsBaseColor));
        }
        painter.end();
        pixmap = QPixmap::fromImage(image);
        pixmap.setDevicePixelRatio(devicePixelRatio);
        QPixmapCache::insert(pixmapName, pixmap);
    }
    int xOffset = r.x() + (r.width() - size)/2;
    int yOffset = r.y() + (r.height() - size)/2;
    painter->drawPixmap(xOffset, yOffset, pixmap);
}

void StyleHelper::drawMinimalArrow(QStyle::PrimitiveElement element, QPainter *painter, const QStyleOption *option)
{
    if (option->rect.width() <= 1 || option->rect.height() <= 1)
        return;

    const qreal devicePixelRatio = painter->device()->devicePixelRatio();
    const bool enabled = option->state & QStyle::State_Enabled;
    QRect r = option->rect;
    int size = qMin(r.height(), r.width());
    QPixmap pixmap;
    const QString pixmapName = QString::asprintf("StyleHelper::drawMinimalArrow-%d-%d-%d-%f",
                                                 element, size, enabled, devicePixelRatio);
    if (!QPixmapCache::find(pixmapName, &pixmap)) {
        QImage image(size * devicePixelRatio, size * devicePixelRatio, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        QPainter painter(&image);
        QStyleOption tweakedOption(*option);

        double rotation = 0;
        switch (element) {
        case QStyle::PE_IndicatorArrowLeft:
            rotation = 45;
            break;
        case QStyle::PE_IndicatorArrowUp:
            rotation = 135;
            break;
        case QStyle::PE_IndicatorArrowRight:
            rotation = 225;
            break;
        case QStyle::PE_IndicatorArrowDown:
            rotation = 315;
            break;
        default:
            break;
        }

        auto drawArrow = [&tweakedOption, rotation, &painter](const QRect &rect, const QColor &color) -> void
        {
            static const QCommonStyle* const style = qobject_cast<QCommonStyle*>(QApplication::style());
            if (!style)
                return;

            // Workaround for QTCREATORBUG-28470
            QPalette pal = tweakedOption.palette;
            pal.setBrush(QPalette::Base, pal.text()); // Base and Text differ, causing a detachment.
            // Inspired by tst_QPalette::cacheKey()
            pal.setColor(QPalette::ButtonText, color.rgb());

            tweakedOption.palette = pal;
            tweakedOption.rect = rect;

            painter.save();
            painter.setOpacity(color.alphaF());

            double minDim = std::min(rect.width(), rect.height());
            double innerWidth = minDim/M_SQRT2;
            int penWidth = std::max(innerWidth/4, 1.0);
            innerWidth -= penWidth;

            QPen pPen(pal.color(QPalette::ButtonText), penWidth);
            pPen.setJoinStyle(Qt::MiterJoin);
            painter.setBrush(pal.text());
            painter.setPen(pPen);

            painter.translate(rect.center());
            painter.rotate(rotation);
            painter.translate(-innerWidth/2, -innerWidth/2);

            const QPointF points[3] = {
                {0, 0},
                {0, innerWidth},
                {innerWidth, innerWidth}
            };

            painter.drawPolyline(points, 3);
            painter.restore();
        };

        if (enabled) {
            if (creatorTheme()->flag(Theme::ToolBarIconShadow))
                drawArrow(image.rect().translated(0, devicePixelRatio), toolBarDropShadowColor());
            drawArrow(image.rect(), creatorColor(Theme::IconsBaseColor));
        } else {
            drawArrow(image.rect(), creatorColor(Theme::IconsDisabledColor));
        }
        painter.end();
        pixmap = QPixmap::fromImage(image);
        pixmap.setDevicePixelRatio(devicePixelRatio);
        QPixmapCache::insert(pixmapName, pixmap);
    }
    int xOffset = r.x() + (r.width() - size)/2;
    int yOffset = r.y() + (r.height() - size)/2;
    painter->drawPixmap(xOffset, yOffset, pixmap);
}

// Draws a cached pixmap with shadow
void StyleHelper::drawIconWithShadow(const QIcon &icon, const QRect &rect,
                                     QPainter *p, QIcon::Mode iconMode, QIcon::State iconState,
                                     int dipRadius, const QColor &color, const QPoint &dipOffset)
{
    QPixmap cache;
    const qreal devicePixelRatio = p->device()->devicePixelRatioF();
    QString pixmapName = QString::fromLatin1("icon %0 %1 %2 %3")
            .arg(icon.cacheKey()).arg(iconMode).arg(rect.height()).arg(devicePixelRatio);

    if (!QPixmapCache::find(pixmapName, &cache)) {
        // High-dpi support: The in parameters (rect, radius, offset) are in
        // device-independent pixels. The call to QIcon::pixmap() below might
        // return a high-dpi pixmap, which will in that case have a devicePixelRatio
        // different than 1. The shadow drawing caluculations are done in device
        // pixels.
        QPixmap px = icon.pixmap(rect.size(), devicePixelRatio, iconMode, iconState);
        // icon.pixmap() never upscales: if no source pixmap with a high enough
        // resolution is available (e.g. icons only have up to @2x variants, but
        // devicePixelRatio is higher), it returns a smaller pixmap with a lower
        // devicePixelRatio instead. Use that actual ratio, otherwise the icon
        // would be painted too small.
        const qreal pixmapDevicePixelRatio = px.devicePixelRatio();
        int radius = int(dipRadius * pixmapDevicePixelRatio);
        QPoint offset = dipOffset * pixmapDevicePixelRatio;
        cache = QPixmap(px.size() + QSize(radius * 2, radius * 2));
        cache.fill(Qt::transparent);

        QPainter cachePainter(&cache);
        if (iconMode == QIcon::Disabled) {
            const bool hasDisabledState =
                    icon.availableSizes().count() == icon.availableSizes(QIcon::Disabled).count();
            if (!hasDisabledState)
                px = disabledSideBarIcon(icon.pixmap(rect.size(), devicePixelRatio));
        } else if (creatorTheme()->flag(Theme::ToolBarIconShadow)) {
            // Draw shadow
            QImage tmp(px.size() + QSize(radius * 2, radius * 2 + 1), QImage::Format_ARGB32_Premultiplied);
            tmp.fill(Qt::transparent);

            QPainter tmpPainter(&tmp);
            tmpPainter.setCompositionMode(QPainter::CompositionMode_Source);
            tmpPainter.drawPixmap(QRect(radius, radius, px.width(), px.height()), px);
            tmpPainter.end();

            // blur the alpha channel
            QImage blurred(tmp.size(), QImage::Format_ARGB32_Premultiplied);
            blurred.fill(Qt::transparent);
            QPainter blurPainter(&blurred);
            qt_blurImage(&blurPainter, tmp, radius, false, true);
            blurPainter.end();

            tmp = blurred;

            // blacken the image...
            tmpPainter.begin(&tmp);
            tmpPainter.setCompositionMode(QPainter::CompositionMode_SourceIn);
            tmpPainter.fillRect(tmp.rect(), color);
            tmpPainter.end();

            // draw the blurred drop shadow...
            cachePainter.drawImage(QRect(0, 0, cache.rect().width(), cache.rect().height()), tmp);
        }

        // Draw the actual pixmap...
        cachePainter.drawPixmap(QRect(QPoint(radius, radius) + offset, QSize(px.width(), px.height())), px);
        cachePainter.end();
        cache.setDevicePixelRatio(pixmapDevicePixelRatio);
        QPixmapCache::insert(pixmapName, cache);
    }

    QRect targetRect = cache.rect();
    targetRect.setSize(targetRect.size() / cache.devicePixelRatio());
    targetRect.moveCenter(rect.center() - dipOffset);
    p->drawPixmap(targetRect, cache);
}

void StyleHelper::applyTf(QLabel *label, const StyleHelper::TextFormat &tf, bool singleLine)
{
    if (singleLine)
        label->setFixedHeight(tf.lineHeight());
    label->setFont(tf.font());
    label->setAlignment(Qt::Alignment(tf.drawTextFlags));
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);

    QPalette pal = label->palette();
    pal.setColor(QPalette::WindowText, tf.color());
    label->setPalette(pal);
}

void StyleHelper::setBaseColor(const QColor &newcolor)
{
    if (!Utils::StyleHelper::storeBaseColor(newcolor))
        return;

    const QWidgetList widgets = QApplication::allWidgets();
    for (QWidget *w : widgets)
        w->update();
}

QIcon StyleHelper::getIconFromIconFont(const QString &fontName, const QString &iconSymbol, int fontSize, int iconSize)
{
    QColor penColor = QApplication::palette("QWidget").color(QPalette::Normal, QPalette::ButtonText);
    return getIconFromIconFont(fontName, iconSymbol, fontSize, iconSize, penColor);
}

void StyleHelper::setPanelWidget(QWidget *widget, bool value)
{
    widget->setProperty(C_PANEL_WIDGET, value);
}

void StyleHelper::setPanelWidgetSingleRow(QWidget *widget, bool value)
{
    widget->setProperty(C_PANEL_WIDGET_SINGLE_ROW, value);
}

void StyleHelper::modifyPaletteBase(QWidget *widget, const QColor &color)
{
    QTC_ASSERT(widget, return);
    QPalette palette = widget->palette();
    palette.setColor(QPalette::Base, color);
    widget->setPalette(palette);
}

void StyleHelper::setBackgroundColor(QWidget *widget, Theme::Color colorRole)
{
    QPalette palette = widget->palette();
    const QPalette::ColorRole role = QPalette::Window;
    palette.setBrush(role, {});
    palette.setColor(role, creatorColor(colorRole));
    widget->setPalette(palette);
    widget->setBackgroundRole(role);
    widget->setAutoFillBackground(true);
}

} // namespace Utils
