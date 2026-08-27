// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "texteditor_global.h"
#include "formatter.h"
#include "indenter.h"

#include <coreplugin/textdocument.h>

#include <utils/id.h>
#include <utils/multitextcursor.h>

#include <QList>
#include <QMap>
#include <QTextCursor>
#include <QTextFormat>
#include <QSharedPointer>

#include <functional>

QT_BEGIN_NAMESPACE
class QAction;
class QTextCursor;
class QTextDocument;
QT_END_NAMESPACE

namespace TextEditor {

class CompletionAssistProvider;
class ExtraEncodingSettingsData;
class FontSettingsData;
class IAssistProvider;
class StorageSettingsData;
class SyntaxHighlighter;
class TabSettingsData;
class TextDocumentPrivate;
class TextMark;
class TextSuggestion;
class TypingSettingsData;

using TextMarks = QList<TextMark *>;

class TEXTEDITOR_EXPORT TextDocument : public Core::BaseTextDocument
{
    Q_OBJECT

public:
    explicit TextDocument(Utils::Id id = Utils::Id());
    ~TextDocument() override;

    static QMap<Utils::FilePath, QString> openedTextDocumentContents();
    static QMap<Utils::FilePath, Utils::TextEncoding> openedTextDocumentEncodings();
    static TextDocument *currentTextDocument();
    static TextDocument *textDocumentForFilePath(const Utils::FilePath &filePath);
    static QString convertToPlainText(const QString &rawText);

    QString plainText() const override;
    virtual QString textAt(int pos, int length) const;
    virtual QChar characterAt(int pos) const;
    QString blockText(int blockNumber) const;

    // A range of the document drawn differently by whatever is showing it: a
    // diagnostic, an unused symbol, a semantic highlight. These are facts
    // about the *document* - a warning is on the line whoever is looking - as
    // opposed to the ones that belong to one view, like where its caret is or
    // which bracket it is matching.
    //
    // A cursor rather than two offsets, so that an edit above moves it, and
    // QTextCharFormat rather than QTextEdit::ExtraSelection, which is the same
    // pair with a QtWidgets header in front of it.
    struct ExtraSelection
    {
        QTextCursor cursor;
        QTextCharFormat format;
    };

    void setExtraSelections(Utils::Id kind, const QList<ExtraSelection> &selections);
    QList<ExtraSelection> extraSelections(Utils::Id kind) const;
    // Every kind that has ever been set, including those since emptied, so
    // that a view can clear what it was drawing for one.
    QList<Utils::Id> extraSelectionKinds() const;

    void setTypingSettings(const TypingSettingsData &typingSettings);
    void setStorageSettings(const StorageSettingsData &storageSettings);
    void setExtraEncodingSettings(const ExtraEncodingSettingsData &extraEncodingSettings);

    const TypingSettingsData &typingSettings() const;
    const StorageSettingsData &storageSettings() const;
    virtual TabSettingsData tabSettings() const;
    const ExtraEncodingSettingsData &extraEncodingSettings() const;
    const FontSettingsData &fontSettings() const;

    void setIndenter(Indenter *indenter);
    Indenter *indenter() const;
    void autoIndent(const QTextCursor &cursor,
                    QChar typedChar = QChar::Null,
                    int currentCursorPosition = -1);
    void autoReindent(const QTextCursor &cursor, int currentCursorPosition = -1);

    // Put \a text in at \a cursor the way a paste does: replacing whatever
    // the cursor has selected, and letting the language decide the
    // indentation of what arrived. \a skipReindent is an editor's own "do not
    // format on paste" state. Leaves \a cursor over the inserted text when
    // \a selectInsertedText, which is what a drop wants and a paste does not.
    void insertWithIndentation(QTextCursor &cursor, const QString &text,
                               bool selectInsertedText, bool skipReindent = false);
    void autoFormatOrIndent(const QTextCursor &cursor);
    Utils::MultiTextCursor indent(const Utils::MultiTextCursor &cursor);
    Utils::MultiTextCursor unindent(const Utils::MultiTextCursor &cursor);

    Formatter* formatter() const;
    void setFormatter(Formatter *indenter); // transfers ownership
    void setFormatterMode(Formatter::FormatMode mode);
    virtual void autoFormat(const QTextCursor &cursor);
    bool applyChangeSet(const Utils::ChangeSet &changeSet);

