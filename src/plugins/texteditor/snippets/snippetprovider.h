// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <texteditor/texteditor_global.h>

#include <QString>

#include <functional>

namespace TextEditor {

class TextEditorWidget;

class TEXTEDITOR_EXPORT SnippetProvider
{
public:
    SnippetProvider() = default;

    using EditorDecorator = std::function<void(TextEditorWidget *)>;

    static const QList<SnippetProvider> &snippetProviders();
    static void registerGroup(const QString &groupId, const QString &displayName,
                              EditorDecorator editorDecorator = EditorDecorator(),
                              const QString &mimeType = {});

    QString groupId() const;
    QString displayName() const;
    // What a snippet in this group is written in. The decorator only tells a
    // TextEditorWidget how to highlight itself; a renderer that has no widget
    // needs the mime type to look a definition up by.
    QString mimeType() const;

    static void decorateEditor(TextEditorWidget *editor, const QString &groupId);
    static QString mimeTypeForGroup(const QString &groupId);

private:
    QString m_groupId;
    QString m_displayName;
    QString m_mimeType;
    EditorDecorator m_editorDecorator;
};

} // TextEditor
