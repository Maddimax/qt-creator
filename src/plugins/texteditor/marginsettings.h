// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "texteditor_global.h"

#include <utils/aspects.h>

#include <QColor>

namespace TextEditor {

class Indenter;
class FontSettingsData;

class TEXTEDITOR_EXPORT MarginSettingsData
{
public:
    MarginSettingsData() = default;

    bool m_showMargin = false;
    bool m_tintMarginArea = true;
    bool m_useIndenter = false;
    int m_marginColumn = 80;
    int m_centerEditorContentWidthPercent = 100;
};

class TEXTEDITOR_EXPORT MarginSettings : public Utils::AspectContainer
{
public:
    explicit MarginSettings(const Utils::Key &keyPrefix = {});

    void apply() override;

    MarginSettingsData data() const;
    void setData(const MarginSettingsData &data);

    Utils::BoolAspect showMargin{this};
    Utils::BoolAspect tintMarginArea{this};
    Utils::BoolAspect useIndenter{this};
    Utils::IntegerAspect marginColumn{this};
    Utils::IntegerAspect centerEditorContentWidthPercent{this};
};

// Which column the right margin sits at, or 0 for no margin at all. The
// indenter gets first refusal where the settings say so: a language whose
// style has a line length of its own knows better than a number typed into
// Preferences.
TEXTEDITOR_EXPORT int visibleMarginColumn(const MarginSettingsData &settings,
                                          const Indenter *indenter);

// What the margin is drawn in: the line itself, and the tint over everything
// past it. Both are the editor's own background nudged towards its opposite,
// so a margin is visible on a light scheme and on a dark one without either
// being named here.
TEXTEDITOR_EXPORT QColor rightMarginColor(const FontSettingsData &fontSettings, bool areaColor);

TEXTEDITOR_EXPORT MarginSettings &marginSettings();

} // namespace TextEditor
