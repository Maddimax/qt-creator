// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "qmljseditor_global.h"

#include <languageserverprotocol/servercapabilities.h>
#include <texteditor/textdocument.h>
#include <qmljstools/qmljssemanticinfo.h>

#include <QTextLayout>

namespace QmlJSEditor {

namespace Internal {
class QmlJSEditorDocumentPrivate;
class QmlOutlineModel;
} // Internal

class QMLJSEDITOR_EXPORT QmlJSEditorDocument : public TextEditor::TextDocument
{
    Q_OBJECT

public:
    QmlJSEditorDocument(Utils::Id id);
    ~QmlJSEditorDocument() override;

    void autoFormat(const QTextCursor &cursor) override;

    const QmlJSTools::SemanticInfo &semanticInfo() const;
    bool isSemanticInfoOutdated() const;
    QVector<QTextLayout::FormatRange> diagnosticRanges() const;
    void setDiagnosticRanges(const QVector<QTextLayout::FormatRange> &ranges);
    Internal::QmlOutlineModel *outlineModel() const;

    TextEditor::IAssistProvider *quickFixAssistProvider() const override;

    // The Refactoring submenu a right click offers, built from the quick fixes
    // proposed at \a cursor. The editor widget builds its own menu and never
    // asks for this; a view that is not one has no other way to get it.
    QList<QAction *> contextMenuActions(const QTextCursor &cursor) override;

    // The licence header every language folds, and the block the QML designer
    // writes at the end of a file.
    void foldOnFirstOpen() override;

    // Folds that block wherever it is. Public because the editor widget folds
    // it again when restoring a state written before this was remembered.
    void foldAuxiliaryData();

    // What this language proposes, which is a question about the file rather
    // than about the view showing it - and which is what a view that is not a
    // widget asks. Completion only: a quick fix still needs the widget it is
    // going to act on.
    std::unique_ptr<TextEditor::AssistInterface> createAssistInterface(
        const QTextCursor &cursor, TextEditor::AssistKind kind, TextEditor::AssistReason reason,
        Core::IEditor *editor) const override;

    void setIsDesignModePreferred(bool value);
    bool isDesignModePreferred() const override;

    void setSourcesWithCapabilities(const LanguageServerProtocol::ServerCapabilities &cap);

signals:
    void semanticInfoUpdated(const QmlJSTools::SemanticInfo &semanticInfo);

protected:
    void applyFontSettings() override;
    void triggerPendingUpdates() override;

private:
    // The one Refactoring action, refilled per click. A QAction does not own
    // the menu it carries, so this owns both.
    QAction *m_refactoringAction = nullptr;

    // The parse errors, as extra selections on this document. It used to be a
    // signal the editor widget answered, which left a QML file open in any
    // other view underlining nothing.
    void updateCodeWarnings(QmlJS::Document::Ptr doc);

    friend class Internal::QmlJSEditorDocumentPrivate; // sending signals
    Internal::QmlJSEditorDocumentPrivate *d;
};


#ifdef WITH_TESTS
QObject *createQmlJSEditorDocumentTest();
#endif

} // QmlJSEditor
