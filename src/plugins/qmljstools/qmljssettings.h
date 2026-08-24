// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "qmljstools_global.h"

#include <coreplugin/dialogs/ioptionspage.h>

#include <texteditor/icodestylepreferences.h>
#include <texteditor/codestyleeditor.h>

#include <utils/filepath.h>
#include <utils/store.h>

namespace TextEditor { class TabSettingsData; }

namespace QmlJSTools {

class QMLJSTOOLS_EXPORT QmlJSCodeStyleSettings
{
public:
    QmlJSCodeStyleSettings();

    enum Formatter {
        Builtin,
        QmlFormat,
        Custom
    };

    int lineLength = 80;
    QString qmlformatIniContent;
    Formatter formatter = QmlFormat;
    Utils::FilePath customFormatterPath;
    QString customFormatterArguments;

    void toMap(Utils::Store &map) const;
    void fromMap(const Utils::Store &map);

    bool equals(const QmlJSCodeStyleSettings &rhs) const;
    bool operator==(const QmlJSCodeStyleSettings &s) const { return equals(s); }

    static QmlJSCodeStyleSettings currentGlobalCodeStyle();
    static TextEditor::TabSettingsData currentGlobalTabSettings();
    static Utils::Id settingsId();
};

using QmlJSCodeStylePreferences = TextEditor::TypedCodeStylePreferences<QmlJSCodeStyleSettings>;

QMLJSTOOLS_EXPORT QmlJSCodeStylePreferences *globalQmlJSCodeStyle();

namespace Internal {

void setupQmlJSToolsSettings();

class QmlJSCodeStyleSettingsPage : public Core::IOptionsPage
{
public:
    QmlJSCodeStyleSettingsPage();
};

} // namespace Internal
} // namespace QmlJSTools

Q_DECLARE_METATYPE(QmlJSTools::QmlJSCodeStyleSettings)
