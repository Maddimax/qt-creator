// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "theme/theme.h"

#include <functional>
#include <utility>

namespace Utils {

// Caches a value derived from the current theme and recomputes it after a theme
// change. Use it where a plain function-local static would latch a resolved
// colour, brush, pixmap or icon for the lifetime of the process:
//
//     static const ThemedValue<QPixmap> icon([] {
//         return Icon({{mask, Theme::Token_Text_Muted}}, Icon::Tint).pixmap();
//     });
//     painter->drawPixmap(rect, icon());
//
// Tinting is not free, which is why these are cached rather than recomputed on
// every paint. Not thread safe; use it from the GUI thread.
template<typename T>
class ThemedValue
{
public:
    explicit ThemedValue(std::function<T()> compute)
        : m_compute(std::move(compute))
    {}

    const T &operator()() const
    {
        const int generation = ThemeWatcher::generation();
        if (generation != m_generation) {
            m_value = m_compute();
            m_generation = generation;
        }
        return m_value;
    }

private:
    const std::function<T()> m_compute;
    mutable T m_value = {};
    mutable int m_generation = -1;
};

} // namespace Utils
