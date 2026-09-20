// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <qmldesigner_global.h>

#include <plaintexteditmodifier.h>

#include <texteditor/textdocument.h>

#include <QStringList>

namespace QmlJS { class Snapshot; }

namespace QmlDesigner {

// The rewriter's way into the text of an open QML file: the document, which
// either view shows. What this adds to the plain modifier - the tab settings,
// the ids the semantic info knows, positions as lines and columns - are all
// the document's; it used to be built on the widget editor and so was the
// design document behind it.
class QMLDESIGNER_EXPORT BaseTextEditModifier : public PlainTextEditModifier
{
public:
    BaseTextEditModifier(TextEditor::TextDocument *document);

    void indentLines(int startLine, int endLine) override;
    void indent(int offset, int length) override;

    TextEditor::TabSettingsData tabSettings() const override;

    bool renameId(const QString &oldId, const QString &newId) override;
    QString moveToComponent(int nodeOffset, const QString &importData) override;

    QStringList autoComplete(QTextDocument *textDocument, int position, bool explicitComplete) override;

    void convertPosition(int pos, int *line, int *column) const override;

private:
    TextEditor::TextDocument *m_document;
};

#ifdef WITH_TESTS
QObject *createBaseTextEditModifierTest();
#endif

} // namespace QmlDesigner
