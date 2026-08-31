// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "themecolors.h"

#include <utils/theme/theme.h>

namespace ScxmlEditor::Common {

const QList<QColor> &defaultThemeColors()
{
    // Left with hardcoded values for now
    if (Utils::creatorTheme()->colorScheme() == Qt::ColorScheme::Dark) {
        static const QList<QColor> colors = {
            QColor(0x64, 0x64, 0x64),
            QColor(0x60, 0x68, 0x59),
            QColor(0x6f, 0x6c, 0x58),
            QColor(0x57, 0x69, 0x6f),
            QColor(0x57, 0x58, 0x61),
            QColor(0x74, 0x63, 0x50),
            QColor(0x75, 0x5a, 0x5a)
        };
        return colors;
    }
    static const QList<QColor> colors = {
        QColor(0xe0, 0xe0, 0xe0),
        QColor(0xd3, 0xe4, 0xc3),
        QColor(0xeb, 0xe4, 0xba),
        QColor(0xb8, 0xdd, 0xeb),
        QColor(0xc7, 0xc8, 0xdd),
        QColor(0xf0, 0xce, 0xa5),
        QColor(0xf1, 0xba, 0xba)
    };
    return colors;
}

} // namespace ScxmlEditor::Common
