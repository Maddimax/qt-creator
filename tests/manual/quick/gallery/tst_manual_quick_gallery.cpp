// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "themeselector.h"

#include <qtcquick/qtcquickwidget.h>

#include <utils/layoutbuilder.h>
#include <utils/stylehelper.h>
#include <utils/theme/theme.h>

#include <QApplication>
#include <QMetaEnum>
#include <QQuickItem>
#include <QQuickWidget>
#include <QVariantMap>

using namespace Utils;

// The grids are driven from the meta-object system so that the gallery cannot
// fall behind the token, font and spacing enums.
static QStringList tokenNames()
{
    const QMetaEnum colors = QMetaEnum::fromType<Theme::Color>();
    QStringList names;
    for (int i = 0; i < colors.keyCount(); ++i) {
        const QByteArray key = colors.key(i);
        if (!key.startsWith("Token_"))
            continue;
        const QStringList parts = QString::fromUtf8(key).mid(6).split('_');
        QString name = parts.first().toLower();
        for (const QString &part : parts.mid(1))
            name += part.left(1).toUpper() + part.mid(1);
        names.append(name);
    }
    return names;
}

static QStringList fontNames()
{
    const QMetaEnum elements = QMetaEnum::fromType<StyleHelper::UiElement>();
    QStringList names;
    for (int i = 0; i < elements.keyCount(); ++i) {
        const QString name = QString::fromUtf8(elements.key(i)).mid(9);
        names.append(name.left(1).toLower() + name.mid(1));
    }
    return names;
}

static QVariantList spacingNames()
{
    const QMetaEnum spacing = QMetaEnum::fromType<StyleHelper::SpacingTokens::Spacing>();
    QVariantList tokens;
    for (int i = 0; i < spacing.keyCount(); ++i) {
        tokens.append(QVariantMap{{"name", QString::fromUtf8(spacing.key(i))},
                                  {"value", spacing.value(i)}});
    }
    return tokens;
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    auto themeSelector = new ManualTest::ThemeSelector;
    auto quickWidget = new QtcQuick::QuickWidget;

    const QVariantMap properties = {
        {"tokenNames", tokenNames()},
        {"fontNames", fontNames()},
        {"spacingNames", spacingNames()},
        {"iconMasks", QStringList{"/utils/images/home.png",
                                  "/utils/images/filtericon.png",
                                  "/utils/images/settings.png",
                                  "/utils/images/reload_gray.png"}},
    };
    quickWidget->quickWidget()->setInitialProperties(properties);
    quickWidget->setSource(QUrl("qrc:/qtcquickgallery/Gallery.qml"));

    // The QML scene reads the theme at creation, so rebuild it on a theme change.
    QObject::connect(themeSelector, &QComboBox::currentTextChanged, quickWidget,
                     [quickWidget, properties](const QString &) {
                         quickWidget->quickWidget()->setSource(QUrl());
                         quickWidget->quickWidget()->setInitialProperties(properties);
                         quickWidget->setSource(QUrl("qrc:/qtcquickgallery/Gallery.qml"));
                     });

    using namespace Layouting;
    QWidget *window = Column {
        Row { themeSelector, st },
        quickWidget,
    }.emerge();
    window->setWindowTitle("Qt Creator Qt Quick design system");
    window->resize(1100, 900);
    window->show();

    return app.exec();
}
