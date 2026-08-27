// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "texteditor_global.h"

#include <utils/aspects.h>

namespace TextEditor {

/**
 * Settings that describe how the text editor behaves. This does not include
 * the TabSettings and StorageSettings.
 */
class TEXTEDITOR_EXPORT BehaviorSettingsData final
{
public:
    BehaviorSettingsData() = default;

    bool m_mouseHiding = true;
    bool m_mouseNavigation = true;
    bool m_scrollWheelZooming = true;
    bool m_constrainHoverTooltips = false;
    bool m_camelCaseNavigation = true;
    bool m_keyboardTooltips = false;
    bool m_smartSelectionChanging = true;
};

class TEXTEDITOR_EXPORT BehaviorSettings final : public Utils::AspectContainer
{
public:
    explicit BehaviorSettings(const Utils::Key &keyPrefix = {});

    BehaviorSettingsData data() const;
    void setData(const BehaviorSettingsData &data);

    void apply() final;

    Utils::BoolAspect mouseHiding{this};
    Utils::BoolAspect mouseNavigation{this};
    Utils::BoolAspect scrollWheelZooming{this};
    Utils::SelectionAspect constrainHoverTooltips{this};
    Utils::BoolAspect camelCaseNavigation{this};
    Utils::BoolAspect keyboardTooltips{this};
    Utils::BoolAspect smartSelectionChanging{this};
};

// Whether the mouse pointer is taken off the screen while the user types.
// Never where the platform does it itself - a Mac, whose window server fights
// a second attempt at it. That is a decision about the platform and not about
// the editor, so both editors ask here rather than each remembering.
//
// \a platformHidesPointerItself is a parameter so that the answer can be
// asked for both kinds of platform from the one the tests happen to run on.
TEXTEDITOR_EXPORT bool hideMouseWhileTyping(const BehaviorSettingsData &settings,
                                            bool platformHidesPointerItself);
TEXTEDITOR_EXPORT bool hideMouseWhileTyping(const BehaviorSettingsData &settings);

// Whether pressing \a key counts as typing for the purpose above. A key that
// only says how to read the next one does not: the pointer has to survive
// Shift being held down.
TEXTEDITOR_EXPORT bool isTypingKey(int key);

TEXTEDITOR_EXPORT BehaviorSettings &globalBehaviorSettings();

namespace Internal { void setupBehaviorSettings(); }

} // namespace TextEditor
