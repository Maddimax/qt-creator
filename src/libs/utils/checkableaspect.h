// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "aspects.h"
#include "layoutbuilder.h"
#include "qtcassert.h"

#include <memory>

namespace Utils::Internal {

// The optional check box that StringAspect and FilePathAspect can put next to
// their control. Shared between aspects.cpp and the widget renderer, which
// builds the composite; the enabling of the control itself is the renderer's
// job because only it has the widget.
class CheckableAspectImplementation
{
public:
    void fromMap(const Store &map)
    {
        if (m_checked)
            m_checked->fromMap(map);
    }

    void toMap(Store &map)
    {
        if (m_checked)
            m_checked->toMap(map);
    }

    void volatileToMap(Store &map)
    {
        if (m_checked)
            m_checked->volatileToMap(map);
    }

    void volatileFromMap(const Store &map)
    {
        if (m_checked)
            m_checked->volatileFromMap(map);
    }

    void setUncheckedSemantics(UncheckedSemantics semantics)
    {
        m_uncheckedSemantics = semantics;
    }

    bool isChecked() const
    {
        QTC_ASSERT(m_checked, return false);
        return m_checked->value();
    }

    void setChecked(bool checked)
    {
        QTC_ASSERT(m_checked, return);
        m_checked->setValue(checked);
    }

    bool isCheckable() const { return bool(m_checked); }

    void makeCheckable(CheckBoxPlacement checkBoxPlacement, const QString &checkerLabel,
                       const Key &checkerKey, BaseAspect *aspect)
    {
        QTC_ASSERT(!m_checked, return);
        m_checkBoxPlacement = checkBoxPlacement;
        m_checked.reset(new BoolAspect);
        m_checked->setLabel(checkerLabel, checkBoxPlacement == CheckBoxPlacement::Top
                                              ? BoolAspect::LabelPlacement::InExtraLabel
                                              : BoolAspect::LabelPlacement::AtCheckBox);
        m_checked->setSettingsKey(checkerKey);
        m_checked->addOnChanged(aspect, [aspect] {
            // FIXME: Check.
            aspect->valueToVolatileValue();
            aspect->volatileValueToGui();
            emit aspect->changed();
            aspect->checkedChanged();
        });
        m_checked->addOnVolatileValueChanged(aspect, [aspect] {
            // FIXME: Check.
            aspect->valueToVolatileValue();
            aspect->volatileValueToGui();
        });

        aspect->valueToVolatileValue();
        aspect->volatileValueToGui();
    }

    void addToLayoutFirst(Layouting::Layout &parent)
    {
        if (m_checked) {
            if (m_checkBoxPlacement == CheckBoxPlacement::Top) {
                m_checked->addToLayoutImpl(parent);
                parent.flush();
            } else if (m_checkBoxPlacement == CheckBoxPlacement::Left) {
                m_checked->addToLayoutImpl(parent);
            }
        }
    }

    void addToLayoutLast(Layouting::Layout &parent)
    {
        if (m_checked && m_checkBoxPlacement == CheckBoxPlacement::Right)
            m_checked->addToLayoutImpl(parent);
    }

    CheckBoxPlacement m_checkBoxPlacement = CheckBoxPlacement::Right;
    UncheckedSemantics m_uncheckedSemantics = UncheckedSemantics::Disabled;
    std::unique_ptr<BoolAspect> m_checked;
};

} // namespace Utils::Internal
