// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include <utils/themedvalue.h>
#include <utils/theme/theme.h>
#include <utils/theme/theme_p.h>

#include <QSettings>
#include <QTemporaryFile>
#include <QTest>

using namespace Utils;

class DummyTheme : public Theme
{
public:
    DummyTheme() : Theme(QLatin1String("dummy")) {}
};

static Theme *themeWithAccent(const QString &argb)
{
    QTemporaryFile file;
    file.setAutoRemove(false);
    if (!file.open())
        return nullptr;
    file.write(QString("[Colors]\nToken_Accent_Default=%1\n").arg(argb).toUtf8());
    file.close();

    auto theme = new DummyTheme;
    QSettings settings(file.fileName(), QSettings::IniFormat);
    theme->readSettings(settings);
    QFile::remove(file.fileName());
    return theme;
}

class tst_ThemedValue : public QObject
{
    Q_OBJECT

private slots:
    void cleanupTestCase();
    void recomputesAfterThemeChange();
    void computesLazilyAndOnlyOnce();
};

void tst_ThemedValue::cleanupTestCase()
{
    delete creatorTheme();
    setCreatorTheme(nullptr);
}

void tst_ThemedValue::recomputesAfterThemeChange()
{
    setCreatorTheme(themeWithAccent("ffff0000"));

    const ThemedValue<QColor> accent([] { return creatorColor(Theme::Token_Accent_Default); });
    QCOMPARE(accent(), QColor(255, 0, 0));

    setCreatorTheme(themeWithAccent("ff00ff00"));
    QCOMPARE(accent(), QColor(0, 255, 0));
}

void tst_ThemedValue::computesLazilyAndOnlyOnce()
{
    setCreatorTheme(themeWithAccent("ff0000ff"));

    int calls = 0;
    const ThemedValue<QColor> accent([&calls] {
        ++calls;
        return creatorColor(Theme::Token_Accent_Default);
    });

    // Nothing is computed until the value is asked for.
    QCOMPARE(calls, 0);
    QCOMPARE(accent(), QColor(0, 0, 255));
    QCOMPARE(calls, 1);
    // Repeated reads within one theme reuse the cached value, which is the
    // whole reason these are cached rather than recomputed on every paint.
    accent();
    accent();
    QCOMPARE(calls, 1);

    setCreatorTheme(themeWithAccent("ffffff00"));
    QCOMPARE(accent(), QColor(255, 255, 0));
    QCOMPARE(calls, 2);
}

QTEST_GUILESS_MAIN(tst_ThemedValue)

#include "tst_themedvalue.moc"
