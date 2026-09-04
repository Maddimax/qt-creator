// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "sourceagent.h"

#include "debuggerengine.h"
#include "debuggericons.h"
#include "debuggerinternalconstants.h"
#include "debuggertr.h"
#include "stackhandler.h"

#include <coreplugin/editormanager/editormanager.h>
#include <coreplugin/idocument.h>

#include <texteditor/texteditor.h>
#include <texteditor/textdocument.h>
#include <texteditor/textmark.h>

#include <cppeditor/cppeditorconstants.h>

#include <utils/qtcassert.h>

#include <QDebug>

#include <limits.h>

using namespace Core;
using namespace TextEditor;

namespace Debugger::Internal {

class SourceAgentPrivate
{
public:
    SourceAgentPrivate();
    ~SourceAgentPrivate();

public:
    QPointer<Core::IEditor> editor;
    QPointer<DebuggerEngine> engine;
    TextMark *locationMark = nullptr;
    QString path;
    QString producer;
};

SourceAgentPrivate::SourceAgentPrivate()
  : producer("remote")
{
}

SourceAgentPrivate::~SourceAgentPrivate()
{
    if (editor)
        EditorManager::closeDocuments({editor->document()});
    editor = nullptr;
    delete locationMark;
}

SourceAgent::SourceAgent(DebuggerEngine *engine)
    : d(new SourceAgentPrivate)
{
    d->engine = engine;
}

SourceAgent::~SourceAgent()
{
    delete d;
    d = nullptr;
}

void SourceAgent::setSourceProducerName(const QString &name)
{
    d->producer = name;
}

void SourceAgent::setContent(const QString &filePath, const QString &content)
{
    QTC_ASSERT(d, return);
    d->path = filePath;

    if (!d->editor) {
        QString titlePattern = d->producer + ": "
            + Utils::FilePath::fromString(filePath).fileName();
        // Whatever the C++ factory builds. Asking for a BaseTextEditor here
        // made this whole view vanish behind a soft assert once C++ files
        // started opening in the Qt Quick editor.
        d->editor = EditorManager::openEditorWithContents(
            CppEditor::Constants::CPPEDITOR_ID, &titlePattern, content.toUtf8());
        QTC_ASSERT(d->editor, return);
        d->editor->document()->setProperty(Debugger::Constants::OPENED_BY_DEBUGGER, true);

        // Widget only: the Qt Quick gutter's mark column is always live.
        if (TextEditorWidget * const widget = TextEditorWidget::fromEditor(d->editor))
            widget->setRequestMarkEnabled(true);
    } else {
        EditorManager::activateEditor(d->editor);
    }

    setReadOnlyIn(d->editor, true);

    updateLocationMarker();
}

void SourceAgent::updateLocationMarker()
{
    QTC_ASSERT(d->editor, return);

    const auto document = qobject_cast<TextDocument *>(d->editor->document());
    QTC_ASSERT(document, return);

    if (d->locationMark)
        document->removeMark(d->locationMark);
    delete d->locationMark;
    d->locationMark = nullptr;
    if (d->engine->stackHandler()->currentFrame().file == Utils::FilePath::fromString(d->path)) {
        int lineNumber = d->engine->stackHandler()->currentFrame().line;

        d->locationMark = new TextMark(Utils::FilePath(),
                                       lineNumber,
                                       {Tr::tr("Debugger Location"),
                                        Constants::TEXT_MARK_CATEGORY_LOCATION});
        d->locationMark->setIcon(Icons::LOCATION.icon());
        d->locationMark->setPriority(TextMark::HighPriority);

        document->addMark(d->locationMark);
        // Where the debugger stopped, which is what the reader was brought
        // here to see. gotoLine() is what either view answers; setting a
        // cursor on the widget was only ever reaching through to this.
        d->editor->gotoLine(lineNumber);
        EditorManager::activateEditor(d->editor);
    }
}

} // Debugger::Internal