    TextMarks marks() const;
    bool addMark(TextMark *mark);
    TextMarks marksAt(int line) const;
    void removeMark(TextMark *mark);
    void updateLayout() const;
    void scheduleUpdateLayout() const;
    void updateMark(TextMark *mark);
    void moveMark(TextMark *mark, int previousLine);
    void removeMarkFromMarksCache(TextMark *mark);
    static void temporaryHideMarksAnnotation(const Utils::Id &category);
    static void showMarksAnnotation(const Utils::Id &category);
    static bool marksAnnotationHidden(const Utils::Id &category);

    // IDocument implementation.
    QByteArray contents() const override;
    Utils::Result<> setContents(const QByteArray &contents) override;
    void formatContents() override;
    bool shouldAutoSave() const override;
    bool isModified() const override;
    bool isSaveAsAllowed() const override;
    Utils::Result<> reload(ReloadFlag flag, ChangeType type) override;
    void setFilePath(const Utils::FilePath &newName) override;
    ReloadBehavior reloadBehavior(ChangeTrigger state, ChangeType type) const override;

    Utils::FilePath fallbackSaveAsPath() const override;
    QString fallbackSaveAsFileName() const override;

    void setFallbackSaveAsPath(const Utils::FilePath &fallbackSaveAsPath);
    void setFallbackSaveAsFileName(const QString &fallbackSaveAsFileName);

    Utils::Result<> open(const Utils::FilePath &filePath,
                         const Utils::FilePath &realFilePath) override;
    virtual Utils::Result<> reload();
    Utils::Result<> reload(const Utils::FilePath &realFilePath);

    Utils::Result<> setPlainText(const QString &text);
    QTextDocument *document() const;

    using SyntaxHighLighterCreator = std::function<SyntaxHighlighter *()>;
    void resetSyntaxHighlighter(const SyntaxHighLighterCreator &creator);
    SyntaxHighlighter *syntaxHighlighter() const;

    Utils::Result<> reload(const Utils::TextEncoding &encoding);
    void cleanWhitespace(const QTextCursor &cursor);

    virtual void triggerPendingUpdates();

    virtual void setCompletionAssistProvider(CompletionAssistProvider *provider);
    virtual CompletionAssistProvider *completionAssistProvider() const;
    virtual void setFunctionHintAssistProvider(CompletionAssistProvider *provider);
    virtual CompletionAssistProvider *functionHintAssistProvider() const;
    void setQuickFixAssistProvider(IAssistProvider *provider) const;
    virtual IAssistProvider *quickFixAssistProvider() const;

    void setCodeStyle(ICodeStylePreferences *preferences);
    ICodeStylePreferences *codeStyle() const;
    void setTabSettings(const TextEditor::TabSettingsData &tabSettings);
    void setFontSettings(const TextEditor::FontSettingsData &fontSettings);

    void setFoldingIndentExternallyProvided(bool ext);
    bool isFoldingIndentExternallyProvided() const;

#ifdef WITH_TESTS
    void setSilentReload();
#endif

signals:
    void aboutToOpen(const Utils::FilePath &filePath, const Utils::FilePath &realFilePath);
    void openFinishedSuccessfully();
    void contentsChangedWithPosition(int position, int charsRemoved, int charsAdded);
    void tabSettingsChanged();
    void fontSettingsChanged();
    void extraSelectionsChanged();
    void markRemoved(TextEditor::TextMark *mark);

protected:
    virtual void applyFontSettings();
    Utils::Result<> saveImpl(const Utils::FilePath &filePath, SaveOption option) override;
    virtual void slotCodeStyleSettingsChanged(); // Used in CppEditorDocumet
    virtual void removeTrailingWhitespace(const QTextBlock &block);

private:
    Utils::Result<> openImpl(const Utils::FilePath &filePath,
                             const Utils::FilePath &realFileName,
                             bool reload);
    void cleanWhitespace(QTextCursor &cursor, bool inEntireDocument, bool cleanIndentation);
    void ensureFinalNewLine(QTextCursor &cursor);
    void modificationChanged(bool modified);

    TextDocumentPrivate *d;
};

using TextDocumentPtr = QSharedPointer<TextDocument>;

} // namespace TextEditor
