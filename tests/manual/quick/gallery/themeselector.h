// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <QComboBox>

namespace ManualTest {

// Like tests/manual/widgets/common/themeselector.h, but without the
// Core::ManhattanStyle dependency, which a Qt Quick scene does not use.
class ThemeSelector : public QComboBox
{
public:
    explicit ThemeSelector(QWidget *parent = nullptr);

    static void setTheme(const QString &themeFile);
};

} // namespace ManualTest
