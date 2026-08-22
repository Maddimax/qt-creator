// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "themeselector.h"

#include <utils/stylehelper.h>
#include <utils/stylehelperpainting.h>
#include <utils/theme/theme.h>
#include <utils/theme/theme_p.h>

#include <QApplication>
#include <QDir>
#include <QSettings>

namespace ManualTest {

void ThemeSelector::setTheme(const QString &themeFile)
{
    using namespace Utils;

    static Theme theme("");
    QSettings settings(themeFile, QSettings::IniFormat);
    theme.readSettings(settings);
    setCreatorTheme(&theme);
    StyleHelper::setBaseColor(QColor(StyleHelper::DEFAULT_BASE_COLOR));
    QApplication::setPalette(theme.palette());
}

ThemeSelector::ThemeSelector(QWidget *parent)
    : QComboBox(parent)
{
    for (const QFileInfo &themeFile : QDir(":/themes/", "*.creatortheme").entryInfoList()) {
        QSettings settings(themeFile.absoluteFilePath(), QSettings::IniFormat);
        addItem(settings.value("ThemeName").toString(), themeFile.absoluteFilePath());
    }
    setCurrentText("Flat Dark");
    setTheme(currentData().toString());

    connect(this, &QComboBox::currentTextChanged, this, [this] {
        setTheme(currentData().toString());
    });
}

} // namespace ManualTest
