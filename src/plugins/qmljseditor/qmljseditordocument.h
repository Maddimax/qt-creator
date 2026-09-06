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

    // What this language proposes, which is a question about the file rather
    // than about the view showing it - and which is what a view that is not a
    // widget asks. Completion only: a quick fix still needs the widget it is
    // going to act on.
    std::unique_ptr<TextEditor::AssistInterface> createAssistInterface(
        const QTextCursor &cursor, TextEditor::AssistKind kind, TextEditor::AssistReason reason,
        Core::IEditor *editor) const override;

    void setIsDesignModePreferred(bool value);
    bool isDesignModePreferred() const;

    void setSourcesWithCapabilities(const LanguageServerProtocol::ServerCapabilities &cap);

signals:
    void updateCodeWarnings(QmlJS::Document::Ptr doc);
    void semanticInfoUpdated(const QmlJSTools::SemanticInfo &semanticInfo);

protected:
    void applyFontSettings() override;
    void triggerPendingUpdates() override;

private:
    friend class Internal::QmlJSEditorDocumentPrivate; // sending signals
    Internal::QmlJSEditorDocumentPrivate *d;
};


#ifdef WITH_TESTS
QObject *createQmlJSEditorDocumentTest();
#endif

} // QmlJSEditor
