// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "../core_global.h"
#include "../icontext.h"

#include <QMetaType>

namespace Core {

class IDocument;

class CORE_EXPORT IEditor : public IContext
{
    Q_OBJECT

public:
    IEditor();

    bool duplicateSupported() const;
    void setDuplicateSupported(bool duplicateSupported);

    virtual IDocument *document() const = 0;

    virtual IEditor *duplicate() { return nullptr; }

    virtual QByteArray saveState() const { return QByteArray(); }
    virtual void restoreState(const QByteArray & /*state*/) {}

    virtual int currentLine() const { return 0; }
    virtual int currentColumn() const { return 0; }
    // What the reader has selected, for whoever wants to act on that rather
    // than on the whole document. Empty when nothing is selected, and for a
    // view where selecting means nothing.
    virtual QString selectedText() const { return {}; }
    virtual void gotoLine(int line, int column = 0, bool centerLine = true) { Q_UNUSED(line) Q_UNUSED(column) Q_UNUSED(centerLine) }
    // Extend the selection from where the caret is to \a line and \a column,
    // in the same one-based line and zero-based column gotoLine() takes. The
    // search results pane asks for this so that a match stands out instead of
    // leaving a caret at its start; an editor that cannot select leaves it be.
    virtual void selectTo(int line, int column) { Q_UNUSED(line) Q_UNUSED(column) }

    virtual QWidget *toolBar() = 0;

    virtual bool isDesignModePreferred() const { return false; }

signals:
    void editorDuplicated(IEditor *duplicate);
    // The caret moved. Whoever follows it - the outline, the type hierarchy -
    // asks the editor rather than the widget, so that a view which is not one
    // can be followed too.
    void cursorPositionChanged();
    // And when what is selected changed, which is not the same thing: a
    // selection can grow backwards over a word the caret is already at the end
    // of. Whoever follows one asks the editor for the same reason.
    void selectionChanged();

private:
    bool m_duplicateSupported;
};

} // namespace Core
