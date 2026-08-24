// Copyright (C) 2016 Dmitry Savchenko
// Copyright (C) 2016 Vasiliy Sorokin
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "keyword.h"

#include <QMetaType>
#include <QObject>

namespace Todo::Internal {

enum ScanningScope {
    ScanningScopeCurrentFile,
    ScanningScopeProject,
    ScanningScopeSubProject,
    ScanningScopeMax
};

class Settings
{
public:
    KeywordList keywords;
    ScanningScope scanningScope = ScanningScopeCurrentFile;
    bool keywordsEdited = false;

    void save() const;
    void load();
    void setDefault();
};

Settings &todoSettings();

void setupTodoSettingsPage();

#ifdef WITH_TESTS
QObject *createTodoSettingsTest();
#endif

} // Todo::Internal

Q_DECLARE_METATYPE(Todo::Internal::ScanningScope)
