// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include <utils/icon.h>
#include <utils/theme/theme.h>
#include <utils/theme/theme_p.h>

#include <QPainter>
#include <QSettings>
#include <QTemporaryFile>
#include <QTest>

using namespace Utils;

class TestTheme : public Theme
{
public:
    TestTheme() : Theme(QLatin1String("test")) {}
};

// A theme whose IconsBaseColor is the given colour, so that a tinted icon built
// against it is visibly different from one built against another.
static Theme *themeWithIconColor(const QString &argb)
{
    QTemporaryFile file;
    file.setAutoRemove(false);
    if (!file.open())
        return nullptr;
    file.write(QString("[Colors]\nIconsBaseColor=%1\nIconsDisabledColor=%1\n")
                   .arg(argb).toUtf8());
    file.close();

    auto theme = new TestTheme;
    QSettings settings(file.fileName(), QSettings::IniFormat);
    theme->readSettings(settings);
    QFile::remove(file.fileName());
    return theme;
}

static QImage render(const QIcon &icon)
{
    return icon.pixmap(QSize(16, 16)).toImage();
}

class tst_Icon : public QObject
{
    Q_OBJECT

private slots:
    void iconFollowsAThemeChange();
    void iconIsStillCachedWithinOneTheme();
};

// Icon::icon() caches the tinted result. The cache used to be keyed on the
// device pixel ratio alone, so a themed icon kept the colour of whichever theme
// happened to be current when it was first asked for.
void tst_Icon::iconFollowsAThemeChange()
{
    const Icon icon({{FilePath::fromString(":/utils/images/filtericon.png"),
                      Theme::IconsBaseColor}}, Icon::Tint);

    setCreatorTheme(themeWithIconColor("ffff0000"));
    const QImage red = render(icon.icon());
    QVERIFY(!red.isNull());

    setCreatorTheme(themeWithIconColor("ff00ff00"));
    const QImage green = render(icon.icon());
    QVERIFY(!green.isNull());

    QCOMPARE(red.size(), green.size());
    QVERIFY2(red != green, "the icon kept the colour of the previous theme");
}

// Keying the cache on the theme must not have disabled it: asking twice under
// one theme has to return the same QIcon, not rebuild it.
void tst_Icon::iconIsStillCachedWithinOneTheme()
{
    const Icon icon({{FilePath::fromString(":/utils/images/filtericon.png"),
                      Theme::IconsBaseColor}}, Icon::Tint);

    setCreatorTheme(themeWithIconColor("ff0000ff"));
    QCOMPARE(icon.icon().cacheKey(), icon.icon().cacheKey());
}

QTEST_MAIN(tst_Icon)

#include "tst_icon.moc"
