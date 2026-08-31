// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <QFrame>
#include <QVariantMap>

#include <memory>

namespace ScxmlEditor::Common {

class ColorSettingsAspects;

// The colour themes: which one is being edited, and the colours in it. A
// widget only so that ColorThemeDialog can hold it; everything it asks is
// aspects.
class ColorSettings : public QFrame
{
    Q_OBJECT

public:
    explicit ColorSettings(QWidget *parent = nullptr);
    ~ColorSettings() override;

    void save();

#ifdef WITH_TESTS
    ColorSettingsAspects *aspectsForTest() const { return d.get(); }
#endif

private:
    const std::unique_ptr<ColorSettingsAspects> d;
};

#ifdef WITH_TESTS
QObject *createColorSettingsTest();
#endif

} // namespace ScxmlEditor::Common
