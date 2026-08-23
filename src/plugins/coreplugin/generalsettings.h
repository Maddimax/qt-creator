// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "core_global.h"

#include <utils/aspects.h>

namespace Core {

class CORE_EXPORT CodecForLocaleAspect : public Utils::StringSelectionAspect
{
public:
    explicit CodecForLocaleAspect(Utils::AspectContainer *container = nullptr);
};

class CORE_EXPORT LanguageSelectionAspect : public Utils::StringSelectionAspect
{
public:
    explicit LanguageSelectionAspect(Utils::AspectContainer *container = nullptr);

    static inline const QString kSystemLanguage = "__system__";

    QVariant toSettingsValue(const QVariant &valueToSave) const override;
    QVariant fromSettingsValue(const QVariant &savedValue) const override;
};

class CORE_EXPORT ThemeSelectionAspect : public Utils::StringSelectionAspect
{
public:
    explicit ThemeSelectionAspect(Utils::AspectContainer *container = nullptr);
};

class CORE_EXPORT GeneralSettings : public Utils::AspectContainer
{
public:
    GeneralSettings();

    Utils::BoolAspect showShortcutsInContextMenus{this};
    Utils::BoolAspect provideSplitterCursors{this};
    Utils::BoolAspect preferInfoBarOverPopup{this};
    Utils::BoolAspect useTabsInEditorViews{this};
    Utils::BoolAspect showOkAndCancelInSettingsMode{this};

    Utils::SelectionAspect toolbarStyle{this};
    Utils::SelectionAspect highDpiScaleFactorRoundingPolicy{this};

    CodecForLocaleAspect codecForLocale{this};
    LanguageSelectionAspect language{this};
    Utils::ColorAspect color{this};
    ThemeSelectionAspect theme{this};
    Utils::TextDisplay envVarInfo{this};
    Utils::ActionAspect resetWarnings{this};
};

CORE_EXPORT GeneralSettings &generalSettings();

} // Core
