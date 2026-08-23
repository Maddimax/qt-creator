// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include <utils/theme/theme.h>
#include <utils/shutdownguard.h>
#include <utils/theme/theme_p.h>

#include <QSettings>
#include <QTemporaryFile>
#include <QTest>

#include <memory>

using namespace Utils;

class TestTheme : public Theme
{
public:
    TestTheme() : Theme(QLatin1String("test")) {}
};

// A theme that replaces the platform palette, so that palette() actually
// consults the base rather than returning it untouched.
static Theme *overridingTheme()
{
    QTemporaryFile file;
    file.setAutoRemove(false);
    if (!file.open())
        return nullptr;
    file.write("[Flags]\nDerivePaletteFromTheme=true\n"
               "[Colors]\nPaletteWindowText=ff112233\n");
    file.close();

    auto theme = new TestTheme;
    QSettings settings(file.fileName(), QSettings::IniFormat);
    theme->readSettings(settings);
    QFile::remove(file.fileName());
    return theme;
}

class tst_Theme : public QObject
{
    Q_OBJECT

private slots:
    void fallsBackToTheDefaultBasePalette();
    void prefersItsOwnBasePalette();
    void doesNotReadTheApplicationPalette();
    // Last on purpose: it tears the shutdown guard down, so every guarded
    // singleton in this process is gone afterwards.
    void survivesAThemeChangeAfterShutdown();
};

void tst_Theme::fallsBackToTheDefaultBasePalette()
{
    QPalette base;
    base.setColor(QPalette::ToolTipBase, QColor(1, 2, 3));
    Theme::setDefaultBasePalette(base);

    const TestTheme theme;
    QCOMPARE(theme.basePalette().color(QPalette::ToolTipBase), QColor(1, 2, 3));
    QCOMPARE(Theme::defaultBasePalette().color(QPalette::ToolTipBase), QColor(1, 2, 3));
    // The long-standing accessor keeps returning the same thing.
    QCOMPARE(Theme::initialPalette().color(QPalette::ToolTipBase), QColor(1, 2, 3));
}

void tst_Theme::prefersItsOwnBasePalette()
{
    QPalette base;
    base.setColor(QPalette::ToolTipBase, QColor(1, 2, 3));
    Theme::setDefaultBasePalette(base);

    QPalette own;
    own.setColor(QPalette::ToolTipBase, QColor(4, 5, 6));

    TestTheme theme;
    theme.setBasePalette(own);
    QCOMPARE(theme.basePalette().color(QPalette::ToolTipBase), QColor(4, 5, 6));
    // The default is untouched by a theme carrying its own.
    QCOMPARE(Theme::defaultBasePalette().color(QPalette::ToolTipBase), QColor(1, 2, 3));

    // And palette() builds on the theme's own base, not the default.
    QCOMPARE(theme.palette().color(QPalette::ToolTipBase), QColor(4, 5, 6));
}

// The point of the exercise: a theme resolves its palette without asking the
// application, so it works in a process that has no QApplication palette set up
// for it and pulls no QtWidgets into theme.cpp.
void tst_Theme::doesNotReadTheApplicationPalette()
{
    QPalette base;
    base.setColor(QPalette::WindowText, QColor(9, 9, 9));
    Theme::setDefaultBasePalette(base);

    // Not installed with setCreatorTheme(): that applies the palette to the
    // application, which needs a QGuiApplication. Resolving a palette does not,
    // which is the property under test.
    const std::unique_ptr<Theme> theme(overridingTheme());
    QVERIFY(theme);

    // The themed colour wins where the theme defines one.
    QCOMPARE(theme->palette().color(QPalette::WindowText), QColor(0x11, 0x22, 0x33));
    // Roles the theme says nothing about come from the base it was given.
    QCOMPARE(theme->palette().color(QPalette::ToolTipBase),
             base.color(QPalette::ToolTipBase));
}

QTEST_GUILESS_MAIN(tst_Theme)

void tst_Theme::survivesAThemeChangeAfterShutdown()
{
    // CorePlugin's destructor installs a null theme, and by then the guard has
    // taken ThemeWatcher with it. Emitting the change on the dead watcher used
    // to abort on exit, every time.
    // The watcher has to exist before the guard goes, as it does in the real
    // application: it is a lazy singleton, so asking for it afterwards would
    // just build a new one.
    QVERIFY(ThemeWatcher::instance());
    triggerShutdownGuard();
    QVERIFY(!ThemeWatcher::instance());

    const int before = ThemeWatcher::generation();
    setCreatorTheme(new TestTheme);
    QCOMPARE(ThemeWatcher::generation(), before + 1);
    setCreatorTheme(nullptr);
    QCOMPARE(ThemeWatcher::generation(), before + 2);
}

#include "tst_theme.moc"
