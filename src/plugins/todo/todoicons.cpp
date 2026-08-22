// Copyright (C) 2016 Dmitry Savchenko
// Copyright (C) 2016 Vasiliy Sorokin
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "todoicons.h"

#include <utils/icon.h>
#include <utils/theme/theme.h>
#include <utils/themedvalue.h>
#include <utils/utilsicons.h>

using namespace Utils;

namespace Todo::Internal {

QIcon icon(IconType type)
{
    switch (type) {
    case IconType::Info:
        return Utils::Icons::INFO.icon();
    case IconType::Warning:
        return Utils::Icons::WARNING.icon();
    case IconType::Bug: {
        static const ThemedValue<QIcon> icon([] {
            return Icon({
                            {":/todoplugin/images/bugfill.png", Theme::BackgroundColorNormal},
                            {":/todoplugin/images/bug.png", Theme::IconsInterruptColor}
                        }, Icon::Tint).icon();
        });
        return icon();
    }
    case IconType::Todo: {
        static const ThemedValue<QIcon> icon([] {
            return Icon({
                            {":/todoplugin/images/tasklist.png", Theme::IconsRunColor}
                        }, Icon::Tint).icon();
        });
        return icon();
    }
    default:
    case IconType::Error:
        return Utils::Icons::CRITICAL.icon();
    }
}

QIcon toolBarIcon(IconType type)
{
    switch (type) {
    case IconType::Info:
        return Icons::INFO_TOOLBAR.icon();
    case IconType::Warning:
        return Icons::WARNING_TOOLBAR.icon();
    case IconType::Bug:
        return Icon({{":/todoplugin/images/bug.png", Theme::IconsInterruptToolBarColor}}).icon();
    case IconType::Todo:
        return Icon({{":/todoplugin/images/tasklist.png", Theme::IconsRunToolBarColor}}).icon();
    default:
    case IconType::Error:
        return Icons::CRITICAL_TOOLBAR.icon();
    }
}

} // namespace Todo::Internal
