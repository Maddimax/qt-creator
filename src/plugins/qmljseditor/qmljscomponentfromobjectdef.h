// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "qmljseditor_global.h"
#include "qmljsquickfix.h"

QT_BEGIN_NAMESPACE
class QTextCursor;
QT_END_NAMESPACE

namespace QmlJSEditor {

class QmlJSEditorDocument;

QMLJSEDITOR_EXPORT void matchComponentFromObjectDefQuickFix(
    const Internal::QmlJSQuickFixAssistInterface *interface, QuickFixOperations &result);

// Moves \a objDef out of \a document into a component file, and answers the
// new file's name. \a cursor is where the caret is in whichever view shows the
// document, which is what a quick fix takes; the document's semantic info is
// the rest of it.
QMLJSEDITOR_EXPORT QString performComponentFromObjectDef(QmlJSEditorDocument *document,
                                                         const QTextCursor &cursor,
                                                         const QString &fileName,
                                                         QmlJS::AST::UiObjectDefinition *objDef,
                                                         const QString &importData);

} // namespace QmlJSEditor
