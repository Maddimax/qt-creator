// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "texteditor_global.h"
#include "codeassist/assistenums.h"
#include "formatter.h"
#include "indenter.h"
#include "refactoroverlay.h"

#include <coreplugin/textdocument.h>

#include <utils/id.h>
#include <utils/multitextcursor.h>

#include <QList>
#include <QMap>
#include <QTextCursor>
#include <QTextFormat>
#include <QSharedPointer>

#include <functional>
#include <memory>

QT_BEGIN_NAMESPACE
class QAction;
class QKeyEvent;
class QTextCursor;
class QTextDocument;
QT_END_NAMESPACE

namespace Core { class IEditor; }

namespace TextEditor {

class AssistInterface;
class CompletionAssistProvider;
class ExtraEncodingSettingsData;
class FontSettingsData;
class IAssistProvider;
class StorageSettingsData;
class SyntaxHighlighter;
class TabSettingsData;
class TextDocumentPrivate;
class TextMark;
class ToolBarChoice;
class TextSuggestion;
class TypingSettingsData;

using TextMarks = QList<TextMark *>;

class TEXTEDITOR_EXPORT TextDocument : public Core::BaseTextDocument,
                                       public QEnableSharedFromThis<TextDocument>
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

    // Something offered at a place in the file - a quick fix waiting, a
    // toolbar to open. A fact about the document like the selections above,
    // so that a view which is not a TextEditorWidget can offer them too, and
    // keyed by producer the same way.
    void setRefactorMarkers(Utils::Id type, const RefactorMarkers &markers);
    RefactorMarkers refactorMarkers(Utils::Id type) const;
    // Every one there is, whoever put it there.
    RefactorMarkers refactorMarkers() const;
    // The one whose cursor covers \a position, or an invalid one.
    RefactorMarker refactorMarkerAt(int position) const;

    // The tooltip carried by an annotation covering pos - a diagnostic, say.
    // Every kind that carries one is document-wide, so no view is needed to
    // ask.
    QString extraSelectionTooltip(int pos) const;

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

    // Fold the comment a file opens with - the licence header - leaving a
    // documentation comment alone, since that one is about the code rather
    // than about the file. Does nothing if the first thing in the file is not
    // a foldable comment.
    void foldLicenseHeader();

    // Run \a f once the highlighter has caught up, or answer false if it
    // already has and the caller should just call it. What a file's comment
    // markers are comes from its language, and the language is not known until
    // the highlighter has been set up.
    bool singleShotAfterHighlightingDone(std::function<void()> &&f);

    // Fold or unfold \a block. Folding is state on the block itself and on
    // this document's layout, so both views showing the file follow - which is
    // why this is here rather than on whichever one happens to be open. A view
    // adds only what it does with its own caret afterwards.
    //
    // Deferred until the file has been highlighted where it has not been:
    // where a block can be folded to is worked out while highlighting, so
    // folding before that has nothing to go on.
    void foldBlock(const QTextBlock &block, bool recursive = false);
    void unfoldBlock(const QTextBlock &block, bool recursive = false);
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

    // What the provider above is asked with. A language whose assist needs
    // more than a cursor and a path - C++ quick fixes want the semantic info -
    // answers here rather than on an editor, so that any view showing this
    // document offers the same assist.
    //
    // \a editor is the view that asked, which a proposal may act on once it
    // is accepted: a fix that renames what it just wrote moves a caret, and a
    // caret belongs to a view. Null where nobody in particular asked.
    virtual std::unique_ptr<AssistInterface> createAssistInterface(
        const QTextCursor &cursor, AssistKind kind, AssistReason reason,
        Core::IEditor *editor = nullptr) const;

    // What this language does with \a event before the view acts on it, with
    // the caret at \a cursor. Answers whether it did anything, in which case
    // the view leaves the key alone.
    //
    // The one thing a language does in the middle of ordinary typing: Enter
    // inside a doxygen comment writes the block. A command or an assist has
    // somewhere to be registered; this does not, so it is here - and asking
    // the document is what makes both views type the same.
    //
    // Only asked where there is a single caret: what these do is edit around
    // one, and several would each want their own answer.
    virtual bool handleKeyPress(QKeyEvent *event, const QTextCursor &cursor);

    // What this language wants in the toolbar row beside what the editor puts
    // there itself - the button that opens the preprocessor dialog for a C++
    // file, say. Actions rather than widgets, so that whichever view is
    // drawing decides how, the same way the context menu is described.
    virtual QList<QAction *> toolBarActions() const;
    // The choice the language offers in the toolbar, or nullptr where it
    // offers none. Owned by the document.
    virtual ToolBarChoice *toolBarChoice() const;

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
    void refactorMarkersChanged();
    // Which actions the toolbar should show has changed.
    void toolBarActionsChanged();
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

// The shared document behind an editor, whatever kind of view is showing it.
// Whoever wants to put the same text somewhere else needs the document, not
// the view: asking for a widget to reach it is what stops a view that has
// none from being usable. Null for an editor that holds no text document.
TEXTEDITOR_EXPORT TextDocumentPtr textDocumentPtr(Core::IEditor *editor);

} // namespace TextEditor
