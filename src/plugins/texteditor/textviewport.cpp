// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "textviewport.h"

#include "quicktexteditor.h"

#include "blockselection.h"
#include "highlighterhelper.h"
#include "symbolrequests.h"
#include "textoperations.h"
#include "textsuggestion.h"

#include <utils/uncommentselection.h>

#include "autocompleter.h"
#include "behaviorsettings.h"
#include "circularclipboard.h"
#include "circularclipboardassist.h"
#include "codeassist/assistinterface.h"
#include "codeassist/assistproposaliteminterface.h"
#include "codeassist/assisttarget.h"
#include "codeassist/completionassistprovider.h"
#include "codeassist/genericproposalmodel.h"
#include "codeassist/iassistprocessor.h"
#include "codeassist/iassistproposal.h"
#include "codeassist/iassistproposalmodel.h"
#include "codeassist/ifunctionhintproposalmodel.h"
#include "codesource.h"
#include "completionsettings.h"
#include "displaysettings.h"
#include "fontsettings.h"
#include "tabsettings.h"
#include "syntaxhighlighter.h"
#include "texteditorconstants.h"
#include "texteditortr.h"
#include "textdocument.h"
#include "texteditor.h"
#include "gutterframe.h"
#include "hoverhandlerrunner.h"
#include "indenter.h"
#include "marginsettings.h"
#include "textdocumentlayout.h"
#include "textmark.h"
#include "typingsettings.h"

#include <coreplugin/dialogs/codecselector.h>
#include <coreplugin/editormanager/editormanager.h>
#include <coreplugin/idocument.h>
#include <coreplugin/messagemanager.h>

#include <qtcquick/qtciconprovider.h>

#include <utils/theme/theme.h>
#include <utils/tooltip/tooltip.h>

#include <QApplication>
#include <QDesktopServices>
#include <QMenu>
#include <QStyle>
#include <utils/utilsicons.h>

#include <QFontMetricsF>
#include <QQuickWindow>
#include <QSGRectangleNode>
#include <QSGTextNode>
#include <utils/camelcasecursor.h>
#include <utils/multitextcursor.h>
#include <utils/plaintextedit/plaintextedit.h>
#include <utils/stylehelper.h>
#include <utils/plaintextedit/texteditorlayout.h>

#include <QClipboard>
#include <QGuiApplication>
#include <QMimeData>
#include <QInputMethod>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextLayout>

namespace TextEditor {

// QTextCursor::selectedText() separates paragraphs with U+2029, which is right
// inside a document and wrong on a clipboard: pasted into anything else it is a
// stray character where a line break should be. Converted where the text leaves
// the document, so that nothing downstream has to know.
static QString selectedPlainText(const QTextCursor &cursor)
{
    return cursor.selectedText().replace(QChar::ParagraphSeparator, '\n');
}

TextViewport::Line::Line() = default;
TextViewport::Line::~Line() = default;
TextViewport::Line::Line(Line &&other) noexcept = default;
TextViewport::Line &TextViewport::Line::operator=(Line &&other) noexcept = default;

TextViewport::TextViewport(QQuickItem *parent)
    : QQuickItem(parent)
{
    setFlag(ItemHasContents);
    setFlag(ItemIsFocusScope);
    setFlag(ItemAcceptsInputMethod);
    setClip(true);

    // Hover tooltips. A widget gets QEvent::ToolTip from Qt after the mouse
    // rests; an item gets no such thing, so the resting is timed here.
    setAcceptHoverEvents(true);
    m_hoverRunner = std::make_unique<HoverHandlerRunner>(this, m_hoverHandlers);
    m_hoverTimer = new QTimer(this);
    m_hoverTimer->setSingleShot(true);
    // The user's own rest-before-tooltip delay, which is a style hint rather
    // than a setting; a widget gets it applied for it.
    m_hoverTimer->setInterval(
        QApplication::style()->styleHint(QStyle::SH_ToolTip_WakeUpDelay));
    connect(m_hoverTimer, &QTimer::timeout, this, &TextViewport::askForTooltip);

    m_autoCompleter = std::make_unique<AutoCompleter>();
    connect(&globalCompletionSettings(), &Utils::AspectContainer::changed, this, [this] {
        applyCompletionSettings();
    });
    applyCompletionSettings();

    // Preferences are *pushed* into a document rather than read from one: a
    // TextDocument on its own never hears about them, which is why the widget
    // editor does exactly this for the document it shows. Doing it here covers
    // the editor and every preview alike.
    connect(&globalFontSettings(), &FontSettings::changed, this, [this] {
        applyGlobalFontSettings();
    });
    // Turning bracket matching off has to take effect on what is already open,
    // rather than on whatever is opened next.
    connect(&displaySettings(), &Utils::AspectContainer::changed, this, [this] {
        // A scope lit up because the setting was on has to go out when it is
        // turned off, and the caret's scope has to light up when it goes on.
        if (displaySettings().highlightBlocks())
            setScopeBlock(cursorBlock().blockNumber());
        else
            clearScopeHighlight();
        emit fileFormatChanged();
        polish();
        update();
    });
    // The same for where the right margin sits, which is a page of its own.
    connect(&marginSettings(), &Utils::AspectContainer::changed, this, [this] {
        polish();
        update();
    });
}

void TextViewport::applyCompletionSettings()
{
    const CompletionSettings &settings = globalCompletionSettings();
    m_autoCompleter->setAutoInsertBracketsEnabled(settings.autoInsertBrackets());
    m_autoCompleter->setSurroundWithBracketsEnabled(settings.surroundingAutoBrackets());
    m_autoCompleter->setAutoInsertQuotesEnabled(settings.autoInsertQuotes());
    m_autoCompleter->setSurroundWithQuotesEnabled(settings.surroundingAutoQuotes());
    m_autoCompleter->setOverwriteClosingCharsEnabled(settings.overwriteClosingChars());
}

void TextViewport::applyGlobalFontSettings()
{
    if (TextDocument * const doc = m_document ? m_document->textDocument() : nullptr)
        doc->setFontSettings(globalFontSettings().data());
}

TextViewport::~TextViewport() = default;

CodeSource *TextViewport::document() const
{
    return m_document;
}

void TextViewport::setDocument(CodeSource *document)
{
    if (m_document == document)
        return;

    if (m_document)
        m_document->disconnect(this);

    m_document = document;

    if (m_document) {
        // A source hands out a different document when it reopens, and says so.
        connect(m_document, &CodeSource::textDocumentChanged,
                this, &TextViewport::documentChangedInternal);
        // And it can stay the same document but change what it is written in,
        // which the source is told after the view was bound to it.
        connect(m_document, &CodeSource::languageChanged,
                this, &TextViewport::adoptSourceCompleter);
    }

    documentChangedInternal();
    adoptSourceCompleter();
    emit documentChanged();
}

qreal TextViewport::scrollY() const
{
    return m_scrollY;
}

void TextViewport::setScrollY(qreal scrollY)
{
    // Held inside the document: scrolling past the end shows nothing, and a
    // negative offset shows nothing twice.
    const qreal clamped = qBound(0.0, scrollY, qMax(0.0, m_contentHeight - height()));
    if (qFuzzyCompare(m_scrollY + 1, clamped + 1))
        return;
    m_scrollY = clamped;
    polish();
    emit scrollYChanged();
}

qreal TextViewport::scrollX() const
{
    return m_scrollX;
}

void TextViewport::setScrollX(qreal scrollX)
{
    const qreal clamped = qBound(0.0, scrollX, qMax(0.0, m_contentWidth - width()));
    if (qFuzzyCompare(m_scrollX + 1, clamped + 1))
        return;
    m_scrollX = clamped;
    polish();
    emit scrollXChanged();
}

qreal TextViewport::contentHeight() const
{
    return m_contentHeight;
}

qreal TextViewport::contentWidth() const
{
    return m_contentWidth;
}

qreal TextViewport::lineHeight() const
{
    return m_lineHeight;
}

QColor TextViewport::backgroundColor() const
{
    return m_background;
}

int TextViewport::firstVisibleLine() const
{
    return m_firstVisibleLine;
}

QColor TextViewport::currentLineColor() const
{
    return m_currentLine;
}

QColor TextViewport::changedLineColor() const
{
    return revisionUnsavedColor();
}

QColor TextViewport::savedLineColor() const
{
    return revisionRevertedColor();
}

qreal TextViewport::indentWidth() const
{
    return m_indentWidth;
}

// Where the word under the caret starts. What counts as part of a word is the
// language's answer, not a general one: a '$' belongs to an identifier in some
// languages and not in others.
static int startOfWordBefore(const QTextCursor &cursor, const CompletionAssistProvider *provider)
{
    const QTextBlock block = cursor.block();
    const QString text = block.text();
    int at = cursor.position() - block.position();
    while (at > 0 && provider->isContinuationChar(text.at(at - 1)))
        --at;
    return block.position() + at;
}

void TextViewport::requestCompletions()
{
    TextDocument * const doc = m_document ? m_document->textDocument() : nullptr;
    CompletionAssistProvider * const provider = doc ? doc->completionAssistProvider() : nullptr;
    const QTextCursor cursor = textCursor();
    if (!provider || cursor.isNull()) {
        emit completionsAvailable({}, {});
        return;
    }

    // Whatever the last request is still doing, nobody is waiting for it now.
    if (m_completionProcessor)
        m_completionProcessor->cancel();

    std::unique_ptr<AssistInterface> interface
        = doc->createAssistInterface(cursor, Completion, ExplicitlyInvoked, m_editor);
    m_completionProcessor.reset(provider->createProcessor(interface.get()));
    if (!m_completionProcessor) {
        emit completionsAvailable({}, {});
        return;
    }

    m_completionProcessor->setAsyncCompletionAvailableHandler(
        [this](IAssistProposal *proposal) { deliverCompletions(proposal); });

    // A proposal now means the language answered on the spot; a null one means
    // it went away to think, and the handler above is what hears back.
    if (IAssistProposal * const proposal = m_completionProcessor->start(std::move(interface)))
        deliverCompletions(proposal);
}

void TextViewport::requestQuickFixes(IAssistProvider *asked)
{
    TextDocument * const doc = m_document ? m_document->textDocument() : nullptr;
    IAssistProvider * const provider = asked ? asked
                                             : (doc ? doc->quickFixAssistProvider() : nullptr);
    const QTextCursor cursor = textCursor();
    m_quickFixProposal.reset();
    if (!provider || cursor.isNull() || !canEdit()) {
        emit quickFixesAvailable({});
        return;
    }

    if (m_quickFixProcessor)
        m_quickFixProcessor->cancel();

    // Asked of the document: a C++ quick fix needs the semantic info, and
    // only the document can hand it over without being a particular view.
    std::unique_ptr<AssistInterface> interface
        = doc->createAssistInterface(cursor, QuickFix, ExplicitlyInvoked, m_editor);
    m_quickFixProcessor.reset(provider->createProcessor(interface.get()));
    if (!m_quickFixProcessor) {
        emit quickFixesAvailable({});
        return;
    }

    const auto deliver = [this](IAssistProposal *proposal) {
        m_quickFixProposal.reset(proposal);
        QStringList fixes;
        if (m_quickFixProposal) {
            if (const ProposalModelPtr model = m_quickFixProposal->model()) {
                fixes.reserve(model->size());
                for (int i = 0; i < model->size(); ++i)
                    fixes.append(model->text(i));
            }
        }
        emit quickFixesAvailable(fixes);
    };
    m_quickFixProcessor->setAsyncCompletionAvailableHandler(deliver);

    // A proposal now means the language answered on the spot; a null one means
    // it will answer through the handler above, or not at all.
    if (IAssistProposal * const immediate = m_quickFixProcessor->start(std::move(interface)))
        deliver(immediate);
}

void TextViewport::applyQuickFix(int index)
{
    if (!canEdit() || !m_quickFixProposal)
        return;
    const ProposalModelPtr model = m_quickFixProposal->model();
    auto * const generic = dynamic_cast<GenericProposalModel *>(model.data());
    if (!generic || index < 0 || index >= model->size())
        return;
    AssistProposalItemInterface * const item = generic->proposalItem(index);
    QTextDocument * const text = textCursor().isNull() ? nullptr : textCursor().document();
    if (!item || !text)
        return;

    // The item rewrites the document itself; all this supplies is somewhere
    // for it to do that, which is what DocumentAssistTarget is.
    DocumentAssistTarget target(text);
    target.setCursorPosition(cursorPosition());
    item->apply(target, m_quickFixProposal->basePosition());

    // Whatever was on offer was for the text as it stood.
    m_quickFixProposal.reset();
    emit quickFixesAvailable({});
    setCursorPosition(target.position());
}

void TextViewport::requestFunctionHint()
{
    TextDocument * const doc = m_document ? m_document->textDocument() : nullptr;
    CompletionAssistProvider * const provider = doc ? doc->functionHintAssistProvider() : nullptr;
    const QTextCursor cursor = textCursor();
    m_functionHintProposal.reset();
    if (!provider || cursor.isNull()) {
        emit functionHintAvailable({}, -1);
        return;
    }

    if (m_functionHintProcessor)
        m_functionHintProcessor->cancel();

    std::unique_ptr<AssistInterface> interface
        = doc->createAssistInterface(cursor, FunctionHint, ExplicitlyInvoked, m_editor);
    m_functionHintProcessor.reset(provider->createProcessor(interface.get()));
    if (!m_functionHintProcessor) {
        emit functionHintAvailable({}, -1);
        return;
    }

    const auto deliver = [this](IAssistProposal *proposal) {
        m_functionHintProposal.reset(proposal);
        updateFunctionHint();
    };
    m_functionHintProcessor->setAsyncCompletionAvailableHandler(deliver);

    if (IAssistProposal * const immediate = m_functionHintProcessor->start(std::move(interface)))
        deliver(immediate);
}

void TextViewport::updateFunctionHint()
{
    if (!m_functionHintProposal) {
        emit functionHintAvailable({}, -1);
        return;
    }

    const auto model = m_functionHintProposal->model()
                           .dynamicCast<IFunctionHintProposalModel>();
    const QTextCursor cursor = textCursor();
    if (!model || cursor.isNull()) {
        m_functionHintProposal.reset();
        emit functionHintAvailable({}, -1);
        return;
    }

    // What has been typed since the call started is what says which argument
    // the caret is in - and -1 means the call is over and the hint with it.
    const int base = m_functionHintProposal->basePosition();
    const int position = cursor.position();
    const QString prefix = position > base
                               ? cursor.document()->toPlainText().mid(base, position - base)
                               : QString();
    const int active = model->activeArgument(prefix);
    if (active < 0) {
        m_functionHintProposal.reset();
        emit functionHintAvailable({}, -1);
        return;
    }

    QStringList signatures;
    signatures.reserve(model->size());
    for (int i = 0; i < model->size(); ++i)
        signatures.append(model->text(i));
    emit functionHintAvailable(signatures, active);
}

void TextViewport::deliverCompletions(IAssistProposal *proposal)
{
    m_completionProposal.reset(proposal);
    QStringList candidates;
    if (m_completionProposal) {
        if (const ProposalModelPtr model = m_completionProposal->model()) {
            candidates.reserve(model->size());
            for (int i = 0; i < model->size(); ++i)
                candidates.append(model->text(i));
        }
    }
    emit completionsAvailable(candidates, completionPrefix());
}

QString TextViewport::completionPrefix() const
{
    TextDocument * const doc = m_document ? m_document->textDocument() : nullptr;
    CompletionAssistProvider * const provider = doc ? doc->completionAssistProvider() : nullptr;
    const QTextCursor cursor = textCursor();
    if (!provider || cursor.isNull())
        return {};

    const int from = startOfWordBefore(cursor, provider);
    return cursor.document()->toPlainText().mid(from, cursor.position() - from);
}

void TextViewport::applyCompletion(const QString &completion)
{
    TextDocument * const doc = m_document ? m_document->textDocument() : nullptr;
    CompletionAssistProvider * const provider = doc ? doc->completionAssistProvider() : nullptr;
    QTextCursor cursor = textCursor();
    if (!provider || cursor.isNull() || completion.isEmpty() || m_readOnly)
        return;

    // The item first, where the offer it came from is still to hand: an item
    // may insert a snippet or rewrite what is around it, and only it knows.
    // Its own text is what the form shows, so that is what identifies it.
    if (AssistProposalItemInterface * const item = completionItemFor(completion)) {
        DocumentAssistTarget target(cursor.document());
        target.setCursorPosition(cursor.position());
        item->apply(target, m_completionProposal->basePosition());
        m_completionProposal.reset();
        setCursorPosition(target.position());
        return;
    }

    // Nothing to ask - the words came from somewhere with no proposal behind
    // them. What was typed is replaced rather than added to, or choosing
    // "beta" for a half-typed "be" would leave "bebeta".
    cursor.setPosition(startOfWordBefore(cursor, provider));
    cursor.setPosition(textCursor().position(), QTextCursor::KeepAnchor);
    cursor.insertText(completion);
    setTextCursor(cursor);
}

AssistProposalItemInterface *TextViewport::completionItemFor(const QString &text) const
{
    if (!m_completionProposal)
        return nullptr;
    const auto model = m_completionProposal->model();
    auto * const generic = dynamic_cast<GenericProposalModel *>(model.data());
    if (!generic)
        return nullptr;
    for (int i = 0; i < generic->size(); ++i) {
        if (generic->text(i) == text)
            return generic->proposalItem(i);
    }
    return nullptr;
}

void TextViewport::setAutoCompleter(AutoCompleter *completer)
{
    if (!completer)
        return;
    m_autoCompleter.reset(completer);
    applyCompletionSettings();
    if (TextDocument * const doc = m_document ? m_document->textDocument() : nullptr)
        m_autoCompleter->setTabSettings(doc->tabSettings());
}

AutoCompleter *TextViewport::autoCompleter() const
{
    return m_autoCompleter.get();
}

void TextViewport::adoptSourceCompleter()
{
    if (AutoCompleter * const offered = m_document ? m_document->createAutoCompleter() : nullptr) {
        setAutoCompleter(offered);
        m_autoCompleterFromSource = true;
    } else if (m_autoCompleterFromSource) {
        setAutoCompleter(new AutoCompleter);
        m_autoCompleterFromSource = false;
    }
}

bool TextViewport::isMouseHidden() const
{
    return m_mouseHidden;
}

void TextViewport::showMouse()
{
    if (!m_mouseHidden)
        return;
    m_mouseHidden = false;
    emit mouseHiddenChanged();
}

qreal TextViewport::marginX() const
{
    return m_marginX;
}

QColor TextViewport::marginLineColor() const
{
    return m_marginLine;
}

QColor TextViewport::marginAreaColor() const
{
    return m_marginArea;
}

bool TextViewport::tintMarginArea() const
{
    return m_tintMarginArea;
}

QColor TextViewport::indentGuideColor() const
{
    return m_indentGuide;
}

QFont TextViewport::font() const
{
    return m_font;
}

int TextViewport::lineCount() const
{
    return m_lineCount;
}

// The two fold markers. Utils::Icon::icon() masks and tints on every call and
// these are wanted per foldable line per relayout, so they are made once -
// which is also why a theme change needs the restart Creator already asks for.
static QString foldMarkerUrl(bool folded)
{
    static const QString expand = QtcQuick::iconUrl(Utils::Icons::EXPAND.icon());
    static const QString collapse = QtcQuick::iconUrl(Utils::Icons::COLLAPSE.icon());
    return folded ? expand : collapse;
}

int TextViewport::visibleLineCount() const
{
    return int(m_lines.size());
}

int TextViewport::visibleColumnCount() const
{
    const qreal charWidth = QFontMetricsF(m_font).horizontalAdvance(QLatin1Char(' '));
    if (charWidth <= 0)
        return 0;
    return int(width() / charWidth);
}

int TextViewport::selectionStart() const
{
    return m_selectionStart;
}

void TextViewport::setSelectionStart(int position)
{
    if (m_selectionStart == position)
        return;
    m_selectionStart = position;
    polish();
    emit selectionChanged();
}

int TextViewport::selectionEnd() const
{
    return m_selectionEnd;
}

void TextViewport::setSelectionEnd(int position)
{
    if (m_selectionEnd == position)
        return;
    m_selectionEnd = position;
    polish();
    emit selectionChanged();
}

// What a row on screen says to the form drawing it. Only what QML reads: the
// text itself, the formats over it and the selection on it are drawn by
// updatePaintNode() straight from the Line, and never travel through here.
struct RowView
{
    qreal y = 0;
    qreal width = 0;
    int lineNumber = 0;
    bool firstRowOfLine = true;
    int changed = 0;
    int indentGuides = 0;
    QString markIcon;
    QString foldIcon;
    QString foldReplacement;
    QString annotation;
    qreal annotationX = 0;
    qreal annotationY = 0;
    QString breakMarker;
    qreal breakMarkerX = 0;
    QVariantList whitespace;

    bool operator==(const RowView &other) const = default;
};

// The rows as something a repeater can keep its delegates for, with a role per
// value. One map per row meant building the whole row whenever a delegate
// wanted any of it, and building the keys cost more than the values did; a
// role is read when a binding asks for it and notified on its own.
class VisibleRowsModel final : public QAbstractListModel
{
public:
    using QAbstractListModel::QAbstractListModel;

    enum Role {
        YRole = Qt::UserRole,
        WidthRole,
        LineNumberRole,
        DisplayNumberRole,
        FirstRowOfLineRole,
        ChangedRole,
        IndentGuidesRole,
        MarkIconRole,
        FoldIconRole,
        FoldReplacementRole,
        AnnotationRole,
        AnnotationXRole,
        AnnotationYRole,
        BreakMarkerRole,
        BreakMarkerXRole,
        WhitespaceRole,
    };

    QHash<int, QByteArray> roleNames() const override
    {
        // Read as model.<name> rather than as a property of the delegate: a
        // role called "y" or "width" would otherwise shadow the Item's own.
        return {{YRole, "y"},
                {WidthRole, "width"},
                {LineNumberRole, "lineNumber"},
                {DisplayNumberRole, "displayNumber"},
                {FirstRowOfLineRole, "firstRowOfLine"},
                {ChangedRole, "changed"},
                {IndentGuidesRole, "indentGuides"},
                {MarkIconRole, "markIcon"},
                {FoldIconRole, "foldIcon"},
                {FoldReplacementRole, "foldReplacement"},
                {AnnotationRole, "annotation"},
                {AnnotationXRole, "annotationX"},
                {AnnotationYRole, "annotationY"},
                {BreakMarkerRole, "breakMarker"},
                {BreakMarkerXRole, "breakMarkerX"},
                {WhitespaceRole, "whitespace"}};
    }

    int rowCount(const QModelIndex &parent = {}) const override
    {
        return parent.isValid() ? 0 : int(m_rows.size());
    }

    QVariant data(const QModelIndex &at, int role) const override
    {
        if (!at.isValid() || at.row() < 0 || at.row() >= m_rows.size())
            return {};
        const RowView &row = m_rows.at(at.row());
        switch (role) {
        case YRole: return row.y;
        case WidthRole: return row.width;
        case LineNumberRole: return row.lineNumber;
        case DisplayNumberRole:
            // What the gutter prints. The same as the line number, except
            // while numbering relative to a line - vim's "relativenumber" -
            // where every other line says how far away it is and the line
            // itself keeps its own number.
            if (m_relativeOrigin <= 0 || row.lineNumber == m_relativeOrigin)
                return row.lineNumber;
            return qAbs(row.lineNumber - m_relativeOrigin);
        case FirstRowOfLineRole: return row.firstRowOfLine;
        case ChangedRole: return row.changed;
        case IndentGuidesRole: return row.indentGuides;
        case MarkIconRole: return row.markIcon;
        case FoldIconRole: return row.foldIcon;
        case FoldReplacementRole: return row.foldReplacement;
        case AnnotationRole: return row.annotation;
        case AnnotationXRole: return row.annotationX;
        case AnnotationYRole: return row.annotationY;
        case BreakMarkerRole: return row.breakMarker;
        case BreakMarkerXRole: return row.breakMarkerX;
        case WhitespaceRole: return row.whitespace;
        }
        return {};
    }

    // The line the numbers are counted from, or 0 for none. Kept here rather
    // than in each row so that moving the caret is one signal and not a
    // rebuild of every row.
    void setRelativeOrigin(int line)
    {
        if (m_relativeOrigin == line)
            return;
        m_relativeOrigin = line;
        if (!m_rows.empty()) {
            emit dataChanged(index(0), index(int(m_rows.size()) - 1),
                             {DisplayNumberRole});
        }
    }

    void setRows(QList<RowView> rows)
    {
        // Scrolling down moves what was row n to row n-shift, and the rows
        // that stayed say exactly what they said before. Telling the view
        // that they changed makes it read all of them again, so look for the
        // shift and report it as rows leaving the top instead.
        if (const int shift = shiftOf(rows); shift > 0) {
            beginRemoveRows({}, 0, shift - 1);
            const int kept = int(m_rows.size()) - shift;
            m_rows.remove(0, shift);
            endRemoveRows();

            if (const int arriving = int(rows.size()) - kept; arriving > 0) {
                beginInsertRows({}, kept, kept + arriving - 1);
                for (int i = kept; i < int(rows.size()); ++i)
                    m_rows.append(rows.at(i));
                endInsertRows();
            }
            return;
        }

        const int before = int(m_rows.size());
        const int now = int(rows.size());

        // Which rows actually say something new, in runs, worked out before
        // the old ones are gone. Typing changes the row it was typed on and
        // leaves the rest alone; announcing the lot makes every delegate on
        // screen evaluate every binding it has for nothing.
        QList<QPair<int, int>> changedRuns;
        for (int i = 0, shared = qMin(before, now); i < shared;) {
            if (m_rows.at(i) == rows.at(i)) {
                ++i;
                continue;
            }
            const int from = i;
            while (i < shared && !(m_rows.at(i) == rows.at(i)))
                ++i;
            changedRuns.append({from, i - 1});
        }

        if (now > before)
            beginInsertRows({}, before, now - 1);
        else if (now < before)
            beginRemoveRows({}, now, before - 1);

        m_rows = std::move(rows);

        if (now > before)
            endInsertRows();
        else if (now < before)
            endRemoveRows();

        for (const auto &[from, to] : std::as_const(changedRuns))
            emit dataChanged(index(from), index(to));
    }

private:
    // How far the rows moved up, if that is all that happened. Answered by
    // comparing them rather than by working it out from where the view has
    // scrolled to: a row that compares equal says the same thing, whatever
    // the reason, and one that does not must be re-read whatever the scroll
    // says.
    int shiftOf(const QList<RowView> &rows) const
    {
        if (m_rows.isEmpty() || rows.isEmpty())
            return 0;

        int shift = 1;
        for (; shift < int(m_rows.size()); ++shift) {
            if (m_rows.at(shift) == rows.at(0))
                break;
        }
        if (shift >= int(m_rows.size()))
            return 0;

        // The whole overlap has to match, not just the row it starts at.
        const int overlap = qMin(int(m_rows.size()) - shift, int(rows.size()));
        for (int i = 0; i < overlap; ++i) {
            if (!(m_rows.at(shift + i) == rows.at(i)))
                return 0;
        }
        return shift;
    }

    QList<RowView> m_rows;
    int m_relativeOrigin = 0;
};

QAbstractItemModel *TextViewport::visibleRows() const
{
    if (!m_visibleRows)
        const_cast<TextViewport *>(this)->m_visibleRows = new VisibleRowsModel(
            const_cast<TextViewport *>(this));
    return m_visibleRows;
}

void TextViewport::rebuildVisibleLines()
{
    QList<RowView> rows;
    rows.reserve(int(m_lines.size()));
    for (const Line &line : m_lines) {
        rows.append(RowView{// Where the row sits in the document, before the
                            // scroll is taken off it. Not row * lineHeight:
                            // anything that claims space between rows moves
                            // this and not that.
                            line.at.y() + m_scrollY,
                            line.layout->lineAt(0).naturalTextWidth(),
                            line.lineNumber,
                            line.firstRowOfLine,
                            int(line.changed),
                            line.indentGuides,
                            line.markIcon,
                            line.foldIcon,
                            line.foldReplacement,
                            line.annotation,
                            line.annotationX,
                            // Not the row's own y when the message was asked
                            // for on a line of its own.
                            line.annotationY,
                            line.breakMarker,
                            line.breakMarkerX,
                            line.whitespace});
    }
    static_cast<VisibleRowsModel *>(visibleRows())->setRows(std::move(rows));

    // The markers are placed in item coordinates, so every relayout and every
    // scroll moves them even when the document says nothing has changed.
    emit refactorMarkersChanged();
}

QVariantMap TextViewport::visibleLine(int index) const
{
    if (index < 0 || index >= int(m_lines.size()))
        return {};

    const Line &line = m_lines.at(index);
    QVariantList formats;
    for (const QTextLayout::FormatRange &range : line.layout->formats()) {
        formats.append(QVariantMap{{QStringLiteral("start"), range.start},
                                   {QStringLiteral("length"), range.length},
                                   // Syntax highlighting is mostly a foreground
                                   // colour, so a test that reads only the
                                   // background cannot tell coloured text from
                                   // text drawn in one colour.
                                   {QStringLiteral("foreground"), range.format.foreground().color()},
                                   {QStringLiteral("background"), range.format.background().color()},
                                   // A link is drawn by underlining it, which
                                   // neither colour shows.
                                   {QStringLiteral("underline"), range.format.fontUnderline()},
                                   // Set-but-empty is what a black cell looks
                                   // like before it is drawn, and the colour
                                   // above cannot tell it from unset.
                                   {QStringLiteral("hasBackground"),
                                    range.format.hasProperty(QTextFormat::BackgroundBrush)}});
    }
    return QVariantMap{{QStringLiteral("text"), line.layout->text()},
                       {QStringLiteral("formats"), formats},
                       // What the gutter numbers this line, counting folded
                       // lines that are not on screen.
                       {QStringLiteral("lineNumber"), line.lineNumber},
                       // Whether this row starts its line. A wrapped line is
                       // numbered and marked only where it begins.
                       {QStringLiteral("firstRowOfLine"), line.firstRowOfLine},
                       // What the gutter draws beside this line, if anything.
                       {QStringLiteral("markIcon"), line.markIcon},
                       // Whether a fold marker belongs beside this line, and
                       // which way round it points.
                       {QStringLiteral("foldable"), line.foldable},
                       {QStringLiteral("folded"), line.folded},
                       {QStringLiteral("foldIcon"), line.foldIcon},
                       {QStringLiteral("foldReplacement"), line.foldReplacement},
                       // How many indent guides to draw on this row.
                       {QStringLiteral("indentGuides"), line.indentGuides},
                       // Whether the line differs from what is on disk, and
                       // which way. See TextViewport::ChangeMark.
                       {QStringLiteral("changed"), int(line.changed)},
                       {QStringLiteral("annotation"), line.annotation},
                       {QStringLiteral("newlineTail"), line.newlineTail},
                       // The selected part of this row, as one rectangle.
                       {QStringLiteral("selectionFills"), QVariant::fromValue(line.selectionFills)},
                       // The spaces and tabs on this row, where they are shown.
                       {QStringLiteral("whitespace"), line.whitespace},
                       // The wrapped-line marker, where this row has one.
                       {QStringLiteral("breakMarker"), line.breakMarker},
                       {QStringLiteral("breakMarkerX"), line.breakMarkerX},
                       // The nested-scope backgrounds behind this row.
                       {QStringLiteral("scopeBands"), line.scopeBands},
                       // Where the line's message starts, which the display
                       // settings decide.
                       {QStringLiteral("annotationX"), line.annotationX},
                       // And how far down, which is not the row's own y when
                       // the message was asked for on a line of its own.
                       {QStringLiteral("annotationY"), line.annotationY},
                       // What is being composed on this line, if anything. Not
                       // part of "text": it is not in the document yet, which
                       // is the whole distinction.
                       {QStringLiteral("preedit"), line.layout->preeditAreaText()},
                       {QStringLiteral("width"), line.layout->lineAt(0).naturalTextWidth()},
                       // Where the row sits in the document, before the scroll
                       // is taken off it. Not row * lineHeight: anything that
                       // claims space between rows moves this and not that.
                       {QStringLiteral("y"), line.at.y() + m_scrollY}};
}

TextViewport::Located TextViewport::locate(int position) const
{
    for (int i = 0; i < int(m_lines.size()); ++i) {
        const Line &line = m_lines.at(i);
        // blockLength counts the newline, and the caret is allowed to sit on
        // it: that is the position at the end of the line.
        if (position >= line.blockPosition && position < line.blockPosition + line.blockLength)
            return {i, position - line.blockPosition};
    }
    return {};
}

QRectF TextViewport::rectangleAt(int position) const
{
    const Located found = locate(position);
    if (found.index < 0)
        return {};

    const Line &line = m_lines.at(found.index);
    const QTextLine textLine = line.layout->lineAt(0);
    if (!textLine.isValid())
        return {};

    int offset = found.offsetInLine;
    const qreal x = textLine.cursorToX(&offset);
    qreal width = 1;
    if (m_overwriteMode) {
        // As wide as the character it stands on, and a space's worth at the
        // end of a line where there is no character to stand on. The same two
        // cases WidgetTextControl draws for the widget editor.
        int next = offset + 1;
        width = next <= textLine.textStart() + textLine.textLength()
                    ? textLine.cursorToX(&next) - x
                    : QFontMetricsF(m_font).horizontalAdvance(QLatin1Char(' '));
        if (width <= 0)
            width = QFontMetricsF(m_font).horizontalAdvance(QLatin1Char(' '));
    }
    return QRectF(line.at.x() + x, line.at.y(), width, m_lineHeight);
}

bool TextViewport::isWrapping() const
{
    return m_wrapping;
}

bool TextViewport::visualizesWhitespace() const
{
    return m_visualizeWhitespace.value_or(displaySettings().visualizeWhitespace());
}

void TextViewport::setVisualizeWhitespace(bool on)
{
    if (m_visualizeWhitespace == on)
        return;
    m_visualizeWhitespace = on;
    polish();
    emit visualizeWhitespaceChanged();
}

void TextViewport::increaseFontZoom()
{
    globalFontSettings().increaseFontZoom();
}

void TextViewport::decreaseFontZoom()
{
    globalFontSettings().decreaseFontZoom();
}

void TextViewport::resetFontZoom()
{
    globalFontSettings().resetFontZoom();
}

void TextViewport::setWrapping(bool wrapping)
{
    if (m_wrapping == wrapping)
        return;
    m_wrapping = wrapping;
    // What there is to scroll through sideways is a different question now.
    m_contentWidth = 0;
    // And a wrapping view works out its own rows, so it has none to give a
    // suggestion: whatever it was showing of one goes.
    rebuildSuggestionGhosts();
    polish();
    update();
    emit wrappingChanged();
}

// PlainTextDocumentLayout::setTextWidth() is protected and PlainTextEdit
// reaches it as a friend, so anything else has to come at it this way.
class ViewportLayout : public Utils::TextEditorLayout
{
public:
    using Utils::TextEditorLayout::TextEditorLayout;
    using Utils::TextEditorLayout::setTextWidth;
};

Utils::TextEditorLayout *TextViewport::editorLayout()
{
    TextDocument * const doc = m_document ? m_document->textDocument() : nullptr;
    QTextDocument * const text = doc ? doc->document() : nullptr;
    if (!text)
        return nullptr;

    auto * const documentLayout
        = qobject_cast<Utils::PlainTextDocumentLayout *>(text->documentLayout());
    if (!documentLayout)
        return nullptr;

    if (!m_editorLayout || m_editorLayout->document() != text) {
        delete m_editorLayout;
        m_editorLayout = new ViewportLayout(documentLayout);
        // A layout that was just made has been told no width at all, and has
        // laid nothing out.
        m_documentTextWidth = -1;
        m_primedGeneration = -1;
        // Owned by this view. TextEditorLayout takes the document as its
        // QObject parent, which would leave one behind per viewport that ever
        // showed the file.
        m_editorLayout->setParent(this);
    }
    return m_editorLayout;
}

bool TextViewport::isReadOnly() const
{
    return m_readOnly;
}

void TextViewport::setReadOnly(bool readOnly)
{
    if (m_readOnly == readOnly)
        return;
    m_readOnly = readOnly;
    // ImEnabled is answered from this, and the platform caches the answer until
    // it is told to ask again.
    if (QGuiApplication::inputMethod())
        QGuiApplication::inputMethod()->update(Qt::ImEnabled);
    emit readOnlyChanged();
}

TextDocument *TextViewport::textDocument() const
{
    return m_document ? m_document->textDocument() : nullptr;
}

void TextViewport::setContextHelpItem(const Core::HelpItem &item)
{
    m_contextHelpItem = item;
}

Core::HelpItem TextViewport::contextHelpItem() const
{
    return m_contextHelpItem;
}

QWidget *TextViewport::tooltipParent()
{
    return m_tooltipHost;
}

void TextViewport::setTooltipHost(QWidget *host)
{
    m_tooltipHost = host;
}

QPoint TextViewport::globalCursorTopLeft() const
{
    return mapToGlobal(cursorRectangle().topLeft()).toPoint();
}

void TextViewport::setHoverHandlers(const QList<BaseHoverHandler *> &handlers)
{
    m_hoverHandlers = handlers;
}

void TextViewport::followSymbolUnderCursor(bool inNextSplit)
{
    followSymbolAt(cursorPosition(), inNextSplit);
}

// Backspace inside a line's leading whitespace, where the typing settings say
// it should do more than take one character back. Answers whether it did.
bool TextViewport::handleSmartBackspace(QTextCursor &cursor)
{
    TextDocument * const doc = textDocument();
    if (!doc)
        return false;

    const TypingSettingsData typing = doc->typingSettings();
    if (typing.m_smartBackspaceBehavior == TypingSettingsData::BackspaceNeverIndents)
        return false;

    // Outside the indentation there is nothing to unindent, so both of the
    // other behaviours take one character like the plain one.
    const QTextBlock block = cursor.block();
    const QString blockText = block.text();
    if (cursor.positionInBlock() == 0
        || cursor.positionInBlock() > TabSettingsData::firstNonSpace(blockText)) {
        return false;
    }

    if (typing.m_smartBackspaceBehavior == TypingSettingsData::BackspaceUnindents) {
        cursor = doc->unindent(Utils::MultiTextCursor({cursor})).mainCursor();
        return true;
    }

    // BackspaceFollowsPreviousIndents: back to the indentation of the nearest
    // line above that is indented less than this one.
    const TabSettingsData tabSettings = doc->tabSettings();
    const int indent = tabSettings.columnAt(blockText, cursor.positionInBlock());
    for (QTextBlock previous = block.previous(); previous.isValid();
         previous = previous.previous()) {
        const QString previousText = previous.text();
        if (previousText.trimmed().isEmpty())
            continue;
        const int previousIndent
            = tabSettings.columnAt(previousText, TabSettingsData::firstNonSpace(previousText));
        if (previousIndent >= indent)
            continue;
        cursor.beginEditBlock();
        cursor.setPosition(block.position(), QTextCursor::KeepAnchor);
        cursor.insertText(TabSettingsData::indentationString(previousText));
        cursor.endEditBlock();
        return true;
    }
    return false;
}

bool TextViewport::opensInNextSplit(bool asked) const
{
    return asked != displaySettings().openLinksInNextSplit();
}

bool TextViewport::isMouseNavigation(Qt::KeyboardModifiers modifiers) const
{
    return globalBehaviorSettings().mouseNavigation()
           && modifiers.testFlag(Qt::ControlModifier)
           && !modifiers.testFlag(Qt::ShiftModifier);
}

void TextViewport::showLink(const Utils::Link &link)
{
    if (m_currentLink == link)
        return;
    m_currentLink = link;
    setCursor(Qt::PointingHandCursor);
    polish();
    update();
}

void TextViewport::clearLink()
{
    if (!m_currentLink.hasValidLinkText())
        return;
    m_currentLink = Utils::Link();
    unsetCursor();
    polish();
    update();
}

void TextViewport::updateLink(const QPointF &pos, Qt::KeyboardModifiers modifiers)
{
    if (!isMouseNavigation(modifiers)) {
        clearLink();
        return;
    }
    TextDocument * const doc = textDocument();
    if (!doc)
        return;
    const TextEditorFactory::LinkFinder finder = TextEditorFactory::linkFinderFor(doc);
    if (!finder)
        return;
    const int position = positionAt(pos.x(), pos.y());
    if (position < 0) {
        clearLink();
        return;
    }
    // Still inside what is already underlined: the answer cannot have changed,
    // and asking again would be one lookup per mouse move.
    if (m_currentLink.hasValidLinkText() && position >= m_currentLink.linkTextStart
        && position < m_currentLink.linkTextEnd) {
        return;
    }

    QTextCursor cursor(doc->document());
    cursor.setPosition(position);
    finder(doc, cursor,
           [self = QPointer<TextViewport>(this)](const Utils::Link &link) {
               if (!self)
                   return;
               if (link.hasValidLinkText())
                   self->showLink(link);
               else
                   self->clearLink();
           },
           /*resolveTarget=*/false, /*inNextSplit=*/false);
}

bool TextViewport::followSymbolAt(int position, bool inNextSplit)
{
    TextDocument * const doc = textDocument();
    if (!doc || position < 0)
        return false;

    const TextEditorFactory::LinkFinder finder = TextEditorFactory::linkFinderFor(doc);
    if (!finder)
        return false;

    QTextCursor cursor(doc->document());
    cursor.setPosition(position);
    finder(doc, cursor,
           [self = QPointer<TextViewport>(this), inNextSplit](const Utils::Link &link) {
               if (self)
                   self->openLink(link, inNextSplit);
           },
           true, inNextSplit);
    return true;
}

bool TextViewport::openLink(const Utils::Link &link, bool inNextSplit)
{
    TextDocument * const doc = textDocument();
    if (!doc)
        return false;

    const QString url = link.targetFilePath.toUrlishString();
    if (url.startsWith(u"https://") || url.startsWith(u"http://")) {
        QDesktopServices::openUrl(url);
        return true;
    }

    // Within this very file it is a jump, not an open: the editor manager
    // would hand back the editor this already is.
    if (!inNextSplit && doc->filePath() == link.targetFilePath) {
        gotoLine(link.target.line, link.target.column);
        forceActiveFocus();
        return true;
    }
    if (!link.hasValidTarget())
        return false;

    Core::EditorManager::OpenEditorFlags flags;
    if (inNextSplit)
        flags |= Core::EditorManager::OpenInOtherSplit;
    return Core::EditorManager::openEditorAt(link, Utils::Id(), flags);
}

void TextViewport::hoverMoveEvent(QHoverEvent *event)
{
    QQuickItem::hoverMoveEvent(event);

    // Kept whether or not anything wants a tooltip: a Control press asks for
    // the link under wherever the mouse last was.
    m_hovering = true;
    m_hoverItemPos = event->position();
    updateLink(m_hoverItemPos, event->modifiers());

    if (m_hoverHandlers.isEmpty() || !m_tooltipHost)
        return;

    // Moving cancels whatever was about to be shown: what the tooltip would
    // have been about is no longer under the mouse.
    Utils::ToolTip::hide();

    m_hoverTimer->start();
}

void TextViewport::hoverLeaveEvent(QHoverEvent *event)
{
    QQuickItem::hoverLeaveEvent(event);
    m_hovering = false;
    clearLink();
    m_hoverTimer->stop();
    Utils::ToolTip::hide();
}

void TextViewport::keyReleaseEvent(QKeyEvent *event)
{
    // Letting go of Control puts the link away, whether or not the mouse has
    // moved since.
    if (event->key() == Qt::Key_Control)
        clearLink();
    if (event->key() == Qt::Key_Alt && m_maybeKeyboardTooltip) {
        m_maybeKeyboardTooltip = false;
        askForTooltipAtCaret();
    }
    QQuickItem::keyReleaseEvent(event);
}

void TextViewport::askForTooltip()
{
    askForTooltipAt(positionAt(m_hoverItemPos.x(), m_hoverItemPos.y()), m_hoverItemPos);
}

void TextViewport::askForTooltipAtCaret()
{
    // Where the caret is drawn, which is empty when it is scrolled off - and
    // then there is nothing on screen to put a tooltip beside.
    const QRectF caret = cursorRectangle();
    if (caret.isNull())
        return;
    askForTooltipAt(m_cursorPosition, caret.bottomLeft());
}

void TextViewport::askForTooltipAt(int position, const QPointF &at)
{
    TextDocument * const doc = textDocument();
    if (!doc || !m_tooltipHost || position < 0)
        return;

    QTextCursor cursor(doc->document());
    cursor.setPosition(position);

    // Below and right of the place asked about, the way Qt places a widget's
    // tooltip, so that the tooltip does not sit on top of what it is about.
    const QPoint point = mapToGlobal(at).toPoint() + QPoint(2, 16);
    m_hoverRunner->startChecking(
        cursor,
        [point](HoverTarget *target, BaseHoverHandler *handler, int) {
            handler->showToolTip(target, point);
        },
        [](HoverTarget *) { Utils::ToolTip::hide(); });
}

QTextCursor TextViewport::textCursor() const
{
    TextDocument * const doc = m_document ? m_document->textDocument() : nullptr;
    QTextDocument * const text = doc ? doc->document() : nullptr;
    if (!text)
        return {};

    QTextCursor cursor(text);
    // Move through what is on screen, not through what is in the file: without
    // this, Down from a folded line steps into the hidden text and the caret
    // is nowhere.
    cursor.setVisualNavigation(true);
    // The anchor is where the selection was started from, which is not always
    // where the caret is: a selection dragged upwards has its anchor after its
    // position, and collapsing it has to keep that straight.
    const bool selected = m_selectionStart >= 0 && m_selectionEnd >= 0
                          && m_selectionStart != m_selectionEnd;
    // Clamped: a position past the end is refused by QTextCursor, which keeps
    // the one it had - so a selection running past the end came back as no
    // selection at all, while the viewport went on drawing one. A selection
    // can outlive the text it was made in, an edit being all it takes.
    const int last = text->characterCount() - 1;
    cursor.setPosition(qBound(0, selected ? m_selectionStart : m_cursorPosition, last));
    if (selected)
        cursor.setPosition(qBound(0, m_selectionEnd, last), QTextCursor::KeepAnchor);
    return cursor;
}

void TextViewport::setTextCursor(const QTextCursor &cursor)
{
    // Here rather than on selectionChanged(): the position and the selection
    // are set one after the other below, so a listener on that signal sees a
    // moment with no selection in the middle of every call - including the
    // one that grew the selection and set the anchor.
    if (!cursor.hasSelection())
        m_selectBlockAnchor = QTextCursor();
    setCursorPosition(cursor.position());
    if (cursor.hasSelection()) {
        setSelectionStart(cursor.anchor());
        setSelectionEnd(cursor.position());
    } else {
        setSelectionStart(-1);
        setSelectionEnd(-1);
    }
    m_caretVisibleXPending = true;
    ensureCursorVisible();
    polish();
}

void TextViewport::ensureCursorVisible()
{
    if (m_lineHeight <= 0)
        return;

    TextDocument * const doc = m_document ? m_document->textDocument() : nullptr;
    QTextDocument * const text = doc ? doc->document() : nullptr;
    if (!text)
        return;

    QTextCursor cursor(text);
    cursor.setPosition(qBound(0, m_cursorPosition, text->characterCount() - 1));
    // Where the caret sits on screen, which is a row and not a line number.
    int row = rowOfBlock(cursor.block());
    if (m_wrapping) {
        // Which row of the block the caret is on. Without this the scroll is
        // measured to the top of the line, so a caret several rows into a
        // wrapped one pulls the view a row too far back every time.
        const QTextLayout * const layout = cursor.block().layout();
        if (layout && layout->lineCount() > 0) {
            const QTextLine line = layout->lineForTextPosition(
                m_cursorPosition - cursor.block().position());
            if (line.isValid())
                row += line.lineNumber();
        }
    }
    const qreal top = yOfRow(row);
    const bool above = top < m_scrollY;
    const bool below = top + rowSpan(row) > m_scrollY + height();
    if (!above && !below)
        return;

    // Put the caret in the middle rather than just inside the edge, where the
    // user asked for that: a caret that stops one line into view leaves
    // nothing to read in the direction it is heading.
    // The reader's setting, or this view's own - a language driving the view
    // can ask for centring for as long as it is in charge.
    if (m_centerOnScroll || displaySettings().centerCursorOnScroll())
        setScrollY(top - (height() - rowSpan(row)) / 2);
    else
        setScrollY(above ? top : top + rowSpan(row) - height());
}

void TextViewport::setRowGaps(const QList<Gap> &gaps)
{
    if (gaps == m_spacers)
        return;
    m_spacers = gaps;
    if (rebuildGaps())
        polish();
}

void TextViewport::setChangedLines(const QList<ChangedLine> &lines)
{
    if (lines == m_changedLines)
        return;
    m_changedLines = lines;
    m_changedByLine.clear();
    for (const ChangedLine &changed : std::as_const(m_changedLines))
        m_changedByLine.insert(changed.line, changed.chars);
    polish();
}

QStringList TextViewport::changedTextOnScreen() const
{
    QStringList texts;
    for (const Line &line : m_lines) {
        if (!line.layout)
            continue;
        const QString text = line.layout->text();
        for (const QTextLayout::FormatRange &range : line.layout->formats()) {
            if (range.format == m_changedCharFormat && range.length > 0)
                texts.append(text.mid(range.start, range.length));
        }
    }
    return texts;
}

QList<int> TextViewport::changedRowsOnScreen() const
{
    QList<int> rows;
    for (const Line &line : m_lines) {
        if (line.diffFill.isValid() && line.diffFill.alpha() > 0)
            rows.append(line.lineNumber);
    }
    return rows;
}

void TextViewport::setGhostRows(const QList<GhostRows> &ghosts)
{
    if (ghosts == m_ghosts)
        return;
    m_ghosts = ghosts;
    // Their height is a row count times a line height, so the gaps they open
    // can only be sized once there is a line height to multiply by.
    rebuildGaps();
    polish();
}

bool TextViewport::rebuildGaps()
{
    QList<Gap> gaps = m_spacers;
    const QList<GhostRows> ghosts = m_ghosts + m_suggestionGhosts;
    for (const GhostRows &ghost : ghosts) {
        if (!ghost.lines.isEmpty())
            gaps.append({ghost.row, ghost.lines.size() * m_lineHeight});
    }

    // A message asked for on a line of its own needs a line for it to be on.
    // Marks are enumerable and there are as many of them as there are
    // diagnostics, so this costs the marks rather than the document.
    if (displaySettings().annotationAlignment() == AnnotationAlignment::BetweenLines
        && m_lineHeight > 0) {
        TextDocument * const doc = m_document ? m_document->textDocument() : nullptr;
        if (doc) {
            for (TextMark * const mark : doc->marks()) {
                if (!mark->isVisible() || mark->icon().isNull()
                    || mark->lineAnnotation().isEmpty()) {
                    continue;
                }
                // Below the line it belongs to, which is above the next row.
                gaps.append({rowOfLine(mark->lineNumber()) + 1, m_lineHeight});
            }
        }
    }
    std::stable_sort(gaps.begin(), gaps.end(), [](const Gap &a, const Gap &b) {
        return a.row < b.row;
    });

    // Two things opening a gap above the same row open one gap, or the running
    // total counts a row twice.
    QList<Gap> merged;
    for (const Gap &gap : std::as_const(gaps)) {
        if (!merged.isEmpty() && merged.last().row == gap.row)
            merged.last().height += gap.height;
        else
            merged.append(gap);
    }

    if (merged == m_rowGaps)
        return false;

    m_rowGaps = merged;
    m_gapSums.clear();
    m_gapSums.reserve(m_rowGaps.size());
    qreal total = 0;
    for (const Gap &gap : std::as_const(m_rowGaps)) {
        total += gap.height;
        m_gapSums.push_back(total);
    }
    return true;
}

void TextViewport::layOutGhostRows()
{
    m_ghostLines.clear();
    const QList<GhostRows> ghosts = m_ghosts + m_suggestionGhosts;
    if (ghosts.isEmpty() || m_lineHeight <= 0)
        return;

    TextDocument * const doc = m_document ? m_document->textDocument() : nullptr;
    if (!doc)
        return;

    // The same scheme entry the decorated widget uses for the lines a diff
    // has removed, so the two views of one diff are coloured alike and follow
    // the theme together. A suggestion's own rows are the colour the row
    // above them is in, which is what says they are all one offer.
    const QTextCharFormat removed = doc->fontSettings().toTextCharFormat(C_DIFF_SOURCE_LINE);
    const QTextCharFormat suggested
        = doc->fontSettings().toTextCharFormat(TextStyles{C_TEXT, {C_DISABLED_CODE}});

    QTextOption option;
    option.setWrapMode(QTextOption::NoWrap);

    for (const GhostRows &ghost : ghosts) {
        if (ghost.lines.isEmpty())
            continue;

        // The gap sits above the row, so its rows run up to where that row
        // starts rather than down from it.
        const qreal bottom = yOfRow(ghost.row);
        const qreal top = bottom - ghost.lines.size() * m_lineHeight;
        if (bottom < m_scrollY || top > m_scrollY + height())
            continue;

        for (int i = 0; i < ghost.lines.size(); ++i) {
            const qreal y = top + i * m_lineHeight;
            if (y + m_lineHeight < m_scrollY || y > m_scrollY + height())
                continue;

            const bool isSuggestion = ghost.kind == GhostRows::Kind::Suggestion;
            const QTextCharFormat &format = isSuggestion ? suggested : removed;

            Line line;
            line.layout = std::make_unique<QTextLayout>(ghost.lines.at(i), m_font);
            line.layout->setTextOption(option);
            QTextLayout::FormatRange range;
            range.start = 0;
            range.length = int(ghost.lines.at(i).size());
            range.format = format;
            line.layout->setFormats({range});
            // The page the row is drawn on. A diff's removed line takes one;
            // a suggestion is drawn on the file's own background, because it
            // is a picture of what that line could say and not a line of its
            // own.
            line.diffFill = format.background().style() == Qt::NoBrush
                                ? QColor()
                                : format.background().color();
            line.layout->beginLayout();
            QTextLine textLine = line.layout->createLine();
            if (textLine.isValid()) {
                textLine.setLineWidth(qreal(INT_MAX));
                textLine.setPosition(QPointF(0, 0));
            }
            line.layout->endLayout();
            // A ghost row is in no block: nothing may map a screen position
            // onto it, which is what -1 says to anyone who asks.
            line.blockPosition = -1;
            line.at = QPointF(-m_scrollX, y - m_scrollY);
            m_ghostLines.push_back(std::move(line));
        }
    }
}

QList<QColor> TextViewport::ghostForegroundsOnScreen() const
{
    QList<QColor> colours;
    for (const Line &line : m_ghostLines) {
        if (!line.layout)
            continue;
        const QList<QTextLayout::FormatRange> formats = line.layout->formats();
        colours.append(formats.isEmpty() ? QColor()
                                         : formats.first().format.foreground().color());
    }
    return colours;
}

QStringList TextViewport::ghostTextOnScreen() const
{
    QStringList texts;
    for (const Line &line : m_ghostLines) {
        if (line.layout)
            texts.append(line.layout->text());
    }
    return texts;
}

QList<QRectF> TextViewport::ghostRectanglesOnScreen() const
{
    QList<QRectF> rects;
    for (const Line &line : m_ghostLines) {
        if (line.layout)
            rects.append(QRectF(line.at.x(), line.at.y(), width(), m_lineHeight));
    }
    return rects;
}

qreal TextViewport::gapAbove(int row) const
{
    if (m_rowGaps.isEmpty())
        return 0;

    // The number of gaps sitting at or above the top of this row: a gap on the
    // row itself is above its text, which is what puts the removed lines of a
    // diff over the line that replaced them.
    const auto behind = std::upper_bound(m_rowGaps.cbegin(), m_rowGaps.cend(), row,
                                         [](int r, const Gap &gap) { return r < gap.row; });
    const auto count = behind - m_rowGaps.cbegin();
    return count > 0 ? m_gapSums.at(count - 1) : 0;
}

qreal TextViewport::yOfRow(int row) const
{
    return row * m_lineHeight + gapAbove(row);
}

int TextViewport::rowAtY(qreal y) const
{
    if (m_lineHeight <= 0)
        return 0;
    if (m_rowGaps.isEmpty())
        return int(y / m_lineHeight);

    // How many gaps begin at or above y. A gap begins where the row it sits on
    // would have started without it.
    int lo = 0;
    int hi = int(m_rowGaps.size());
    while (lo < hi) {
        const int mid = (lo + hi) / 2;
        const qreal before = mid > 0 ? m_gapSums.at(mid - 1) : 0;
        if (m_rowGaps.at(mid).row * m_lineHeight + before <= y)
            lo = mid + 1;
        else
            hi = mid;
    }

    const qreal above = lo > 0 ? m_gapSums.at(lo - 1) : 0;
    const int row = int((y - above) / m_lineHeight);
    // A y inside a gap belongs to no row, so answer with the one under it
    // rather than the one the arithmetic lands on part way through.
    return lo > 0 ? qMax(row, m_rowGaps.at(lo - 1).row) : row;
}

qreal TextViewport::rowSpan(int row) const
{
    // A gap is not part of the row it sits above: a caret on that row is as
    // tall as its text, and scrolling to it need not bring the gap along.
    Q_UNUSED(row)
    return m_lineHeight;
}

int TextViewport::rowOfLine(int line)
{
    TextDocument * const doc = m_document ? m_document->textDocument() : nullptr;
    QTextDocument * const text = doc ? doc->document() : nullptr;
    if (!text)
        return 0;
    const QTextBlock block = text->findBlockByNumber(qBound(0, line - 1, text->blockCount() - 1));
    return block.isValid() ? rowOfBlock(block) : 0;
}

int TextViewport::rowCount()
{
    TextDocument * const doc = m_document ? m_document->textDocument() : nullptr;
    QTextDocument * const text = doc ? doc->document() : nullptr;
    if (!text)
        return 0;
    if (const Utils::TextEditorLayout * const rows = m_wrapping ? editorLayout() : nullptr)
        return rows->lineCount();
    return text->lineCount();
}

int TextViewport::rowOfBlock(const QTextBlock &block)
{
    if (const Utils::TextEditorLayout * const rows = m_wrapping ? editorLayout() : nullptr)
        return rows->firstLineNumberOf(block);
    // Not the block number: whatever is folded above it takes up no room.
    return block.firstLineNumber();
}

void TextViewport::ensureCaretVisibleSideways()
{
    // Where the caret is across the line is only known once the row it is on
    // has been laid out, so this runs at the end of updatePolish() rather than
    // when the caret moves.
    m_caretVisibleXPending = false;
    if (m_wrapping || width() <= 0)
        return;
    const QRectF caret = rectangleAt(m_cursorPosition);
    if (caret.isNull())
        return;

    // Centred rather than nudged just inside the edge, which is what the
    // widget editor does - see PlainTextEditPrivate::ensureCursorVisible().
    // Nudging leaves the caret against the edge it came in at, with nothing
    // ahead of it to read.
    if (caret.right() > width() || caret.left() < 0)
        setScrollX(m_scrollX + caret.center().x() - width() / 2);
}

static Utils::PlainTextDocumentLayout *layoutOf(const QTextDocument *text)
{
    return text ? qobject_cast<Utils::PlainTextDocumentLayout *>(text->documentLayout())
                : nullptr;
}

// Home, the way an editor means it: the first character that is not
// indentation, and the true start of the line when the cursor is already
// there. Mirrors TextEditorWidgetPrivate::handleHomeKey().
static void moveToFirstCharacter(QTextCursor &cursor, QTextCursor::MoveMode mode)
{
    const QTextBlock block = cursor.block();
    const int start = block.position();
    const int was = cursor.position();

    const QString text = block.text();
    int offset = 0;
    while (offset < text.size()
           && (text.at(offset) == '\t' || text.at(offset).category() == QChar::Separator_Space)) {
        ++offset;
        // Stopping *at* the cursor rather than running past it is what sends a
        // caret sitting inside the indentation to the margin instead of
        // forward to the code. The widget editor stops here too.
        if (start + offset == was)
            break;
    }

    cursor.setPosition(start + offset == was ? start : start + offset, mode);
}

void TextViewport::keyPressEvent(QKeyEvent *event)
{
    // Any key ends a run of line moves, so the next one starts its own
    // undo step rather than joining what was typed before it.
    m_lineMoveJoinsUndo = false;
    // Alt on its own asks for a tooltip when it is let go; Alt with anything
    // else is a shortcut, so any other key takes the offer back.
    m_maybeKeyboardTooltip = event->key() == Qt::Key_Alt
                             && globalBehaviorSettings().keyboardTooltips();

    // Control pressed while the mouse is over the text asks for the link under
    // it, rather than waiting for the mouse to move first.
    if (event->key() == Qt::Key_Control && m_hovering)
        updateLink(m_hoverItemPos, Qt::ControlModifier);

    if (!m_mouseHidden && isTypingKey(event->key())
        && hideMouseWhileTyping(globalBehaviorSettings().data())) {
        m_mouseHidden = true;
        emit mouseHiddenChanged();
    }

    QTextCursor cursor = textCursor();
    if (cursor.isNull()) {
        QQuickItem::keyPressEvent(event);
        return;
    }

    // What the language does with the key, before this does anything with it:
    // Enter inside a doxygen comment writes the block rather than a newline.
    // Asked only with one caret, because that is what these edit around.
    TextDocument * const language = m_document ? m_document->textDocument() : nullptr;
    if (language && !multiTextCursor().hasMultipleCursors()
        && language->handleKeyPress(event, cursor)) {
        return event->accept();
    }

    // And what it does with the key in *this* view, where it is in the middle
    // of something that spans several places at once - an in-place rename.
    // After the document above, which answers for the file rather than for a
    // caret, and before anything this view would do on its own.
    for (EditHandler * const handler : editHandlers()) {
        if (handler->handleKeyPress(event, [this, event] { processKeyNormally(event); }))
            return;
    }

    processKeyNormally(event);
}

bool TextViewport::wantsKeyBeforeShortcuts(QKeyEvent *event)
{
    // The language first: a modal editing mode wants keys that are bound to
    // commands, and only it knows which.
    for (EditHandler * const handler : editHandlers()) {
        if (handler->wantsKeyBeforeShortcuts(event))
            return true;
    }

    // Escape belongs to the view while it has something to dismiss with it.
    if (event->key() == Qt::Key_Escape)
        return multiTextCursor().hasMultipleCursors() || currentSuggestion();

    // Otherwise the same rule the widget editor uses: ordinary typing is the
    // view's, and anything with a real modifier is a shortcut's. Mirrors
    // QInputControl::isCommonTextEditShortcut().
    return (event->modifiers() == Qt::NoModifier || event->modifiers() == Qt::ShiftModifier
            || event->modifiers() == Qt::KeypadModifier)
           && event->key() < Qt::Key_Escape;
}

void TextViewport::processKeyNormally(QKeyEvent *event)
{
    QTextCursor cursor = textCursor();
    if (cursor.isNull()) {
        QQuickItem::keyPressEvent(event);
        return;
    }

    // Insert turns typing-over on and off. Only on its own: Shift+Insert is
    // paste and Ctrl+Insert is copy, which is what the widget editor checks
    // for here too.
    if (event->key() == Qt::Key_Insert && canEdit()
        && (event->modifiers() == Qt::NoModifier
            || event->modifiers() == Qt::KeypadModifier)) {
        setOverwriteMode(!m_overwriteMode);
        return event->accept();
    }

    const QTextCursor::MoveMode mode = event->modifiers().testFlag(Qt::ShiftModifier)
                                           ? QTextCursor::KeepAnchor
                                           : QTextCursor::MoveAnchor;

    // Home goes to the first thing on the line and only to column zero from
    // there, which is what an editor does with indented code. Before the move
    // table below, which would otherwise take the same key - the same order
    // the widget editor uses.
    const bool toStartOfBlock = event->matches(QKeySequence::MoveToStartOfBlock)
                                || event->matches(QKeySequence::SelectStartOfBlock);
    if (toStartOfBlock || event->matches(QKeySequence::MoveToStartOfLine)
        || event->matches(QKeySequence::SelectStartOfLine)) {
        moveToFirstCharacter(cursor, mode);
        setTextCursor(cursor);
        return event->accept();
    }

    if (event->key() == Qt::Key_PageUp || event->key() == Qt::Key_PageDown) {
        // Pages are the one move that depends on how tall the view is, so they
        // are not in the shared table below.
        const bool up = event->key() == Qt::Key_PageUp;
        const int rows = rowsPerPage();

        // A page is a screen of rows, and a wrapped line is several rows, so
        // counting lines moves by as many screens as a line takes rows. The
        // rows are the viewport's own - it works out where the lines break,
        // and the document's layout never sees that - so the page is taken by
        // scrolling a screen and then putting the caret back where it was on
        // screen, which is where the reader is still looking.
        const QRectF caret = rectangleAt(m_cursorPosition);
        if (!caret.isNull()) {
            const qreal was = m_scrollY;
            setScrollY(m_scrollY + (up ? -1 : 1) * rows * m_lineHeight);
            if (!qFuzzyCompare(was + 1, m_scrollY + 1)) {
                m_pendingPage = PendingPage{caret.left(), caret.top(), mode};
                polish();
                return event->accept();
            }
        }

        // Nothing scrolled, so this is the first screen or the last one and
        // there is no page to take: move by rows the way it always did.
        cursor.movePosition(up ? QTextCursor::Up : QTextCursor::Down, mode, rows);
        setTextCursor(cursor);
        return event->accept();
    }

    // Everything else the keyboard can do to a cursor comes from the one place
    // both editors take it: the platform's own chords - a word is an Alt chord
    // on a Mac and a Ctrl chord elsewhere, and Home means the top of the file
    // on one and the start of the line on the other - plus camel-case stepping
    // and moving through what is on screen rather than what is in the file.
    // Moving down a wrapped line has to keep the column the caret started
    // from. A QTextCursor carries that; this one is built fresh from a
    // position each key, so the memory is handed back before the move and
    // taken after. Without it the second Down measures from the end of the row
    // it is on and steps over one.
    cursor.setVerticalMovementX(m_verticalMovementX);
    Utils::MultiTextCursor cursors({cursor});
    // The layout that knows where the rows are. While the viewport is wrapping
    // that is its own - the document's layout never sees that wrapping, so
    // handing it over makes Down step over a whole wrapped line instead of on
    // to the next row of it.
    if (cursors.handleMoveKeyEvent(event, globalBehaviorSettings().camelCaseNavigation(),
                                   movementLayout())) {
        m_verticalMovementX = cursors.mainCursor().verticalMovementX();
        setTextCursor(cursors.mainCursor());
        return event->accept();
    }

    // Reading a file means being able to select all of it and copy it, so
    // these come before the read-only guard.
    if (event->matches(QKeySequence::SelectAll)) {
        selectAll();
        event->accept();
        return;
    }
    if (event->matches(QKeySequence::Copy)) {
        copy();
        event->accept();
        return;
    }

    // Back to one caret. Before the read-only check, because collapsing them
    // is not an edit - a view that cannot be typed into can still have been
    // given several carets, and has to be able to be rid of them. Only when
    // there is more than one: Escape means other things elsewhere, and taking
    // it always would be taking it from them.
    if (event->key() == Qt::Key_Escape) {
        // A suggestion first, which is what the widget editor takes Escape
        // for before anything else: it is the thing on screen that the reader
        // most likely means to be rid of, and it is the only one that is
        // there without having been asked for.
        if (currentSuggestion()) {
            clearSuggestion();
            event->accept();
            return;
        }
        if (m_extraCursors.isEmpty()) {
            QQuickItem::keyPressEvent(event);
            return;
        }
        m_extraCursors.clear();
        polish();
        emit cursorRectangleChanged();
        event->accept();
        return;
    }

    // Told not to edit, or editing a file the filesystem will not take back.
    // The widget editor offers to make it writable; this only refuses, which
    // is the half that must not be missing.
    TextDocument * const doc = m_document->textDocument();
    if (!canEdit()) {
        QQuickItem::keyPressEvent(event);
        return;
    }

    // Undo and redo are the document's, so they take back what any other view
    // of it did as well - which is the point of editing through a cursor.
    if (event->matches(QKeySequence::Undo) || event->matches(QKeySequence::Redo)) {
        if (event->matches(QKeySequence::Undo))
            undo();
        else
            redo();
        event->accept();
        return;
    }

    if (event->matches(QKeySequence::Cut)) {
        cut();
        event->accept();
        return;
    }
    if (event->matches(QKeySequence::Paste)) {
        paste();
        event->accept();
        return;
    }

    switch (event->key()) {
    case Qt::Key_Backspace:
        applyToEveryCaret([this](QTextCursor &caret) {
            // Between the two halves of a pair this editor inserted, Backspace
            // takes both - otherwise it leaves the closing one orphaned.
            if (!caret.hasSelection() && m_autoCompleter->autoBackspace(caret))
                return;
            // With a selection, Backspace removes it rather than one more
            // character before it, which deletePreviousChar() already does.
            if (caret.hasSelection() || !handleSmartBackspace(caret))
                caret.deletePreviousChar();
        });
        event->accept();
        return;
    case Qt::Key_Delete:
        applyToEveryCaret([](QTextCursor &caret) { caret.deleteChar(); });
        event->accept();
        return;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        // The new line starts where the language says it should, which is the
        // difference between an editor and a text box.
        applyToEveryCaret([doc](QTextCursor &caret) {
            caret.insertText("\n");
            doc->autoIndent(caret);
        });
        event->accept();
        return;
    case Qt::Key_Space:
        // Ctrl+Space is what asks for the list; the form is what shows it.
        if (event->modifiers().testFlag(Qt::ControlModifier)) {
            emit completionRequested();
            event->accept();
            return;
        }
        applyToEveryCaret([this, typed = event->text()](QTextCursor &caret) {
            insertTypedText(caret, typed);
        });
        event->accept();
        return;
    case Qt::Key_Tab:
        // One indent's worth of whatever the tab settings say, and a whole
        // block where something is selected. A literal tab is what a text box
        // types; it is not what a code style asks for.
        // The indenter already takes several cursors and knows how to apply
        // one indent to all of them, so this hands over the carets rather
        // than unwrapping a single one and putting it back.
        indent();
        event->accept();
        return;
    case Qt::Key_Backtab:
        unindent();
        event->accept();
        return;
    default:
        // Anything else is text only if it produced any. A modifier chord
        // produces none, and neither does a function key.
        if (event->text().isEmpty() || event->text().at(0).isNonCharacter()
            || event->text().at(0).category() == QChar::Other_Control) {
            QQuickItem::keyPressEvent(event);
            return;
        }
        applyToEveryCaret([this, typed = event->text()](QTextCursor &caret) {
            insertTypedText(caret, typed);
        });
        event->accept();
        return;
    }

    setTextCursor(cursor);
    event->accept();
}

// Typing, as opposed to inserting: what the user typed goes in, and then the
// language gets to say what else belongs with it - the closing bracket or
// quote, and the indentation a character like '}' asks for. The closing text is
// left *after* the caret, which is what makes it something to type over rather
// than something to delete.
void TextViewport::insertTypedText(QTextCursor &cursor, const QString &text)
{
    // Overwriting: what is under the caret goes, unless the caret is at the end
    // of the line, where there is nothing to type over. The same rule the
    // widget editor's text control uses.
    const auto takeOverwrittenCharacter = [this](QTextCursor &cursor) {
        if (m_overwriteMode && !cursor.hasSelection() && !cursor.atBlockEnd())
            cursor.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor);
    };

    TextDocument * const doc = m_document ? m_document->textDocument() : nullptr;
    if (!doc || !m_autoCompleter) {
        takeOverwrittenCharacter(cursor);
        cursor.insertText(text);
        return;
    }

    QChar electricChar;
    if (doc->typingSettings().m_autoIndent) {
        for (const QChar c : text) {
            if (doc->indenter()->isElectricCharacter(c)) {
                electricChar = c;
                break;
            }
        }
    }

    QTextCursor probe = cursor;
    const QString closing = m_autoCompleter->autoComplete(probe, text, false);

    cursor.beginEditBlock();
    // After the auto-completer has been asked, which reads the caret as the
    // reader left it rather than with a character already selected.
    takeOverwrittenCharacter(cursor);
    cursor.insertText(text);
    if (!closing.isEmpty()) {
        const int before = cursor.position();
        cursor.insertText(closing);
        cursor.setPosition(before);
    }
    if (!electricChar.isNull() && m_autoCompleter->contextAllowsElectricCharacters(cursor))
        doc->autoIndent(cursor, electricChar, cursor.position());
    cursor.endEditBlock();

    offerCompletionsIfAsked(cursor);
}

// Some characters mean "you are about to name something" - a '.' or a '->' in
// most languages - and the language is what says which. Asking after every
// keystroke would put a list in front of the user constantly and, for a
// language that thinks about it in a thread, do so at a cost.
void TextViewport::offerCompletionsIfAsked(const QTextCursor &cursor)
{
    if (globalCompletionSettings().completionTrigger() == ManualCompletion)
        return;

    TextDocument * const doc = m_document ? m_document->textDocument() : nullptr;
    CompletionAssistProvider * const provider = doc ? doc->completionAssistProvider() : nullptr;
    if (!provider)
        return;

    const int length = provider->activationCharSequenceLength();
    if (length <= 0 || cursor.position() < length)
        return;

    const QString sequence = cursor.document()->toPlainText().mid(cursor.position() - length,
                                                                  length);
    if (provider->isActivationCharSequence(sequence))
        emit completionRequested();
}

void TextViewport::inputMethodEvent(QInputMethodEvent *event)
{
    if (m_readOnly) {
        event->ignore();
        return;
    }

    QTextCursor cursor = textCursor();
    if (cursor.isNull()) {
        event->ignore();
        return;
    }

    m_preeditText = event->preeditString();
    m_preeditFormats.clear();
    for (const QInputMethodEvent::Attribute &attribute : event->attributes()) {
        if (attribute.type != QInputMethodEvent::TextFormat)
            continue;
        QTextLayout::FormatRange range;
        range.start = attribute.start;
        range.length = attribute.length;
        range.format = qvariant_cast<QTextFormat>(attribute.value).toCharFormat();
        m_preeditFormats.append(range);
    }

    if (!event->commitString().isEmpty() || event->replacementLength() > 0) {
        // A replacement is counted from the cursor, and may reach back before
        // it - an input method correcting what it already committed.
        if (event->replacementLength() > 0) {
            cursor.setPosition(cursor.position() + event->replacementStart());
            cursor.setPosition(cursor.position() + event->replacementLength(),
                               QTextCursor::KeepAnchor);
        }
        cursor.insertText(event->commitString());
        setTextCursor(cursor);
    }

    polish();
    update();
    event->accept();
}

QVariant TextViewport::inputMethodQuery(Qt::InputMethodQuery query) const
{
    const QTextCursor cursor = textCursor();
    switch (query) {
    case Qt::ImEnabled:
        return !m_readOnly;
    case Qt::ImHints:
        return int(Qt::ImhMultiLine);
    case Qt::ImCursorRectangle:
        return cursorRectangle();
    case Qt::ImFont:
        return m_document && m_document->textDocument()
                   ? m_document->textDocument()->fontSettings().font()
                   : QFont();
    case Qt::ImCursorPosition:
        // Relative to the surrounding text, which is the block the caret is in.
        return cursor.isNull() ? 0 : cursor.positionInBlock();
    case Qt::ImAnchorPosition:
        return cursor.isNull() ? 0 : cursor.anchor() - cursor.block().position();
    case Qt::ImSurroundingText:
        return cursor.isNull() ? QString() : cursor.block().text();
    case Qt::ImCurrentSelection:
        return cursor.isNull() ? QString() : selectedPlainText(cursor);
    case Qt::ImAbsolutePosition:
        return cursor.isNull() ? 0 : cursor.position();
    default:
        return QQuickItem::inputMethodQuery(query);
    }
}

int TextViewport::cursorPosition() const
{
    return m_cursorPosition;
}

void TextViewport::setCursorPosition(int position)
{
    // Putting the caret somewhere is putting *the* caret somewhere, so the
    // extra ones are gone - before the early return, because putting it where
    // it already is still says there is one of it. Whoever wants to keep them
    // says so with setMultiTextCursor() afterwards.
    m_extraCursors.clear();
    if (m_cursorPosition == position)
        return;
    // Put somewhere rather than moved there, so there is no column to keep.
    m_verticalMovementX = -1;
    m_cursorPosition = position;
    // What "Highlight blocks" adds: the scope around the caret lights up as it
    // moves, without anything being hovered.
    if (displaySettings().highlightBlocks())
        setScopeBlock(cursorBlock().blockNumber());
    polish();
    // A hint that is up describes the call the caret is in, so moving the
    // caret is what changes what it says - or ends it.
    if (m_functionHintProposal)
        updateFunctionHint();
    updateSuggestion();
    // Numbering counted from the caret has to follow it.
    updateRelativeOrigin();
    emit cursorPositionChanged();
    emit cursorRectangleChanged();
}

QRectF TextViewport::cursorRectangle() const
{
    return rectangleAt(m_cursorPosition);
}

QVariantList TextViewport::caretRectangles() const
{
    QVariantList rects;
    rects.append(rectangleAt(m_cursorPosition));
    for (const QTextCursor &cursor : m_extraCursors)
        rects.append(rectangleAt(cursor.position()));
    return rects;
}

Utils::MultiTextCursor TextViewport::multiTextCursor() const
{
    // The main caret last, which is where MultiTextCursor keeps it -
    // mainCursor() is the back of the list. Anything reading cursors().first()
    // means the oldest one, and the algorithms reused here are written that
    // way; putting the main one first quietly gives them the wrong anchor.
    QList<QTextCursor> cursors = m_extraCursors;
    cursors.append(textCursor());
    return Utils::MultiTextCursor(cursors);
}

void TextViewport::setMultiTextCursor(const Utils::MultiTextCursor &cursors)
{
    QList<QTextCursor> all = cursors.cursors();
    if (all.isEmpty()) {
        m_extraCursors.clear();
        return;
    }
    // The last is the main one; the rest are extra, in the order they were
    // made. setTextCursor() is what moves the view and marks the caret for
    // redrawing, so the main one goes through it - and it clears the extras,
    // so they are put back after.
    const QTextCursor main = all.takeLast();
    setTextCursor(main);
    m_extraCursors = all;
}

void TextViewport::addCaretAt(int position)
{
    TextDocument * const doc = m_document ? m_document->textDocument() : nullptr;
    QTextDocument * const text = doc ? doc->document() : nullptr;
    if (!text)
        return;

    QTextCursor added(text);
    added.setPosition(qBound(0, position, text->characterCount() - 1));

    Utils::MultiTextCursor cursors = multiTextCursor();
    cursors.addCursor(added);
    // Clicking where a caret already is does not put a second one there.
    cursors.mergeCursors();
    setMultiTextCursor(cursors);
}

void TextViewport::addCaretsToLineEnds()
{
    TextDocument * const doc = m_document ? m_document->textDocument() : nullptr;
    QTextDocument * const text = doc ? doc->document() : nullptr;
    if (!text)
        return;

    // The lines a selection covers, except the one it ends on: a selection
    // stopping part way through a line was not asking for that line's end.
    Utils::MultiTextCursor ends;
    for (const QTextCursor &caret : multiTextCursor().cursors()) {
        if (!caret.hasSelection())
            continue;
        QTextBlock block = text->findBlock(caret.selectionStart());
        while (block.isValid()) {
            const int blockEnd = block.position() + block.length() - 1;
            if (blockEnd >= caret.selectionEnd())
                break;
            QTextCursor atEnd(text);
            atEnd.setPosition(blockEnd);
            ends.addCursor(atEnd);
            block = block.next();
        }
    }

    // Nothing spanned more than its own line, so there is nothing to replace
    // the carets with.
    if (!ends.isNull())
        setMultiTextCursor(ends);
}

void TextViewport::anchorBlockSelection(int position)
{
    m_blockSelectionAnchor = textCursor();
    if (position < 0 || m_blockSelectionAnchor.isNull())
        return;
    QTextDocument * const text = m_blockSelectionAnchor.document();
    m_blockSelectionAnchor.setPosition(qBound(0, position, text->characterCount() - 1));
}

void TextViewport::selectBlockTo(qreal x, qreal y)
{
    TextDocument * const doc = m_document ? m_document->textDocument() : nullptr;
    QTextDocument * const text = doc ? doc->document() : nullptr;
    if (!text || m_blockSelectionAnchor.isNull())
        return;

    QTextCursor corner(text);
    corner.setPosition(qBound(0, positionAt(x, y), text->characterCount() - 1));

    const TabSettingsData tabSettings = doc->tabSettings();
    int column = tabSettings.columnAt(corner.block().text(), corner.positionInBlock());
    // Past the end of the line there are no characters to count, so how far
    // beyond it the pointer is has to be measured rather than counted. This
    // is what lets a rectangle be dragged out over short lines.
    if (corner.positionInBlock() == corner.block().length() - 1) {
        const qreal charWidth = QFontMetricsF(m_font).horizontalAdvance(QLatin1Char(' '));
        if (charWidth > 0) {
            const QRectF at = rectangleAt(corner.position());
            column += int((x + m_scrollX - at.center().x()) / charWidth);
        }
    }

    const BlockSelection selection{corner.blockNumber(),
                                   qMax(0, column),
                                   m_blockSelectionAnchor.blockNumber(),
                                   tabSettings.columnAt(m_blockSelectionAnchor.block().text(),
                                                        m_blockSelectionAnchor.positionInBlock())};

    const QList<QTextCursor> carets = cursorsForBlockSelection(text, tabSettings, selection);
    if (carets.isEmpty())
        return;
    Utils::MultiTextCursor cursors;
    cursors.addCursors(carets);
    setMultiTextCursor(cursors);
}

void TextViewport::uppercaseSelection()
{
    transformSelectedText([](const QString &text) { return text.toUpper(); });
}

void TextViewport::lowercaseSelection()
{
    transformSelectedText([](const QString &text) { return text.toLower(); });
}

void TextViewport::transformSelectedText(const TextTransformation &transform)
{
    if (!canEdit())
        return;
    Utils::MultiTextCursor cursors = multiTextCursor();
    TextEditor::transformSelection(cursors, transform);
    setMultiTextCursor(cursors);
}

void TextViewport::insertLineAbove()
{
    if (!canEdit())
        return;
    Utils::MultiTextCursor cursors = multiTextCursor();
    TextEditor::insertLineAbove(cursors, m_document ? m_document->textDocument() : nullptr);
    setMultiTextCursor(cursors);
}

void TextViewport::insertLineBelow()
{
    if (!canEdit())
        return;
    Utils::MultiTextCursor cursors = multiTextCursor();
    TextEditor::insertLineBelow(cursors, m_document ? m_document->textDocument() : nullptr);
    setMultiTextCursor(cursors);
}

void TextViewport::duplicateSelection()
{
    if (!canEdit())
        return;
    Utils::MultiTextCursor cursors = multiTextCursor();
    // No comment definition here: which markers a language uses is something
    // the Quick editor is not told yet, so "Duplicate and Comment" stays with
    // the widget editor for now.
    TextEditor::duplicateSelection(cursors);
    setMultiTextCursor(cursors);
}

void TextViewport::sortLines()
{
    TextDocument * const doc = m_document ? m_document->textDocument() : nullptr;
    if (!canEdit() || !doc)
        return;
    // Sorting several disjoint runs at once has no obvious meaning, the same
    // conclusion the widget editor came to.
    if (multiTextCursor().hasMultipleCursors())
        return;

    QTextCursor cursor = textCursor();
    if (!TextEditor::sortLines(cursor, doc->tabSettings()))
        return;
    setTextCursor(cursor);
}

Utils::CommentDefinition TextViewport::commentDefinition() const
{
    TextDocument * const doc = m_document ? m_document->textDocument() : nullptr;
    if (!doc)
        return {};
    return HighlighterHelper::commentDefinitionFor(HighlighterHelper::definitionForDocument(doc),
                                                   doc->typingSettings());
}

void TextViewport::unCommentSelection()
{
    TextDocument * const doc = m_document ? m_document->textDocument() : nullptr;
    if (!canEdit() || !doc)
        return;
    const bool singleLine = doc->typingSettings().m_preferSingleLineComments;
    setMultiTextCursor(
        Utils::unCommentSelection(multiTextCursor(), commentDefinition(), singleLine));
}

void TextViewport::duplicateSelectionAndComment()
{
    if (!canEdit())
        return;
    const Utils::CommentDefinition comment = commentDefinition();
    Utils::MultiTextCursor cursors = multiTextCursor();
    TextEditor::duplicateSelection(cursors, &comment);
    setMultiTextCursor(cursors);
}

void TextViewport::selectWholeLines()
{
    TextDocument * const doc = m_document ? m_document->textDocument() : nullptr;
    if (!doc)
        return;
    Utils::MultiTextCursor cursors = multiTextCursor();
    TextEditor::maybeSelectLine(cursors, doc->document());
    setMultiTextCursor(cursors);
}

void TextViewport::deleteLine()
{
    if (!canEdit())
        return;
    selectWholeLines();
    // The main caret's line, as the widget editor does it: with several
    // carets it takes the one line rather than all of them.
    QTextCursor cursor = textCursor();
    if (!cursor.hasSelection())
        return;
    cursor.removeSelectedText();
    setTextCursor(cursor);
}

void TextViewport::copyLine()
{
    selectWholeLines();
    const QTextCursor cursor = textCursor();
    if (cursor.hasSelection())
        QGuiApplication::clipboard()->setText(selectedPlainText(cursor));
}

void TextViewport::cutLine()
{
    if (!canEdit())
        return;
    selectWholeLines();
    QTextCursor cursor = textCursor();
    if (!cursor.hasSelection())
        return;
    QGuiApplication::clipboard()->setText(selectedPlainText(cursor));
    cursor.removeSelectedText();
    setTextCursor(cursor);
}

void TextViewport::copyLineUp()
{
    copyLineUpOrDown(true);
}

void TextViewport::copyLineDown()
{
    copyLineUpOrDown(false);
}

void TextViewport::copyLineUpOrDown(bool up)
{
    TextDocument * const doc = m_document ? m_document->textDocument() : nullptr;
    if (!canEdit() || !doc)
        return;
    // One caret only, the same conclusion the widget editor came to: where
    // the copies of several lines would go is not a question with an answer.
    if (multiTextCursor().hasMultipleCursors())
        return;
    QTextCursor cursor = textCursor();
    TextEditor::copyLineUpDown(cursor, up, doc);
    setTextCursor(cursor);
}

void TextViewport::moveLineUp()
{
    moveLineUpOrDown(true);
}

void TextViewport::moveLineDown()
{
    moveLineUpOrDown(false);
}

void TextViewport::moveLineUpOrDown(bool up)
{
    TextDocument * const doc = m_document ? m_document->textDocument() : nullptr;
    if (!canEdit() || !doc)
        return;
    if (multiTextCursor().hasMultipleCursors())
        return;

    const QTextCursor cursor = textCursor();
    QTextCursor move = TextEditor::selectLinesToMove(cursor);
    if (m_lineMoveJoinsUndo)
        move.joinPreviousEditBlock();
    else
        move.beginEditBlock();

    // No refactor markers to carry along: that overlay is the widget editor's.
    TextEditor::moveSelectedLines(move, up, cursor.hasSelection(), doc, commentDefinition());

    move.endEditBlock();
    setTextCursor(move);
    m_lineMoveJoinsUndo = true;
}

void TextViewport::rewrapParagraph()
{
    TextDocument * const doc = m_document ? m_document->textDocument() : nullptr;
    if (!canEdit() || !doc)
        return;
    QTextCursor cursor = textCursor();
    TextEditor::rewrapParagraph(cursor, doc->tabSettings(),
                                marginSettings().data().m_marginColumn);
    setTextCursor(cursor);
}

QList<EditHandler *> TextViewport::editHandlers() const
{
    return m_editor ? m_editor->findChildren<EditHandler *>() : QList<EditHandler *>();
}

void TextViewport::selectAll()
{
    for (EditHandler * const handler : editHandlers()) {
        if (handler->handleSelectAll())
            return;
    }
    // Reading a file means being able to select all of it, so no edit check.
    QTextCursor cursor = textCursor();
    if (cursor.isNull())
        return;
    cursor.select(QTextCursor::Document);
    setTextCursor(cursor);
}

void TextViewport::copy()
{
    const QTextCursor cursor = textCursor();
    if (!cursor.isNull() && cursor.hasSelection())
        QGuiApplication::clipboard()->setText(selectedPlainText(cursor));
}

void TextViewport::cut()
{
    for (EditHandler * const handler : editHandlers()) {
        if (handler->handleCut())
            return;
    }
    cutNormally();
}

void TextViewport::cutNormally()
{
    if (!canEdit())
        return;
    QTextCursor cursor = textCursor();
    if (cursor.isNull() || !cursor.hasSelection())
        return;
    QGuiApplication::clipboard()->setText(selectedPlainText(cursor));
    cursor.removeSelectedText();
    setTextCursor(cursor);
}

void TextViewport::paste()
{
    for (EditHandler * const handler : editHandlers()) {
        if (handler->handlePaste())
            return;
    }
    pasteNormally();
}

void TextViewport::pasteNormally()
{
    if (!canEdit())
        return;
    QTextCursor cursor = textCursor();
    if (cursor.isNull())
        return;
    cursor.insertText(QGuiApplication::clipboard()->text());
    setTextCursor(cursor);
}

void TextViewport::undo()
{
    if (!canEdit())
        return;
    QTextCursor cursor = textCursor();
    if (cursor.isNull())
        return;
    // The document's, so it takes back what any other view of it did as well,
    // which is the point of editing through a cursor.
    cursor.document()->undo(&cursor);
    setTextCursor(cursor);
}

void TextViewport::redo()
{
    if (!canEdit())
        return;
    QTextCursor cursor = textCursor();
    if (cursor.isNull())
        return;
    cursor.document()->redo(&cursor);
    setTextCursor(cursor);
}

Utils::PlainTextDocumentLayout *TextViewport::movementLayout() const
{
    const QTextCursor cursor = textCursor();
    Utils::PlainTextDocumentLayout *movement = cursor.isNull()
                                                   ? nullptr
                                                   : layoutOf(cursor.document());
    if (m_wrapping) {
        if (Utils::TextEditorLayout * const rows
            = const_cast<TextViewport *>(this)->editorLayout()) {
            movement = rows;
        }
    }
    return movement;
}

void TextViewport::moveCursor(QTextCursor::MoveOperation operation, QTextCursor::MoveMode mode)
{
    Utils::MultiTextCursor cursors = multiTextCursor();
    if (cursors.isNull())
        return;
    cursors.movePosition(operation, mode, 1, movementLayout());
    m_verticalMovementX = cursors.mainCursor().verticalMovementX();
    setMultiTextCursor(cursors);
}

void TextViewport::moveCamelCase(bool forward, QTextCursor::MoveMode mode)
{
    Utils::MultiTextCursor cursors = multiTextCursor();
    if (cursors.isNull())
        return;
    if (forward)
        Utils::CamelCaseCursor::right(&cursors, mode);
    else
        Utils::CamelCaseCursor::left(&cursors, mode);
    setMultiTextCursor(cursors);
}

void TextViewport::gotoLineStart(QTextCursor::MoveMode mode)
{
    QTextCursor cursor = textCursor();
    if (cursor.isNull())
        return;
    moveToFirstCharacter(cursor, mode);
    setTextCursor(cursor);
}

void TextViewport::selectWordUnderCursor()
{
    Utils::MultiTextCursor cursors = multiTextCursor();
    if (cursors.isNull())
        return;
    TextEditor::selectWordUnderCursor(cursors);
    setMultiTextCursor(cursors);
}

void TextViewport::clearSelection()
{
    Utils::MultiTextCursor cursors = multiTextCursor();
    if (cursors.isNull())
        return;
    cursors.clearSelection();
    setMultiTextCursor(cursors);
}

int TextViewport::rowsPerPage() const
{
    return qMax(1, int(height() / qMax(1.0, m_lineHeight)) - 1);
}

void TextViewport::scrollByRows(int rows)
{
    setScrollY(m_scrollY + rows * m_lineHeight);
}

void TextViewport::viewPageUp()
{
    scrollByRows(-rowsPerPage());
}

void TextViewport::viewPageDown()
{
    scrollByRows(rowsPerPage());
}

void TextViewport::viewLineUp()
{
    scrollByRows(-1);
}

void TextViewport::viewLineDown()
{
    scrollByRows(1);
}

void TextViewport::gotoBlockStart(bool select)
{
    if (multiTextCursor().hasMultipleCursors())
        return;
    QTextCursor cursor = textCursor();
    if (cursor.isNull() || !TextBlockUserData::findPreviousOpenParenthesis(&cursor, select))
        return;
    setTextCursor(cursor);
    updateParenthesesMatch();
}

void TextViewport::gotoBlockEnd(bool select)
{
    if (multiTextCursor().hasMultipleCursors())
        return;
    QTextCursor cursor = textCursor();
    if (cursor.isNull() || !TextBlockUserData::findNextClosingParenthesis(&cursor, select))
        return;
    setTextCursor(cursor);
    updateParenthesesMatch();
}

bool TextViewport::selectBlockUp()
{
    if (multiTextCursor().hasMultipleCursors())
        return false;
    QTextCursor cursor = textCursor();
    if (cursor.isNull() || !TextEditor::selectBlockUp(cursor, m_selectBlockAnchor))
        return false;
    setTextCursor(cursor);
    updateParenthesesMatch();
    return true;
}

bool TextViewport::selectBlockDown()
{
    if (multiTextCursor().hasMultipleCursors())
        return false;
    QTextCursor cursor = textCursor();
    if (cursor.isNull() || !TextEditor::selectBlockDown(cursor, m_selectBlockAnchor))
        return false;
    setTextCursor(cursor);
    updateParenthesesMatch();
    return true;
}

bool TextViewport::relativeLineNumbers() const
{
    return m_relativeLineNumbers;
}

void TextViewport::setRelativeLineNumbers(bool relative)
{
    if (m_relativeLineNumbers == relative)
        return;
    m_relativeLineNumbers = relative;
    updateRelativeOrigin();
    emit relativeLineNumbersChanged();
}

// The line the gutter counts from, pushed to the model rather than baked into
// each row: the caret moves far more often than the rows are rebuilt.
void TextViewport::updateRelativeOrigin()
{
    if (m_visibleRows)
        m_visibleRows->setRelativeOrigin(m_relativeLineNumbers ? cursorLine() : 0);
}

bool TextViewport::overwriteMode() const
{
    return m_overwriteMode;
}

void TextViewport::setOverwriteMode(bool overwrite)
{
    if (m_overwriteMode == overwrite)
        return;
    m_overwriteMode = overwrite;
    // The caret is drawn a character wide in this mode, so what is on screen
    // changes even though the text has not.
    emit cursorRectangleChanged();
    emit overwriteModeChanged();
}

QTextCursor TextViewport::cursorForPosition(const QPoint &point) const
{
    QTextDocument * const doc = m_document ? m_document->textDocument()->document() : nullptr;
    if (!doc)
        return {};
    const int position = positionAt(point.x(), point.y());
    if (position < 0)
        return {};
    QTextCursor cursor(doc);
    cursor.setPosition(position);
    return cursor;
}

void TextViewport::setTabStopDistance(qreal distance)
{
    const qreal spaceWidth = QFontMetricsF(m_font).horizontalAdvance(QLatin1Char(' '));
    if (spaceWidth <= 0 || distance <= 0)
        return;
    // Kept as a column count, which is what this view lays tabs out by; the
    // widget editors take pixels because QTextOption does.
    if (TextDocument * const doc = m_document ? m_document->textDocument() : nullptr) {
        TabSettingsData settings = doc->tabSettings();
        settings.m_tabSize = qMax(1, qRound(distance / spaceWidth));
        doc->setTabSettings(settings);
    }
}

bool TextViewport::centerOnScroll() const
{
    return m_centerOnScroll;
}

void TextViewport::setCenterOnScroll(bool center)
{
    if (m_centerOnScroll == center)
        return;
    m_centerOnScroll = center;
    ensureCursorVisible();
}

Core::IEditor *TextViewport::editor() const
{
    return m_editor;
}

void TextViewport::setEditor(Core::IEditor *editor)
{
    m_editor = editor;
}

SymbolRequests *TextViewport::symbolRequests() const
{
    if (!m_symbolRequests)
        const_cast<TextViewport *>(this)->m_symbolRequests = new SymbolRequests(
            const_cast<TextViewport *>(this));
    return m_symbolRequests;
}

void TextViewport::findUsages()
{
    const QTextCursor cursor = textCursor();
    if (!cursor.isNull())
        symbolRequests()->askForUsages(cursor);
}

void TextViewport::renameSymbolUnderCursor()
{
    const QTextCursor cursor = textCursor();
    if (cursor.isNull())
        return;
    // A name used in one function only is renamed here, without asking anyone:
    // every use of it is in this view. Anything else is a search, which is
    // what the relay is for.
    for (EditHandler * const handler : editHandlers()) {
        if (handler->handleRename())
            return;
    }
    symbolRequests()->askForRename(cursor);
}

void TextViewport::followTypeUnderCursor(bool inNextSplit)
{
    const QTextCursor cursor = textCursor();
    if (cursor.isNull())
        return;
    symbolRequests()->askForTypeAt(
        cursor,
        [self = QPointer<TextViewport>(this), inNextSplit](const Utils::Link &link) {
            if (self)
                self->openLink(link, inNextSplit);
        },
        true, inNextSplit);
}

void TextViewport::openCallHierarchy()
{
    symbolRequests()->askForCallHierarchy();
}

void TextViewport::deleteTo(QTextCursor::MoveOperation operation)
{
    if (!canEdit())
        return;
    moveCursor(operation, QTextCursor::KeepAnchor);
    Utils::MultiTextCursor cursors = multiTextCursor();
    if (cursors.isNull())
        return;
    cursors.removeSelectedText();
    setMultiTextCursor(cursors);
}

void TextViewport::deleteToCamelCase(bool forward)
{
    if (!canEdit())
        return;
    moveCamelCase(forward, QTextCursor::KeepAnchor);
    Utils::MultiTextCursor cursors = multiTextCursor();
    if (cursors.isNull())
        return;
    cursors.removeSelectedText();
    setMultiTextCursor(cursors);
}

void TextViewport::deleteEndOfLine()
{
    deleteTo(QTextCursor::EndOfLine);
}

void TextViewport::deleteStartOfLine()
{
    deleteTo(QTextCursor::StartOfLine);
}

void TextViewport::deleteEndOfWord()
{
    deleteTo(QTextCursor::NextWord);
}

void TextViewport::deleteStartOfWord()
{
    deleteTo(QTextCursor::PreviousWord);
}

void TextViewport::deleteEndOfWordCamelCase()
{
    deleteToCamelCase(true);
}

void TextViewport::deleteStartOfWordCamelCase()
{
    deleteToCamelCase(false);
}

void TextViewport::indent()
{
    TextDocument * const doc = m_document ? m_document->textDocument() : nullptr;
    if (!canEdit() || !doc)
        return;
    setMultiTextCursor(doc->indent(multiTextCursor()));
}

void TextViewport::unindent()
{
    TextDocument * const doc = m_document ? m_document->textDocument() : nullptr;
    if (!canEdit() || !doc)
        return;
    setMultiTextCursor(doc->unindent(multiTextCursor()));
}

void TextViewport::autoIndent()
{
    TextDocument * const doc = m_document ? m_document->textDocument() : nullptr;
    if (!canEdit() || !doc)
        return;
    Utils::MultiTextCursor cursors = multiTextCursor();
    if (cursors.isNull())
        return;
    TextEditor::autoIndent(cursors, doc);
    setMultiTextCursor(cursors);
}

void TextViewport::autoFormat()
{
    TextDocument * const doc = m_document ? m_document->textDocument() : nullptr;
    if (!canEdit() || !doc)
        return;
    QTextCursor cursor = textCursor();
    if (cursor.isNull())
        return;
    cursor.beginEditBlock();
    doc->autoFormat(cursor);
    cursor.endEditBlock();
}

void TextViewport::cleanWhitespace()
{
    TextDocument * const doc = m_document ? m_document->textDocument() : nullptr;
    if (!canEdit() || !doc)
        return;
    const QTextCursor cursor = textCursor();
    if (cursor.isNull())
        return;
    doc->cleanWhitespace(cursor);
}

void TextViewport::circularPaste()
{
    if (!canEdit())
        return;

    Internal::CircularClipboard * const history = Internal::CircularClipboard::instance();
    if (const QMimeData * const current = QGuiApplication::clipboard()->mimeData()) {
        history->collect(Internal::duplicateMimeData(current));
        history->toLastCollect();
    }

    // More than one thing to choose between, so it is a choice: offered
    // through the list quick fixes use, which is the same question - here are
    // some things that could be done, pick one.
    if (history->size() > 1) {
        requestQuickFixes(&Internal::clipboardAssistProvider());
        return;
    }

    if (const std::shared_ptr<const QMimeData> only = history->next()) {
        QGuiApplication::clipboard()->setMimeData(Internal::duplicateMimeData(only.get()));
        paste();
    }
}

void TextViewport::pasteWithoutFormat()
{
    paste();
}

void TextViewport::showContextMenu()
{
    emit contextMenuRequested();
}

bool TextViewport::canEdit() const
{
    TextDocument * const doc = m_document ? m_document->textDocument() : nullptr;
    return !m_readOnly && !(doc && doc->isFileReadOnly());
}

void TextViewport::joinLines()
{
    if (!canEdit())
        return;
    Utils::MultiTextCursor cursors = multiTextCursor();
    TextEditor::joinLines(cursors);
    setMultiTextCursor(cursors);
}

void TextViewport::addCaretAtNextMatch()
{
    const Utils::MultiTextCursor carets = multiTextCursor();
    const QList<QTextCursor> all = carets.cursors();
    if (all.isEmpty() || !all.first().hasSelection())
        return;

    const QString selected = all.first().selectedText();
    // A selection running over a line break is not a thing to look for again.
    if (selected.contains(QChar::ParagraphSeparator))
        return;

    // Every caret has to have the same text selected, or "the next one" is
    // not a question with an answer.
    const QString folded = selected.toCaseFolded();
    for (const QTextCursor &caret : all) {
        if (caret.selectedText().toCaseFolded() != folded)
            return;
    }

    QTextDocument * const text = all.first().document();
    if (!text)
        return;

    int searchFrom = all.last().selectionEnd();
    while (true) {
        const QTextCursor next = text->find(selected, searchFrom);
        if (next.isNull()) {
            // Nothing after the last caret: start again from the top, once.
            if (searchFrom == 0)
                return;
            searchFrom = 0;
            continue;
        }
        // Back at the first one, so every occurrence already has a caret.
        if (next.selectionStart() == all.first().selectionStart())
            return;
        Utils::MultiTextCursor added = carets;
        added.addCursor(next);
        setMultiTextCursor(added);
        return;
    }
}

void TextViewport::applyToEveryCaret(const std::function<void(QTextCursor &)> &edit)
{
    QList<QTextCursor> carets = m_extraCursors;
    carets.append(textCursor());
    if (carets.last().isNull())
        return;

    // By index, so that the main caret is still the last one afterwards
    // however the order came out.
    QList<int> order;
    for (int i = 0; i < carets.size(); ++i)
        order.append(i);
    std::sort(order.begin(), order.end(), [&carets](int a, int b) {
        return carets.at(a).position() > carets.at(b).position();
    });

    // One undo step for the lot: typing once should not take several undos to
    // take back merely because it happened at several carets.
    QTextCursor group = carets.first();
    group.beginEditBlock();
    for (const int i : std::as_const(order))
        edit(carets[i]);
    group.endEditBlock();

    setTextCursor(carets.takeLast());
    m_extraCursors = carets;
}

QTextBlock TextViewport::cursorBlock() const
{
    TextDocument * const doc = m_document ? m_document->textDocument() : nullptr;
    QTextDocument * const text = doc ? doc->document() : nullptr;
    return text ? text->findBlock(m_cursorPosition) : QTextBlock();
}

int TextViewport::cursorLine() const
{
    const QTextBlock block = cursorBlock();
    return block.isValid() ? block.blockNumber() + 1 : 0;
}

int TextViewport::cursorColumn() const
{
    const QTextBlock block = cursorBlock();
    return block.isValid() ? m_cursorPosition - block.position() + 1 : 0;
}

void TextViewport::selectWordAt(int position)
{
    QTextCursor cursor = textCursor();
    if (cursor.isNull())
        return;

    cursor.setPosition(position);
    cursor.select(QTextCursor::WordUnderCursor);

    // Between two spaces, WordUnderCursor selects the word *before* them, or
    // nothing at the start of a line. Take the whitespace itself instead,
    // which is what the widget editor corrects this to.
    const QTextDocument * const text = cursor.document();
    const QChar here = text->characterAt(position);
    const QChar before = text->characterAt(position - 1);
    if (here.isSpace() && before.isSpace() && here != QChar::ParagraphSeparator) {
        cursor.setPosition(position);
        if (before != QChar::ParagraphSeparator) {
            cursor.movePosition(QTextCursor::PreviousWord);
            cursor.movePosition(QTextCursor::EndOfWord);
        }
        cursor.movePosition(QTextCursor::NextWord, QTextCursor::KeepAnchor);
    }

    setTextCursor(cursor);
}

void TextViewport::selectLineAt(int position)
{
    QTextCursor cursor = textCursor();
    if (cursor.isNull())
        return;

    cursor.setPosition(position);
    cursor.select(QTextCursor::BlockUnderCursor);
    // BlockUnderCursor reaches back over the newline that ends the line above,
    // which reads as selecting two lines. Start where the text does.
    if (cursor.selectionStart() < cursor.block().position()) {
        cursor.setPosition(cursor.block().position());
        cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
    }
    setTextCursor(cursor);
}

void TextViewport::zoomBy(int steps)
{
    if (steps == 0 || !globalBehaviorSettings().scrollWheelZooming())
        return;

    // The same step the widget editor takes, and always at least one, so a
    // high-resolution wheel still does something per notch.
    const int step = steps * 10;
    globalFontSettings().increaseFontZoom(step != 0 ? step : (steps > 0 ? 1 : -1));
}

// The kind under which the bracket pair is highlighted, so that setting it
// replaces the previous pair and nobody else's ranges.
const char PARENTHESES_MATCH[] = "TextEditor.TextViewport.ParenthesesMatch";

void TextViewport::prepareSuggestion(const QTextBlock &block)
{
    TextDocument * const doc = m_document ? m_document->textDocument() : nullptr;
    TextSuggestion * const suggestion = TextBlockUserData::suggestion(block);
    if (!doc || !suggestion)
        return;

    // The same two things the widget editor does when one arrives: give it
    // the tab stops the text is drawn with, and the colours the scheme says
    // a suggestion is shown in.
    QTextOption option = suggestion->replacementDocument()->defaultTextOption();
    option.setTabStopDistance(doc->tabSettings().m_tabSize
                              * QFontMetricsF(m_font).horizontalAdvance(QLatin1Char(' ')));
    suggestion->replacementDocument()->setDefaultTextOption(option);
    TextBlockUserData::updateSuggestionFormats(block, doc->fontSettings());
    polish();
}

// The line a suggestion on \a block would put on the block's own row, or an
// invalid block when there is nothing to show: no suggestion, or a view that
// wraps and would have to find rows of its own for one. A suggestion of
// several lines puts its first line here and the rest below - see
// suggestionGhostsFor().
QTextBlock TextViewport::suggestionRowFor(const QTextBlock &block) const
{
    if (m_wrapping)
        return {};
    TextSuggestion * const suggestion = TextBlockUserData::suggestion(block);
    if (!suggestion)
        return {};
    QTextDocument * const replacement = suggestion->replacementDocument();
    if (!replacement || replacement->blockCount() < 1)
        return {};
    return replacement->firstBlock();
}

// The lines of a suggestion that do not fit on the block's own row. They are
// in no document, so they are drawn as ghost rows: rows between the file's
// own, which is what the inline diff shows a removed line with.
void TextViewport::rebuildSuggestionGhosts()
{
    QList<GhostRows> ghosts;
    if (m_suggestionBlock.isValid() && !m_wrapping) {
        if (TextSuggestion * const suggestion
            = TextBlockUserData::suggestion(m_suggestionBlock)) {
            QStringList rest;
            for (QTextBlock line = suggestion->replacementDocument()->firstBlock().next();
                 line.isValid(); line = line.next()) {
                rest.append(line.text());
            }
            if (!rest.isEmpty()) {
                // Below the line the suggestion is about, which is above the
                // row after it.
                ghosts.append({rowOfBlock(m_suggestionBlock) + 1, rest,
                               GhostRows::Kind::Suggestion});
            }
        }
    }

    if (ghosts == m_suggestionGhosts)
        return;
    m_suggestionGhosts = ghosts;
    rebuildGaps();
    polish();
}

void TextViewport::updateDocumentSelections()
{
    TextDocument * const doc = m_document ? m_document->textDocument() : nullptr;
    if (!doc)
        return;

    // Every kind the document knows, including the ones since emptied: an
    // emptied kind has to clear what was drawn for it.
    const QList<Utils::Id> kinds = doc->extraSelectionKinds();
    for (const Utils::Id &kind : kinds) {
        QList<Highlight> highlights;
        const QList<TextDocument::ExtraSelection> selections = doc->extraSelections(kind);
        for (const TextDocument::ExtraSelection &selection : selections) {
            if (selection.cursor.hasSelection()) {
                highlights.append({selection.cursor.selectionStart(),
                                   selection.cursor.selectionEnd(),
                                   selection.format});
            }
        }
        setHighlights(kind, highlights);
    }
}

QVariantList TextViewport::refactorMarkers() const
{
    TextDocument * const doc = m_document ? m_document->textDocument() : nullptr;
    if (!doc)
        return {};
    QVariantList markers;
    const RefactorMarkers all = doc->refactorMarkers();
    for (const RefactorMarker &marker : all) {
        if (marker.cursor.isNull())
            continue;
        const int position = marker.cursor.selectionEnd();
        // Where it is drawn, worked out here because only the view knows: a
        // marker off screen has an empty rectangle and the form skips it.
        const QRectF where = rectangleAt(position);
        markers.append(QVariantMap{{"position", position},
                                   {"toolTip", marker.tooltip},
                                   {"x", where.x()},
                                   {"y", where.y()},
                                   {"height", where.height()},
                                   {"onScreen", !where.isEmpty()}});
    }
    return markers;
}

bool TextViewport::applyRefactorMarkerAt(int position)
{
    TextDocument * const doc = m_document ? m_document->textDocument() : nullptr;
    if (!doc)
        return false;
    const RefactorMarker marker = doc->refactorMarkerAt(position);
    if (!marker.isValid() || !marker.callback)
        return false;
    // The editor rather than this item: what the marker does is the reader's
    // action in a file, and the producer should not have to know which view
    // they were looking at.
    marker.callback(Internal::editorForViewport(this));
    return true;
}

void TextViewport::updateParenthesesMatch()
{
    TextDocument * const doc = m_document ? m_document->textDocument() : nullptr;
    QTextDocument * const text = doc ? doc->document() : nullptr;
    // Either setting is a reason to look: with only the animation on there is
    // nothing to draw but still something to pulse, which is what the widget
    // editor does too.
    const bool highlight = displaySettings().data().m_highlightMatchingParentheses;
    const bool animate = displaySettings().data().m_animateMatchingParentheses;
    if (!text || !(highlight || animate)) {
        setHighlights(PARENTHESES_MATCH, {});
        return;
    }

    // The caret alone, not the selection: what is being asked is which bracket
    // the caret is beside, and textCursor() answers with the selection when
    // there is one.
    QTextCursor caret(text);
    caret.setPosition(qBound(0, m_cursorPosition, text->characterCount() - 1));

    QTextCursor backward = caret;
    QTextCursor forward = caret;
    const TextBlockUserData::MatchType backwardType
        = TextBlockUserData::matchCursorBackward(&backward);
    const TextBlockUserData::MatchType forwardType
        = TextBlockUserData::matchCursorForward(&forward);

    const FontSettingsData &fonts = doc->fontSettings();
    const QTextCharFormat matched = fonts.toTextCharFormat(C_PARENTHESES);
    // C_PARENTHESES_MISMATCH is an overlay category, so its format is nearly
    // empty by design - the same trap as the search results.
    QTextCharFormat mismatched = fonts.toTextCharFormat(C_PARENTHESES_MISMATCH);
    if (mismatched.background().style() == Qt::NoBrush
        || !mismatched.background().color().isValid()) {
        mismatched.setBackground(Utils::creatorColor(Utils::Theme::TextEditor_SearchResult_ScrollBarColor));
    }

    QList<Highlight> found;
    // Which character to pulse, if any: the far half of the pair the caret is
    // beside. Only one of the two, and only for a real match.
    int animatePosition = -1;
    const auto add = [&](const QTextCursor &cursor, TextBlockUserData::MatchType type, bool isForward) {
        if (!cursor.hasSelection())
            return;
        if (type == TextBlockUserData::Mismatch) {
            found.append({cursor.selectionStart(), cursor.selectionEnd(), mismatched});
            return;
        }
        // The two brackets themselves rather than everything between them: a
        // whole function body in the parenthesis colour is not a hint.
        found.append({cursor.selectionStart(), cursor.selectionStart() + 1, matched});
        found.append({cursor.selectionEnd() - 1, cursor.selectionEnd(), matched});
        if (animate && cursor.block().isVisible())
            animatePosition = isForward ? cursor.selectionEnd() - 1 : cursor.selectionStart();
    };
    add(backward, backwardType, false);
    add(forward, forwardType, true);

    // Not again for a bracket that was already marked: the caret moving along
    // the same pair is one arrival, not one per keystroke.
    if (animatePosition >= 0) {
        const QList<Highlight> before = m_highlights.value(PARENTHESES_MATCH);
        for (const Highlight &highlighted : before) {
            if (highlighted.start == animatePosition || highlighted.end - 1 == animatePosition) {
                animatePosition = -1;
                break;
            }
        }
    }

    setHighlights(PARENTHESES_MATCH, highlight ? found : QList<Highlight>());

    // Left for the end of the layout: this runs before the rows are built, so
    // asking now where the bracket is on screen answers nowhere.
    if (animatePosition >= 0) {
        m_pendingPulse = PendingPulse{animatePosition, matched.foreground().color(),
                                      matched.background().color()};
    }
}

void TextViewport::setHighlights(Utils::Id kind, const QList<Highlight> &highlights,
                                 const QColor &onScrollBar)
{
    if (onScrollBar.isValid())
        m_highlightsOnScrollBar.insert(kind, onScrollBar);
    else
        m_highlightsOnScrollBar.remove(kind);

    if (highlights.isEmpty()) {
        if (m_highlights.remove(kind) == 0)
            return;
    } else if (m_highlights.value(kind) == highlights) {
        return;
    } else {
        QList<Highlight> sorted = highlights;
        // Sorted so that a line can find the ranges that reach it without
        // looking at the ones before, which is what keeps highlighting every
        // match in a large file O(what is on screen).
        std::sort(sorted.begin(), sorted.end(),
                  [](const Highlight &a, const Highlight &b) { return a.start < b.start; });
        m_highlights.insert(kind, sorted);
    }
    polish();
    update();
}

QList<TextViewport::Highlight> TextViewport::highlights(Utils::Id kind) const
{
    return m_highlights.value(kind);
}

int TextViewport::cursorDisplayColumn() const
{
    TextDocument * const doc = m_document ? m_document->textDocument() : nullptr;
    const QTextBlock block = cursorBlock();
    if (!doc || !block.isValid())
        return 0;
    return doc->tabSettings().columnAt(block.text(), m_cursorPosition - block.position()) + 1;
}

int TextViewport::selectedCharacterCount() const
{
    if (m_selectionStart < 0 || m_selectionEnd < 0)
        return 0;
    return qAbs(m_selectionEnd - m_selectionStart);
}

QString TextViewport::selectedText() const
{
    const QTextCursor cursor = textCursor();
    return cursor.isNull() || !cursor.hasSelection() ? QString() : selectedPlainText(cursor);
}

void TextViewport::removeSelectedText()
{
    TextDocument * const doc = m_document ? m_document->textDocument() : nullptr;
    QTextDocument * const document = doc ? doc->document() : nullptr;
    const int from = qMin(m_selectionStart, m_selectionEnd);
    const int to = qMax(m_selectionStart, m_selectionEnd);
    if (!document || m_readOnly || doc->isFileReadOnly() || from < 0 || to <= from)
        return;

    QTextCursor cursor(document);
    cursor.setPosition(from);
    cursor.setPosition(to, QTextCursor::KeepAnchor);
    cursor.removeSelectedText();
    setSelectionStart(from);
    setSelectionEnd(from);
    setCursorPosition(from);
}

void TextViewport::dropText(const QString &text, qreal x, qreal y, bool moveFromSelection)
{
    TextDocument * const doc = m_document ? m_document->textDocument() : nullptr;
    QTextDocument * const document = doc ? doc->document() : nullptr;
    if (!document || text.isEmpty() || m_readOnly || doc->isFileReadOnly())
        return;

    const int at = positionAt(x, y);
    if (at < 0)
        return;

    const int from = qMin(m_selectionStart, m_selectionEnd);
    const int to = qMax(m_selectionStart, m_selectionEnd);
    const bool moving = moveFromSelection && from >= 0 && to > from;
    // Dropping a selection back inside itself moves it nowhere, and taking the
    // text out first would make it a deletion.
    if (moving && at >= from && at <= to)
        return;

    QTextCursor cursor(document);
    cursor.beginEditBlock();
    if (moving) {
        // Remove first, then insert where that leaves the drop point: text
        // taken from above it moves it up by as much.
        const int shift = at > to ? to - from : 0;
        cursor.setPosition(from);
        cursor.setPosition(to, QTextCursor::KeepAnchor);
        cursor.removeSelectedText();
        cursor.setPosition(at - shift);
    } else {
        cursor.setPosition(at);
    }

    doc->insertWithIndentation(cursor, text, true);
    cursor.endEditBlock();

    // What just arrived is what is highlighted, which is what a drop onto the
    // widget editor leaves behind too.
    setSelectionStart(cursor.anchor());
    setSelectionEnd(cursor.position());
    setCursorPosition(cursor.position());
}

// Worked out once per layout and kept, rather than answered afresh every time
// the bar asks: a document with a few thousand search results in it would
// otherwise rebuild every mark on the bar for each scroll of a wheel.
void TextViewport::updateScrollBarHighlights()
{
    TextDocument * const doc = m_document ? m_document->textDocument() : nullptr;
    const qreal height = contentHeight();
    if (!doc || !displaySettings().scrollBarHighlights() || height <= 0 || m_lineHeight <= 0) {
        if (!m_scrollBarHighlights.isEmpty()) {
            m_scrollBarHighlights.clear();
            emit scrollBarHighlightsChanged();
        }
        return;
    }

    QVariantList highlights;
    QSet<QPair<int, QRgb>> already;
    const auto add = [&](const QTextBlock &block, const QColor &colour) {
        // A folded-away line has no row of its own, so there is nowhere on the
        // bar to point at.
        if (!block.isValid() || !block.isVisible() || !colour.isValid())
            return;
        const int row = rowOfBlock(block);
        const QPair<int, QRgb> where{row, colour.rgba()};
        if (already.contains(where))
            return;
        already.insert(where);
        highlights.append(
            QVariantMap{{QStringLiteral("position"), yOfRow(row) / height}, {QStringLiteral("color"), colour}});
    };

    // The line the caret is on.
    add(cursorBlock(), Utils::creatorColor(Utils::Theme::TextEditor_CurrentLine_ScrollBarColor));

    // And every mark that says what colour to draw it in. One without is one
    // the widget editor leaves off the bar too.
    const TextMarks marks = doc->marks();
    for (TextMark * const mark : marks) {
        if (!mark->isVisible() || !mark->color().has_value())
            continue;
        add(doc->document()->findBlockByNumber(mark->lineNumber() - 1),
            Utils::creatorColor(*mark->color()));
    }

    // What is highlighted in the text and asked to show on the bar as well -
    // search results, above all. Once per row: a hundred matches on one line
    // are one place to scroll to.
    for (auto kind = m_highlightsOnScrollBar.cbegin(); kind != m_highlightsOnScrollBar.cend();
         ++kind) {
        const QList<Highlight> found = m_highlights.value(kind.key());
        for (const Highlight &highlight : found)
            add(doc->document()->findBlock(highlight.start), kind.value());
    }

    if (highlights == m_scrollBarHighlights)
        return;
    m_scrollBarHighlights = highlights;
    emit scrollBarHighlightsChanged();
}

int TextViewport::contentWidthPercent() const
{
    return marginSettings().centerEditorContentWidthPercent();
}

QString TextViewport::tabSettingsLabel() const
{
    TextDocument * const doc = m_document ? m_document->textDocument() : nullptr;
    // Hidden for a read-only file for the same reason as the line ending
    // beside it: there is nothing to be done about the indentation of a file
    // that cannot be written.
    if (!doc || !displaySettings().displayTabSettings() || m_readOnly
        || doc->isFileReadOnly()) {
        return {};
    }

    const TabSettingsData tabs = doc->tabSettings();
    const QString policy = tabs.m_tabPolicy == TabSettingsData::TabsOnlyTabPolicy
                               ? Tr::tr("Tabs")
                               : Tr::tr("Spaces");
    return QString("%1: %2").arg(policy).arg(tabs.m_indentSize);
}

int TextViewport::indentSize() const
{
    TextDocument * const doc = m_document ? m_document->textDocument() : nullptr;
    return doc ? doc->tabSettings().m_indentSize : 0;
}

void TextViewport::modifyTabSettings(const std::function<void(TabSettingsData &)> &modify)
{
    TextDocument * const doc = m_document ? m_document->textDocument() : nullptr;
    if (!doc)
        return;
    TabSettingsData tabs = doc->tabSettings();
    tabs.m_autoDetect = false;
    modify(tabs);
    doc->setTabSettings(tabs);
}

void TextViewport::setTabPolicyIsSpaces(bool spaces)
{
    modifyTabSettings([spaces](TabSettingsData &tabs) {
        tabs.m_tabPolicy = spaces ? TabSettingsData::SpacesOnlyTabPolicy
                                  : TabSettingsData::TabsOnlyTabPolicy;
    });
}

void TextViewport::setIndentSize(int size)
{
    modifyTabSettings([size](TabSettingsData &tabs) { tabs.m_indentSize = size; });
}

void TextViewport::detectTabSettings()
{
    modifyTabSettings([](TabSettingsData &tabs) { tabs.m_autoDetect = true; });
}

QString TextViewport::fileLineEnding() const
{
    TextDocument * const doc = m_document ? m_document->textDocument() : nullptr;
    // The widget editor hides this for a read-only file too: there is nothing
    // to be done about the line endings of a file that cannot be written.
    if (!doc || !displaySettings().displayFileLineEnding() || m_readOnly
        || doc->isFileReadOnly()) {
        return {};
    }

    switch (doc->lineTerminationMode()) {
    case Utils::TextFileFormat::LFLineTerminator:
        return Tr::tr("LF");
    case Utils::TextFileFormat::CRLFLineTerminator:
        return Tr::tr("CRLF");
    default:
        return {};
    }
}

void TextViewport::setFileLineEndingIsWindows(bool windows)
{
    TextDocument * const doc = m_document ? m_document->textDocument() : nullptr;
    if (!doc)
        return;

    const Utils::TextFileFormat::LineTerminationMode wanted
        = windows ? Utils::TextFileFormat::CRLFLineTerminator
                  : Utils::TextFileFormat::LFLineTerminator;
    if (doc->lineTerminationMode() == wanted)
        return;

    doc->setLineTerminationMode(wanted);
    doc->document()->setModified(true);
    emit fileFormatChanged();
}

// This view as something a suggestion can be applied to. Nothing a widget
// does: where the caret is, and where to put what is left over.
class ViewportSuggestionTarget final : public SuggestionTarget
{
public:
    explicit ViewportSuggestionTarget(TextViewport *view)
        : m_view(view)
    {}

    QTextCursor textCursor() const override { return m_view->textCursor(); }
    QTextDocument *document() const override
    {
        const QTextCursor cursor = m_view->textCursor();
        return cursor.isNull() ? nullptr : cursor.document();
    }
    void insertSuggestion(std::unique_ptr<TextSuggestion> &&suggestion) override
    {
        m_view->insertSuggestion(std::move(suggestion));
    }

private:
    TextViewport *m_view = nullptr;
};

TextViewport::SuggestionBlocker TextViewport::blockSuggestions()
{
    if (!m_suggestionBlocker) {
        // A token nobody else holds yet. The deleter does nothing: what is
        // being counted is the holders, not a resource.
        m_suggestionBlocker = std::shared_ptr<void>(this, [](void *) {});
    }
    if (!suggestionsBlocked())
        clearSuggestion();
    return m_suggestionBlocker;
}

bool TextViewport::suggestionsBlocked() const
{
    return m_suggestionBlocker.use_count() > 1;
}

void TextViewport::insertSuggestion(std::unique_ptr<TextSuggestion> &&suggestion)
{
    clearSuggestion();
    // Asked for while somebody is holding them off - a modal editing mode
    // outside insert mode, say. Dropped rather than queued, which is what the
    // widget editor does with it.
    if (suggestionsBlocked())
        return;
    const QTextCursor cursor = textCursor();
    if (cursor.isNull())
        return;
    m_suggestionBlock = cursor.block();
    TextBlockUserData::insertSuggestion(m_suggestionBlock, std::move(suggestion));
    prepareSuggestion(m_suggestionBlock);
    rebuildSuggestionGhosts();
    emit suggestionChanged();
}

void TextViewport::clearSuggestion()
{
    if (!m_suggestionBlock.isValid())
        return;
    TextBlockUserData::clearSuggestion(m_suggestionBlock);
    m_suggestionBlock = QTextBlock();
    rebuildSuggestionGhosts();
    polish();
    emit suggestionChanged();
}

// A suggestion stays while what is typed still leads to it, and goes when it
// does not - so it is never a picture of something that could no longer
// happen. The same rule the widget editor follows.
void TextViewport::updateSuggestion()
{
    if (!m_suggestionBlock.isValid())
        return;
    const QTextCursor cursor = textCursor();
    if (!cursor.isNull() && cursor.block() == m_suggestionBlock) {
        TextSuggestion * const suggestion = TextBlockUserData::suggestion(m_suggestionBlock);
        if (QTC_GUARD(suggestion)) {
            const int position = cursor.position();
            if (position >= suggestion->currentPosition()) {
                suggestion->setCurrentPosition(position);
                ViewportSuggestionTarget target(this);
                if (suggestion->filterSuggestions(target)) {
                    if (TextDocument * const doc = textDocument()) {
                        TextBlockUserData::updateSuggestionFormats(
                            m_suggestionBlock, doc->fontSettings());
                    }
                    rebuildSuggestionGhosts();
                    polish();
                    return;
                }
            }
        }
    }
    clearSuggestion();
}

TextSuggestion *TextViewport::currentSuggestion() const
{
    if (!m_suggestionBlock.isValid())
        return nullptr;
    return TextBlockUserData::suggestion(m_suggestionBlock);
}

void TextViewport::applySuggestion()
{
    if (!canEdit())
        return;
    if (TextSuggestion * const suggestion = currentSuggestion())
        suggestion->apply();
}

void TextViewport::applySuggestionWord()
{
    if (!canEdit())
        return;
    if (TextSuggestion * const suggestion = currentSuggestion()) {
        ViewportSuggestionTarget target(this);
        suggestion->applyWord(target);
    }
}

void TextViewport::applySuggestionLine()
{
    if (!canEdit())
        return;
    if (TextSuggestion * const suggestion = currentSuggestion()) {
        ViewportSuggestionTarget target(this);
        suggestion->applyLine(target);
    }
}

void TextViewport::copyWithHtml()
{
    const Utils::MultiTextCursor cursors = multiTextCursor();
    if (cursors.isNull() || !cursors.hasSelection())
        return;

    const QTextCursor main = textCursor();
    auto * const mime = new QMimeData;
    mime->setText(selectedPlainText(main));
    // Every row this view lays out is one it shows, so there is nothing to
    // leave out - the widget editor's question of whether a line is visible
    // is for views that hide some.
    mime->setHtml(TextEditor::htmlForSelection(cursors, main.document(), movementLayout(),
                                               [](int) { return true; }));
    QGuiApplication::clipboard()->setMimeData(mime);
}

void TextViewport::switchUtf8Bom()
{
    TextDocument * const doc = m_document ? m_document->textDocument() : nullptr;
    if (!canEdit() || !doc)
        return;
    doc->switchUtf8Bom();
}

void TextViewport::selectEncoding()
{
    TextDocument * const doc = m_document ? m_document->textDocument() : nullptr;
    if (!doc)
        return;
    applyEncodingChoice(Core::askForCodec(doc));
}

void TextViewport::applyEncodingChoice(const Core::CodecSelectorResult &choice)
{
    TextDocument * const doc = m_document ? m_document->textDocument() : nullptr;
    if (!doc)
        return;

    switch (choice.action) {
    case Core::CodecSelectorResult::Reload:
        // Reading the same bytes as something else: what is on screen changes,
        // and what is on disk does not.
        if (const Utils::Result<> reloaded = doc->reload(choice.encoding); !reloaded)
            Core::MessageManager::writeDisrupting(reloaded.error());
        break;
    case Core::CodecSelectorResult::Save:
        // The other direction: the text stays, the bytes change, and that only
        // reaches the file when it is saved.
        doc->setEncoding(choice.encoding);
        Core::EditorManager::saveDocument(doc);
        break;
    case Core::CodecSelectorResult::Cancel:
        return;
    }
    emit fileFormatChanged();
}

QString TextViewport::fileEncoding() const
{
    TextDocument * const doc = m_document ? m_document->textDocument() : nullptr;
    if (!doc || !displaySettings().displayFileEncoding())
        return {};
    return doc->encoding().displayName();
}

void TextViewport::gotoLine(int line, int column, bool centerLine)
{
    TextDocument * const doc = m_document ? m_document->textDocument() : nullptr;
    QTextDocument * const text = doc ? doc->document() : nullptr;
    if (!text)
        return;

    const QTextBlock block = text->findBlockByNumber(
        qBound(0, qMin(line, text->blockCount()) - 1, text->blockCount() - 1));
    if (!block.isValid())
        return;

    TextBlockUserData::unfoldTo(block);

    QTextCursor cursor(block);
    if (column >= block.length()) {
        cursor.movePosition(QTextCursor::EndOfBlock);
    } else if (column > 0) {
        cursor.movePosition(QTextCursor::Right, QTextCursor::MoveAnchor, column);
    } else {
        // No column asked for means the line rather than its margin, so this
        // lands on the first thing on it - what a search result or a compiler
        // message is pointing at.
        int position = cursor.position();
        while (text->characterAt(position).category() == QChar::Separator_Space)
            ++position;
        cursor.setPosition(position);
    }

    setSelectionStart(-1);
    setSelectionEnd(-1);
    setCursorPosition(cursor.position());

    // The row the line starts on, which is not its number once anything above
    // it is folded away or wrapped over several rows.
    const int row = rowOfBlock(block);
    const qreal top = yOfRow(row);
    const qreal start = m_scrollY;
    if (centerLine)
        setScrollY(top - (height() - rowSpan(row)) / 2);
    else if (top < m_scrollY || top + rowSpan(row) > m_scrollY + height())
        setScrollY(top < m_scrollY ? top : top + rowSpan(row) - height());

    // Where it ended up rather than where it was asked to go: setScrollY
    // refuses to leave the document, and the animation has to end somewhere
    // real.
    const qreal end = m_scrollY;
    if (end == start || !displaySettings().animateNavigationWithinFile())
        return;

    if (m_navigationAnimation)
        m_navigationAnimation->stop();

    // Two halves, with a gap in the middle for a jump longer than the setting
    // allows: it sets off, skips, and settles, which is what makes a long
    // jump's direction readable. The widget editor's arithmetic exactly.
    const int most = displaySettings().animateWithinFileTimeMax();
    const int steps = qMax(-most, qMin(most, qRound(end - start)));
    // Four frames on a sixty hertz monitor, so that even a short jump is seen.
    const int duration = qMax(4 * 1000 / 60, qAbs(steps));

    auto * const group = new QSequentialAnimationGroup(this);
    // Starting the group puts the scroll back where it set off from: a
    // QPropertyAnimation applies its start value as it starts, so saying so
    // again here would be a line that does nothing.
    auto * const setOff = new QPropertyAnimation(this, "scrollY", group);
    setOff->setEasingCurve(QEasingCurve::InExpo);
    setOff->setStartValue(start);
    setOff->setEndValue(start + steps / 2.0);
    setOff->setDuration(duration / 2);
    group->addAnimation(setOff);
    auto * const settle = new QPropertyAnimation(this, "scrollY", group);
    settle->setEasingCurve(QEasingCurve::OutExpo);
    settle->setStartValue(end - steps / 2.0);
    settle->setEndValue(end);
    settle->setDuration(duration / 2);
    group->addAnimation(settle);
    m_navigationAnimation = group;
    group->start(QAbstractAnimation::DeleteWhenStopped);
}

void TextViewport::toggleFold(int lineNumber)
{
    TextDocument * const doc = m_document ? m_document->textDocument() : nullptr;
    if (!doc)
        return;
    QTextDocument * const text = doc->document();
    const QTextBlock block = text->findBlockByNumber(lineNumber - 1);
    if (!block.isValid() || !TextBlockUserData::canFold(block))
        return;

    // Looking the line up again after the wait, rather than keeping the block,
    // means it does not matter what the highlighter did to the document.
    if (waitsForHighlighter([this, lineNumber] { toggleFold(lineNumber); }))
        return;

    TextBlockUserData::doFoldOrUnfold(block, TextBlockUserData::isFolded(block));
    foldingChanged();
}

bool TextViewport::waitsForHighlighter(const std::function<void()> &retry)
{
    TextDocument * const doc = m_document ? m_document->textDocument() : nullptr;
    SyntaxHighlighter * const highlighter = doc ? doc->syntaxHighlighter() : nullptr;
    if (!highlighter || highlighter->syntaxHighlighterUpToDate())
        return false;
    connect(highlighter, &SyntaxHighlighter::finished, this, retry, Qt::SingleShotConnection);
    return true;
}

void TextViewport::foldingChanged()
{
    TextDocument * const doc = m_document ? m_document->textDocument() : nullptr;
    QTextDocument * const text = doc ? doc->document() : nullptr;
    if (!text)
        return;
    auto * const layout = qobject_cast<TextDocumentLayout *>(text->documentLayout());
    QTC_ASSERT(layout, return);
    layout->requestUpdate();
    layout->emitDocumentSizeChanged();

    // A caret left inside what was just folded would type into text nobody can
    // see, so it comes out to the end of the line that swallowed it.
    const QTextBlock cursorBlock = text->findBlock(m_cursorPosition);
    if (cursorBlock.isValid() && !cursorBlock.isVisible()) {
        const QTextBlock owner = TextEditor::blockToUnfold(cursorBlock);
        if (owner.isValid())
            setCursorPosition(owner.position() + owner.length() - 1);
    }
}

void TextViewport::foldCurrentBlock(bool recursive)
{
    if (waitsForHighlighter([this, recursive] { foldCurrentBlock(recursive); }))
        return;
    const QTextCursor cursor = textCursor();
    if (cursor.isNull())
        return;
    const QTextBlock block = TextEditor::blockToFold(cursor.block());
    if (!block.isValid())
        return;
    TextBlockUserData::doFoldOrUnfold(block, false, recursive);
    foldingChanged();
}

void TextViewport::unfoldCurrentBlock(bool recursive)
{
    if (waitsForHighlighter([this, recursive] { unfoldCurrentBlock(recursive); }))
        return;
    const QTextCursor cursor = textCursor();
    if (cursor.isNull())
        return;
    const QTextBlock block = TextEditor::blockToUnfold(cursor.block());
    if (!block.isValid())
        return;
    TextBlockUserData::doFoldOrUnfold(block, true, recursive);
    foldingChanged();
}

void TextViewport::toggleFoldAll()
{
    if (waitsForHighlighter([this] { toggleFoldAll(); }))
        return;
    TextDocument * const doc = m_document ? m_document->textDocument() : nullptr;
    QTextDocument * const text = doc ? doc->document() : nullptr;
    if (!text)
        return;
    const bool unfold = !TextEditor::hasUnfoldedBlocks(text);
    for (QTextBlock block = text->firstBlock(); block.isValid(); block = block.next()) {
        if (TextBlockUserData::canFold(block))
            TextBlockUserData::doFoldOrUnfold(block, unfold);
    }
    foldingChanged();
}

// The line under \a y in this item's coordinates, counting from one the way
// the marks do. -1 when nothing is laid out there.
static int lineNumberAt(const TextViewport *view, TextDocument *document, qreal y)
{
    if (!document)
        return -1;
    const int position = view->positionAt(0, y);
    if (position < 0)
        return -1;
    return document->document()->findBlock(position).blockNumber() + 1;
}

void TextViewport::clickMarkColumn(qreal y, Qt::KeyboardModifiers modifiers)
{
    TextDocument * const doc = m_document ? m_document->textDocument() : nullptr;
    const int line = lineNumberAt(this, doc, y);
    if (line < 1)
        return;

    // The marks already there get the click first - that is how clicking a
    // breakpoint's own icon disables it - and only what none of them wanted
    // becomes a request for a new one.
    if (doc->clickMark(line))
        return;
    doc->requestMark(line, modifiers);
}

bool TextViewport::prepareMarkMenu(qreal y)
{
    TextDocument * const doc = m_document ? m_document->textDocument() : nullptr;
    const int line = lineNumberAt(this, doc, y);
    if (line < 1)
        return false;

    // A fresh one each time: the actions are built for this line, and the
    // model lists what the menu holds rather than owning any of it.
    m_markMenu = std::make_unique<QMenu>();
    doc->fillMarkContextMenu(line, m_markMenu.get());
    m_markActions.setActions(m_markMenu->actions());
    return !m_markMenu->isEmpty();
}

void TextViewport::highlightScopeAt(qreal y)
{
    TextDocument * const doc = m_document ? m_document->textDocument() : nullptr;
    if (!doc)
        return;
    const int position = positionAt(0, y);
    if (position < 0) {
        clearScopeHighlight();
        return;
    }
    setScopeBlock(doc->document()->findBlock(position).blockNumber());
}

void TextViewport::clearScopeHighlight()
{
    setScopeBlock(-1);
}

void TextViewport::setScopeBlock(int blockNumber)
{
    TextDocument * const doc = m_document ? m_document->textDocument() : nullptr;
    if (!doc)
        return;

    const int revision = doc->document()->revision();
    if (m_scopeBlock == blockNumber && m_scopeRevision == revision)
        return;
    m_scopeBlock = blockNumber;
    m_scopeRevision = revision;

    BlockNesting nesting;
    if (blockNumber >= 0) {
        QTextBlock block = doc->document()->findBlockByNumber(blockNumber);
        // A line that opens a fold is taken as being inside what it opens, so
        // that hovering a marker lights up the scope the marker names rather
        // than the one around it.
        if (block.isValid() && block.next().isValid()
            && TextBlockUserData::foldingIndent(block.next())
                   > TextBlockUserData::foldingIndent(block)) {
            block = block.next();
        }
        // Where a level starts, in pixels. The widget asks its layout for the
        // first non-space character's x; here the indent is counted in
        // columns and multiplied out, which is what places the indent guides
        // too, so a band and a guide agree.
        const qreal spaceWidth = QFontMetricsF(m_font).horizontalAdvance(QLatin1Char(' '));
        const auto tabs = doc->tabSettings();
        nesting = blockNestingAt(block, [&tabs, spaceWidth](const QTextBlock &b) {
            return b.isValid() ? qRound(indentDepthForBlock(b, tabs) * spaceWidth) : 0;
        });
    }

    if (m_scopeNesting == nesting)
        return;
    m_scopeNesting = nesting;
    polish();
    update();
}

int TextViewport::positionAt(qreal x, qreal y) const
{
    if (m_lines.empty() || m_lineHeight <= 0)
        return -1;

    // Clamped to what is laid out rather than to the document: a drag that
    // leaves the viewport should select to the edge of it, not jump to the end
    // of the file.
    const int index = qBound(0, rowAtY(y + m_scrollY) - m_firstVisibleLine,
                             int(m_lines.size()) - 1);
    const Line &line = m_lines.at(index);
    const QTextLine textLine = line.layout->lineAt(0);
    if (!textLine.isValid())
        return line.blockPosition;

    return line.blockPosition + textLine.xToCursor(x - line.at.x());
}

void TextViewport::documentChangedInternal()
{
    // The QTextDocument only exists once the file has opened, and it is a
    // different one each time it reopens, so the connection to it is made here
    // rather than in setDocument(). Nothing is cached across the signal - the
    // whole visible window is laid out again - so this needs none of the
    // ordering care that a widget editor's contentsChange handler does.
    TextDocument * const doc = m_document ? m_document->textDocument() : nullptr;
    QTextDocument * const text = doc ? doc->document() : nullptr;
    if (text != m_connectedDocument) {
        if (m_connectedDocument)
            disconnect(m_connectedDocument, nullptr, this, nullptr);
        // A different file has different lines in it, so the widest one seen
        // so far means nothing any more.
        m_contentWidth = 0;
        m_connectedDocument = text;
        if (text) {
            connect(text, &QTextDocument::contentsChanged, this, [this] {
                // What was typed decides whether the suggestion still
                // describes anything - including the case where taking it is
                // what changed the text.
                updateSuggestion();
                polish();
                update();
            });
        }
    }

    // Marks arrive and go without the text changing: an error appears while
    // the file sits there. The widget gutter is redrawn from
    // TextDocumentLayout::updateExtraArea, so this listens to the same thing,
    // and to the document for the one that says a mark has gone.
    if (doc != m_connectedMarkSource) {
        if (m_connectedMarkSource) {
            disconnect(m_connectedMarkSource, nullptr, this, nullptr);
            if (auto *layout = qobject_cast<TextDocumentLayout *>(
                    m_connectedMarkSource->document()->documentLayout())) {
                disconnect(layout, &TextDocumentLayout::updateExtraArea, this, nullptr);
            }
        }
        m_connectedMarkSource = doc;
        if (doc) {
            connect(doc, &TextDocument::markRemoved, this, [this] {
                polish();
                update();
            });
            // The encoding and the line endings are part of what "the document
            // changed" covers - reopening with another encoding says so here.
            connect(doc, &Core::IDocument::changed, this, &TextViewport::fileFormatChanged);
            connect(doc, &TextDocument::tabSettingsChanged, this,
                    &TextViewport::fileFormatChanged);
            // Only a request to lay out again: the ranges themselves are
            // read in updatePolish(), because a cursor moves when the text
            // above it changes and nothing announces that separately.
            connect(doc, &TextDocument::extraSelectionsChanged, this, [this] {
                polish();
                update();
            });
            // A marker appears while the file sits there - a quick fix the
            // analyser has just worked out - so the form is told even when
            // nothing has been laid out again.
            connect(doc, &TextDocument::refactorMarkersChanged, this,
                    &TextViewport::refactorMarkersChanged);
            // The font, the colours and the zoom are all read in updatePolish()
            // from the document's font settings, and nothing else makes this
            // lay out again - so without this an open file keeps the size and
            // the scheme it was opened with.
            connect(doc, &TextDocument::fontSettingsChanged, this, [this] {
                polish();
                update();
            });
            // A document opened after a zoom starts at the size everything
            // else is already showing.
            applyGlobalFontSettings();
            if (auto *layout = qobject_cast<TextDocumentLayout *>(
                    doc->document()->documentLayout())) {
                connect(layout, &TextDocumentLayout::updateExtraArea, this, [this] {
                    polish();
                    update();
                });
                // The *first* mark on a document does not go through
                // requestExtraAreaUpdate() at all - TextDocument::addMark()
                // calls scheduleUpdate() instead, because the layout has to
                // make room for a gutter it did not have before. That is a
                // layout update, so this listens to the one signal that says
                // so, or the first error in a file is drawn only if something
                // else happens to relayout. Folding arrives the same way:
                // whoever folds a block calls requestUpdate() afterwards.
                connect(layout, &QAbstractTextDocumentLayout::update, this, [this] {
                    polish();
                    update();
                });
            }
        }
    }

    // Both of these are also done by the layout pass the polish below asks
    // for, so there is nothing to do here but ask for it.
    polish();
    update();
}

// Highlighting arrives late and separately. It lands as formats on the
// blocks' own layouts, which is not a content change and so says nothing
// through contentsChanged - and the viewport copies those formats into layouts
// of its own, so without this the text is drawn in one colour until something
// else happens to relayout it.
//
// Called from updatePolish() as well as on a document change, because a
// highlighter is installed on a document that is *already* being shown: an
// editor only knows which language to ask for once it has the file's path, and
// nothing announces the swap.
void TextViewport::connectHighlighter(TextDocument *doc)
{
    SyntaxHighlighter * const highlighter = doc ? doc->syntaxHighlighter() : nullptr;
    if (highlighter == m_connectedHighlighter)
        return;

    if (m_connectedHighlighter)
        disconnect(m_connectedHighlighter, nullptr, this, nullptr);
    m_connectedHighlighter = highlighter;
    if (highlighter) {
        connect(highlighter, &SyntaxHighlighter::finished, this, [this] {
            polish();
            update();
        });
    }
}

void TextViewport::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() != oldGeometry.size())
        polish();
}

// A background brush that paints nothing. QTextLayout::draw() asks the brush
// for its style and leaves the run alone; a QSGTextNode takes the property
// being there as reason enough and fills the run with the brush's colour,
// which for a default-constructed one is black. SyntaxHighlighter::
// whitespacified() sets exactly that on whitespace inside comments and
// strings, so every such space was drawn as a solid black cell.
static void dropEmptyBackground(QTextCharFormat &format)
{
    if (format.hasProperty(QTextFormat::BackgroundBrush)
        && format.background().style() == Qt::NoBrush) {
        format.clearProperty(QTextFormat::BackgroundBrush);
    }
}

// The same ranges, cut at every boundary so that none of them overlap.
//
// A QTextLayout takes overlapping ranges and QTextLayout::draw() merges them,
// so the widget editor never had to care. A QSGTextNode does not: it splits
// the text at every boundary and keeps one format per run, so where two ranges
// overlap the run outside the overlap loses whatever the other one said. A
// selection ending inside a coloured token took the colour off the rest of it
// that way.
//
// Later ranges win, property by property, which is what merging them in order
// gives and what draw() would have done.
static QList<QTextLayout::FormatRange> flattenedFormats(
    const QList<QTextLayout::FormatRange> &formats)
{
    if (formats.size() < 2) {
        QList<QTextLayout::FormatRange> single = formats;
        for (QTextLayout::FormatRange &range : single)
            dropEmptyBackground(range.format);
        return single;
    }

    QList<int> edges;
    edges.reserve(formats.size() * 2);
    for (const QTextLayout::FormatRange &range : formats) {
        edges.append(range.start);
        edges.append(range.start + range.length);
    }
    std::sort(edges.begin(), edges.end());
    edges.erase(std::unique(edges.begin(), edges.end()), edges.end());

    QList<QTextLayout::FormatRange> flat;
    for (int i = 1; i < edges.size(); ++i) {
        const int from = edges.at(i - 1);
        const int to = edges.at(i);
        QTextCharFormat merged;
        bool covered = false;
        for (const QTextLayout::FormatRange &range : formats) {
            if (range.start <= from && range.start + range.length >= to) {
                merged.merge(range.format);
                covered = true;
            }
        }
        if (!covered)
            continue;
        dropEmptyBackground(merged);
        // Runs that say the same thing are one run: fewer glyph runs to build,
        // and the reuse check below compares format lists.
        if (!flat.isEmpty() && flat.last().start + flat.last().length == from
            && flat.last().format == merged) {
            flat.last().length += to - from;
            continue;
        }
        flat.append({from, to - from, merged});
    }
    return flat;
}

void TextViewport::appendHighlights(QList<QTextLayout::FormatRange> &formats,
                                    const QTextBlock &block) const
{

    const int blockStart = block.position();
    // length() counts the newline, which no format can cover.
    const int blockEnd = blockStart + block.length() - 1;

    for (const QList<Highlight> &set : m_highlights) {
        // The first range that reaches this line. Ranges from one source do
        // not overlap, so their ends rise with their starts and this is exact.
        auto it = std::lower_bound(set.cbegin(), set.cend(), blockStart,
                                   [](const Highlight &highlight, int position) {
                                       return highlight.end <= position;
                                   });
        for (; it != set.cend() && it->start < blockEnd; ++it) {
            const int from = qMax(it->start, blockStart);
            const int to = qMin(it->end, blockEnd);
            if (to <= from)
                continue;
            QTextLayout::FormatRange range;
            range.start = from - blockStart;
            range.length = to - from;
            range.format = it->format;
            formats.append(range);
        }
    }
}

void TextViewport::updatePolish()
{

    // Last time's rows, to be taken from rather than thrown away: scrolling
    // brings back text that has already been shaped, and shaping it again is
    // most of what a layout costs.
    std::vector<Line> previous = std::move(m_lines);
    m_lines.clear();

    TextDocument * const doc = m_document ? m_document->textDocument() : nullptr;
    QTextDocument * const text = doc ? doc->document() : nullptr;

    // Both of these belong here rather than on a signal of their own. Anything
    // that can change which bracket the caret is beside - the caret moving,
    // the text changing, the highlighter finishing a pass - already asks for a
    // layout, and setHighlights() does nothing when the answer is the same, so
    // this settles rather than loops.
    connectHighlighter(doc);
    updateParenthesesMatch();
    updateDocumentSelections();

    if (!text || height() <= 0) {
        m_lineHeight = 0;
        m_contentHeight = 0;
        m_firstVisibleLine = 0;
        // There are no rows now, and what QML holds has to say so rather than
        // keep describing the ones there used to be.
        static_cast<VisibleRowsModel *>(visibleRows())->setRows({});
        emit metricsChanged();
        emit cursorRectangleChanged();
        update();
        return;
    }

    const FontSettingsData &fonts = doc->fontSettings();
    QFont font = fonts.font();
    // font() is the unzoomed font and lineSpacing() is already zoomed, so a
    // font taken straight from font() draws glyphs of one size on lines of
    // another. Zoom it the way lineSpacing() does.
    font.setPointSize(std::max(fonts.fontSize() * fonts.fontZoom() / 100, 1));
    const QFontMetricsF metrics(font);
    // The same height for every line is what makes the line at a scroll offset
    // arithmetic rather than a walk, which is what holds at a million lines.
    // lineSpacing() is what the widget editor lays out with, in pixels, so the
    // two backends put a line in the same place.
    m_lineHeight = qMax(1.0, fonts.lineSpacing());
    m_font = font;
    m_lineCount = text->blockCount();

    // Which row a block starts on. Unwrapped, the document answers: a folded
    // block counts as zero lines, so lineCount() is the height in rows and
    // blockCount() is only how far the gutter's numbers run. Wrapped, no such
    // answer exists until every block has been laid out at this width - see
    // testTheEditorLayoutFollowsAWidthChange - so the index is built here and
    // paid for in full.
    // Wrapped rows indented under the text they continue, vim's 'breakindent'.
    // The layout that counts rows is told the same, or it wraps somewhere else
    // and moving by rows lands between the rows on screen.
    const bool breakIndentOn = m_wrapping && displaySettings().breakindent();
    const int breakIndentMin = displaySettings().breakindentMin();
    const int breakIndentShift = displaySettings().breakindentShift();
    // The marker drawn at the start of a wrapped row, vim's 'showbreak'. It
    // takes room whether or not the rows are indented, so it is part of the
    // same measurement.
    const QString breakMarker = m_wrapping ? displaySettings().showBreak() : QString();
    const bool markerBeforeIndent = displaySettings().breakindentSbr();

    const qreal wrapWidth = m_wrapping ? qMax(1.0, width() - m_lineHeight / 4)
                                       : std::numeric_limits<qreal>::max();
    Utils::TextEditorLayout *rows = m_wrapping ? editorLayout() : nullptr;
    if (rows) {
        // What the index measures with has to be what is drawn.
        if (text->defaultFont() != font)
            text->setDefaultFont(font);
        // setTextWidth() is a document width: the layout takes the document's
        // margin off both sides, and the width of a line separator glyph where
        // the text option asks for one, before it wraps anything. The rows
        // below are shaped with setLineWidth(), which is the width of the line
        // itself. Handing the same number to both wraps them in different
        // places, and then moving by rows lands between the rows on screen.
        qreal separator = 0;
        if (text->defaultTextOption().flags()
            & QTextOption::AddSpaceForLineAndParagraphSeparators) {
            separator = QFontMetricsF(font).horizontalAdvance(QChar(0x21B5));
        }
        rows->setBreakIndent(breakIndentOn, breakIndentMin, breakIndentShift);
        rows->setShowBreak(breakMarker, markerBeforeIndent);
        // Handing it the width it already has is not free: setTextWidth()
        // throws away every block layout that could wrap differently, and the
        // loop below then builds every one of them again. That is the whole
        // document, per layout, for a width that did not change.
        const qreal documentWidth = wrapWidth + 2 * text->documentMargin() + separator;
        if (documentWidth != m_documentTextWidth) {
            static_cast<ViewportLayout *>(rows)->setTextWidth(documentWidth);
            m_documentTextWidth = documentWidth;
        }
        // Every block, because nothing else lays them out and what a row is
        // called depends on all of them - but only when one of them was
        // thrown away since the last time. Scrolling throws none away, and
        // this is the whole document.
        if (const int generation = rows->layoutGeneration(); generation != m_primedGeneration) {
            for (QTextBlock b = text->firstBlock(); b.isValid(); b = b.next())
                rows->blockBoundingRect(b);
            m_primedGeneration = generation;
        }
    }

    // Before anything is placed: a ghost row's gap is as tall as the rows in
    // it, so it is only the right size once there is a line height.
    rebuildGaps();

    const int totalRows = rows ? rows->lineCount() : text->lineCount();
    m_contentHeight = yOfRow(totalRows);
    // The widget editor fills with the *brush*, so a scheme that sets no
    // background paints nothing and the palette shows through. A colour has no
    // way to say "nothing", and QBrush().color() is black, so ask the theme
    // instead of painting the fallback black.
    // What a diff paints over a line it changed, and over the characters in it
    // that differ. Read once a layout rather than per row.
    const QBrush changedBrush = fonts.toTextCharFormat(C_DIFF_DEST_LINE).background();
    m_changedBackground = changedBrush.style() == Qt::NoBrush ? QColor() : changedBrush.color();
    m_changedCharFormat = fonts.toTextCharFormat(C_DIFF_DEST_CHAR);

    const QBrush backgroundBrush = fonts.toTextCharFormat(C_TEXT).background();
    m_background = backgroundBrush.style() == Qt::NoBrush
                       ? Utils::creatorColor(Utils::Theme::BackgroundColorNormal)
                       : backgroundBrush.color();

    // The same brush question as the background above: a scheme that sets no
    // current-line colour must draw nothing, and QBrush().color() is black.
    const QBrush currentLineBrush = fonts.toTextCharFormat(C_CURRENT_LINE).background();
    m_currentLine = currentLineBrush.style() == Qt::NoBrush ? QColor(Qt::transparent)
                                                           : currentLineBrush.color();

    const QTextCharFormat selectionFormat = fonts.toTextCharFormat(C_SELECTION);
    m_selectionColour = selectionFormat.background().color();
    const int selectionFrom = qMin(m_selectionStart, m_selectionEnd);
    const int selectionTo = qMax(m_selectionStart, m_selectionEnd);
    const bool hasSelection = m_selectionStart >= 0 && m_selectionEnd >= 0
                              && selectionFrom != selectionTo;
    // What every caret has selected, the main one included. Drawn per row
    // below; a row usually meets none of these or one.
    QList<QPair<int, int>> selectedRanges;
    if (hasSelection)
        selectedRanges.append({selectionFrom, selectionTo});
    for (const QTextCursor &extra : std::as_const(m_extraCursors)) {
        if (extra.hasSelection())
            selectedRanges.append({extra.selectionStart(), extra.selectionEnd()});
    }

    // The other places the selected text appears. Usually that is why it was
    // selected, so the widget editor shows them and this does too - same rule:
    // a selection inside one line, trimmed, and only when asked for.
    QString occurrence;
    QTextCharFormat occurrenceFormat;
    if (hasSelection && displaySettings().highlightSelection()) {
        QTextCursor selected(text);
        selected.setPosition(selectionFrom);
        selected.setPosition(selectionTo, QTextCursor::KeepAnchor);
        if (selected.block() == text->findBlock(selected.anchor()))
            occurrence = selected.selectedText().trimmed();

        // The selection colour, thinned so that the real selection still
        // stands out against the places it also appears. Which way to thin it
        // depends on whether the page is light or dark, as it does for the
        // widget.
        QColor fill = selectionFormat.background().color();
        const QColor page = fonts.toTextCharFormat(C_TEXT).background().color();
        fill.setAlphaF(Utils::StyleHelper::luminance(page) > 0.5 ? 0.25 : 0.5);
        occurrenceFormat.setBackground(fill);
    }

    // What a link under the pointer looks like: the scheme's link colour,
    // underlined, which is what the widget editor draws.
    QTextCharFormat linkFormat = fonts.toTextCharFormat(C_LINK);
    linkFormat.setFontUnderline(true);

    // One level of indentation, in pixels, and what a guide is drawn in.
    m_indentWidth = doc->tabSettings().m_indentSize * metrics.horizontalAdvance(' ');

    // The right margin. The widget editor puts the line four pixels past the
    // column so that a line exactly that long does not touch it.
    const int marginColumn = visibleMarginColumn(marginSettings().data(), doc->indenter());
    m_marginX = marginColumn <= 0 ? -1 : metrics.horizontalAdvance(' ') * marginColumn + 4;
    m_marginLine = rightMarginColor(fonts, false);
    m_marginArea = rightMarginColor(fonts, true);
    m_tintMarginArea = marginSettings().tintMarginArea();

    // What the document was at when the file was last written. Only a
    // TextDocumentLayout keeps it; without one there is nothing to compare a
    // block's revision against, so no line is marked rather than every line.
    std::optional<int> saveRevision;
    if (auto * const layout = qobject_cast<TextDocumentLayout *>(text->documentLayout()))
        saveRevision = layout->lastSaveRevision;
    const QBrush whitespaceBrush = fonts.toTextCharFormat(C_VISUAL_WHITESPACE).foreground();
    m_indentGuide = whitespaceBrush.style() == Qt::NoBrush ? QColor(Qt::transparent)
                                                          : whitespaceBrush.color();

    const bool showWhitespace = visualizesWhitespace();

    QTextOption option;
    option.setTabStopDistance(doc->tabSettings().m_tabSize * metrics.horizontalAdvance(' '));
    // Anywhere as well as at word boundaries: a single word longer than the
    // viewport has to break somewhere, or it draws off the edge and the row
    // count disagrees with what is shown.
    option.setWrapMode(m_wrapping ? QTextOption::WrapAtWordBoundaryOrAnywhere
                                  : QTextOption::NoWrap);

    // A row can only be kept if it would be shaped the same way again.
    if (font != m_shapedWith || option.tabStopDistance() != m_shapedTabStop
        || wrapWidth != m_shapedWrapWidth) {
        previous.clear();
        m_shapedWith = font;
        m_shapedTabStop = option.tabStopDistance();
        m_shapedWrapWidth = wrapWidth;
    }
    // By where the text starts, which is what a row is identified by once the
    // text on it and the formats over it are the same.
    std::unordered_map<int, std::vector<Line>> reusable;
    int shaped = 0;
    for (Line &line : previous) {
        if (line.layout)
            reusable[line.blockPosition].push_back(std::move(line));
    }

    m_firstVisibleLine = qMax(0, rowAtY(m_scrollY));
    // The last line that starts before the bottom edge - not one more. A hair
    // off the edge so that a viewport an exact number of lines tall does not
    // lay out one that begins where it ends.
    const int lastOnScreen = rowAtY(m_scrollY + height() - 0.001);
    const int last = qMin(totalRows - 1, lastOnScreen);

    // By row rather than by block, so that what is folded above the screen
    // costs nothing to skip: whoever indexes the rows already knows which
    // block one belongs to, and a hidden block is no row at all.
    QTextBlock block = rows ? rows->findBlockByLineNumber(m_firstVisibleLine)
                            : text->findBlockByLineNumber(m_firstVisibleLine);
    while (block.isValid() && !block.isVisible())
        block = block.next();
    // Where in its block the first row on screen sits: a block scrolled
    // half off the top starts partway down.
    int rowInBlock = block.isValid()
                         ? m_firstVisibleLine
                               - (rows ? rows->firstLineNumberOf(block) : block.firstLineNumber())
                         : 0;
    rowInBlock = qMax(0, rowInBlock);

    for (int row = m_firstVisibleLine; row <= last && block.isValid(); ) {
        // Where this block's rows break. Unwrapped there is one row and it is
        // the whole line, so this costs nothing on the fast path.
        QList<QPair<int, int>> breaks; // start, length within the block
        // How far the block's continuation rows are pushed in.
        qreal blockBreakIndent = 0;
        // A block with a suggestion on it is shown as the suggestion says it
        // could read, which is what makes the grey text appear where it will
        // land. Only when it is one line and this view is not wrapping: a
        // multi-line suggestion needs rows this view has not made, and
        // showing part of one would be worse than showing none.
        const QTextBlock suggested = suggestionRowFor(block);
        const QString blockText = suggested.isValid() ? suggested.text() : block.text();
        if (!m_wrapping) {
            breaks.append({0, int(blockText.size())});
        } else {
            QTextLayout shaping(blockText, font);
            shaping.setTextOption(option);
            shaping.beginLayout();
            while (true) {
                QTextLine shaped = shaping.createLine();
                if (!shaped.isValid())
                    break;
                if (breaks.isEmpty()) {
                    shaped.setLineWidth(wrapWidth);
                    if (breakIndentOn || !breakMarker.isEmpty()) {
                        // As far in as the block's own indent, plus the shift,
                        // but never so far that less than breakIndentMin
                        // columns of text are left - the same arithmetic
                        // PlainTextDocumentLayout does for the widget editor.
                        int ws = 0;
                        while (ws < blockText.size()
                               && (blockText.at(ws) == QLatin1Char(' ')
                                   || blockText.at(ws) == QLatin1Char('\t'))) {
                            ++ws;
                        }
                        const qreal spaceWidth = metrics.horizontalAdvance(QLatin1Char(' '));
                        const qreal indent = breakIndentOn
                                                 ? shaped.cursorToX(ws)
                                                       + breakIndentShift * spaceWidth
                                                 : 0;
                        // Room for the marker too, whether or not the rows are
                        // indented - the layout reserves it the same way.
                        const qreal wanted = indent + metrics.horizontalAdvance(breakMarker);
                        const qreal most = wrapWidth - breakIndentMin * spaceWidth;
                        blockBreakIndent = qBound(qreal(0), wanted, qMax(qreal(0), most));
                    }
                } else {
                    shaped.setLineWidth(wrapWidth - blockBreakIndent);
                }
                breaks.append({shaped.textStart(), shaped.textLength()});
            }
            shaping.endLayout();
            if (breaks.isEmpty())
                breaks.append({0, 0});
        }

        // Everything below is the block's, and is put on the row that starts
        // it: a wrapped line is numbered, marked and annotated once.
        Line blockLine;
        // The line the document calls this, which is not the row it is drawn
        // on once anything above it is folded.
        blockLine.lineNumber = block.blockNumber() + 1;
        // The scopes around what the gutter is hovering. Every row of the
        // block carries them - a wrapped line is inside the same scope all
        // the way down - and a block outside the innermost scope still gets
        // level zero, which is what dims everything the scope does not cover.
        if (!m_scopeNesting.isEmpty()) {
            const int number = block.blockNumber();
            int depth = 0;
            for (const int open : m_scopeNesting.open) {
                if (number >= open)
                    ++depth;
            }
            for (const int close : m_scopeNesting.close) {
                if (number > close)
                    --depth;
            }
            const int levels = m_scopeNesting.count();
            const qreal right = qMax(width(), m_contentWidth) - m_scrollX;
            const qreal margin = m_marginX > 0 ? m_marginX - m_scrollX : -1;
            for (int level = 0; level <= depth; ++level) {
                const qreal left
                    = (level > 0 ? m_scopeNesting.visualIndent.at(level - 1) : 0) - m_scrollX;
                if (left >= right)
                    continue;
                const QColor colour = scopeLevelColor(m_background, level, levels);
                const auto band = [&](qreal x, qreal w) {
                    if (w <= 0)
                        return;
                    QVariantMap entry;
                    entry.insert(QStringLiteral("x"), x);
                    entry.insert(QStringLiteral("width"), w);
                    entry.insert(QStringLiteral("colour"), colour);
                    blockLine.scopeBands.append(entry);
                };
                // Split around the right margin so that the line marking it
                // is not painted over, the way the widget editor splits it.
                if (margin > 0 && left < margin && right > margin) {
                    band(left, margin - 1 - left);
                    band(margin + 1, right - margin - 1);
                } else {
                    band(left, right - left);
                }
            }
        }
        // The block's revision against the one the file was written at: a
        // line edited since then is marked, and a negative revision is how the
        // document records an edit that was undone back to what is on disk.
        if (displaySettings().markTextChanges() && saveRevision.has_value()
            && block.revision() != *saveRevision) {
            blockLine.changed = block.revision() < 0 ? Saved : Changed;
        }
        // Off unless asked for, so a row that draws no guides and a row that
        // has none look the same to the form.
        if (displaySettings().visualizeIndent()) {
            const int columns = indentDepthForBlock(block, doc->tabSettings());
            const int perLevel = qMax(1, doc->tabSettings().m_indentSize);
            blockLine.indentGuides = (columns + perLevel - 1) / perLevel;
        }
        // A line starts a fold when what follows it is indented deeper - the
        // same test the widget gutter makes - and that fold is closed when
        // what follows is not shown at all.
        const QTextBlock next = block.next();
        blockLine.foldable = next.isValid()
                             && TextBlockUserData::foldingIndent(next)
                                    > TextBlockUserData::foldingIndent(block);
        blockLine.folded = blockLine.foldable && !next.isVisible();
        if (blockLine.foldable)
            blockLine.foldIcon = foldMarkerUrl(blockLine.folded);
        if (blockLine.folded) {
            // Everything the fold swallowed, so that its last bracket can be
            // put back on the end of the replacement.
            QTextBlock lastHidden = next;
            while (lastHidden.next().isValid() && !lastHidden.next().isVisible())
                lastHidden = lastHidden.next();
            blockLine.foldReplacement
                = TextBlockUserData::foldReplacementText(QString("..."), next, lastHidden);
        }
        // The marks on this line - errors, warnings, breakpoints. The highest
        // priority one wins the slot, which is what the widget gutter does
        // with the space too.
        const TextMarks marks = doc->marksAt(blockLine.lineNumber);
        const TextMark *shown = nullptr;
        for (TextMark * const mark : marks) {
            if (!mark->isVisible() || mark->icon().isNull())
                continue;
            if (!shown || mark->priority() > shown->priority())
                shown = mark;
        }
        if (shown) {
            blockLine.markIcon = QtcQuick::iconUrl(shown->icon());
            blockLine.annotation = shown->lineAnnotation();
        }

        // What the highlighter said about this block, plus the selection over
        // the top of it. Read here, on the GUI thread: the block's own layout
        // belongs to the document and must not be reached from the render one.
        // More than one view may be showing this document, so it is only ever
        // read here, and everything this view alone knows - its selection, its
        // preedit - goes on the copies below.
        QList<QTextLayout::FormatRange> blockFormats;
        if (suggested.isValid()) {
            // Both halves, because they are kept in different places: the
            // grey a suggestion is shown in is set as character formats on
            // the replacement document, and the highlighting carried over
            // from the real line is set on its layout.
            blockFormats = suggested.textFormats();
            if (suggested.layout())
                blockFormats += suggested.layout()->formats();
        } else {
            blockFormats = block.layout()->formats();
        }
        // Under the selection rather than over it: a search result the reader
        // has selected should still look selected.
        appendHighlights(blockFormats, block);
        const int blockStart = block.position();

        // Before the selection is added below, so that where the two overlap -
        // the selection is one of its own occurrences - the selection wins.
        if (!occurrence.isEmpty()) {
            QString haystack = blockText;
            // What the widget searches too: a non-breaking space reads as a
            // space to whoever selected it.
            haystack.replace(QChar::Nbsp, QLatin1Char(' '));
            for (int at = haystack.indexOf(occurrence, 0, Qt::CaseInsensitive); at >= 0;
                 at = haystack.indexOf(occurrence, at + 1, Qt::CaseInsensitive)) {
                QTextLayout::FormatRange range;
                range.start = at;
                range.length = int(occurrence.size());
                range.format = occurrenceFormat;
                blockFormats.append(range);
            }
        }
        if (m_currentLink.hasValidLinkText()) {
            const int from = qMax(0, m_currentLink.linkTextStart - blockStart);
            const int to = qMin(block.length() - 1, m_currentLink.linkTextEnd - blockStart);
            if (to > from) {
                QTextLayout::FormatRange range;
                range.start = from;
                range.length = to - from;
                range.format = linkFormat;
                blockFormats.append(range);
            }
        }
        if (hasSelection) {
            const int from = qMax(0, selectionFrom - blockStart);
            const int to = qMin(block.length() - 1, selectionTo - blockStart);
            if (to > from) {
                QTextLayout::FormatRange range;
                range.start = from;
                range.length = to - from;
                range.format = selectionFormat;
                blockFormats.append(range);
            }
        }

        // One Line per row. A row is a slice of the block, so its formats are
        // the block's clipped to the slice and shifted to start at zero - the
        // same arithmetic a highlight needs, one level down.
        for (; rowInBlock < breaks.size() && row <= last; ++rowInBlock, ++row) {
            const int from = breaks.at(rowInBlock).first;
            const int length = breaks.at(rowInBlock).second;
            const bool lastRow = rowInBlock == breaks.size() - 1;

            Line line;
            line.lineNumber = blockLine.lineNumber;
            line.scopeBands = blockLine.scopeBands;
            line.firstRowOfLine = rowInBlock == 0;
            // The gutter, the fold marker and what a mark says belong to the
            // line, so they go on the row that starts it.
            if (line.firstRowOfLine) {
                // Guides belong to the line, so a wrapped line's continuation
                // rows get none - which is what the widget editor does too.
                line.indentGuides = blockLine.indentGuides;
                line.changed = blockLine.changed;
                line.foldable = blockLine.foldable;
                line.folded = blockLine.folded;
                line.foldIcon = blockLine.foldIcon;
                line.foldReplacement = blockLine.foldReplacement;
                line.markIcon = blockLine.markIcon;
                line.annotation = blockLine.annotation;
            }

            const QString rowText = blockText.mid(from, length);

            QList<QTextLayout::FormatRange> formats;
            for (const QTextLayout::FormatRange &range : std::as_const(blockFormats)) {
                const int rangeFrom = qMax(range.start, from);
                const int rangeTo = qMin(range.start + range.length, from + length);
                if (rangeTo <= rangeFrom)
                    continue;
                QTextLayout::FormatRange clipped;
                clipped.start = rangeFrom - from;
                clipped.length = rangeTo - rangeFrom;
                clipped.format = range.format;
                formats.append(clipped);
            }

            // What a diff says about this line. The marks go on before the
            // reuse check below, or a row whose diff changed would be taken
            // from last time with the old marks still on it.
            if (const auto changed = m_changedByLine.constFind(line.lineNumber);
                changed != m_changedByLine.cend()) {
                line.diffFill = m_changedBackground;
                for (const QPair<int, int> &range : *changed) {
                    const int rangeFrom = qMax(range.first, from);
                    const int rangeTo = qMin(range.first + range.second, from + length);
                    if (rangeTo <= rangeFrom)
                        continue;
                    QTextLayout::FormatRange mark;
                    mark.start = rangeFrom - from;
                    mark.length = rangeTo - rangeFrom;
                    mark.format = m_changedCharFormat;
                    formats.append(mark);
                }
            }

            // Composing text belongs to the row the caret is on and to no
            // other, and it is not in the document: setPreeditArea() is where a
            // layout keeps text that is being typed but not committed.
            const int rowStart = blockStart + from;
            const int rowEnd = rowStart + length + (lastRow ? 1 : 0);
            const bool composing = !m_preeditText.isEmpty() && m_cursorPosition >= rowStart
                                   && m_cursorPosition < rowEnd;

            // The same text with the same formats over it shapes to the same
            // thing, so last time's row will do. Not while composing: what is
            // being typed is not in the text and would not be compared.
            if (!composing) {
                const auto candidates = reusable.find(blockStart);
                if (candidates != reusable.end()) {
                    std::vector<Line> &rows = candidates->second;
                    for (auto it = rows.begin(); it != rows.end(); ++it) {
                        if (it->layout->text() == rowText && it->layout->formats() == formats) {
                            line.layout = std::move(it->layout);
                            rows.erase(it);
                            break;
                        }
                    }
                }
            }

            QTextLine textLine;
            if (line.layout) {
                textLine = line.layout->lineAt(0);
            } else {
                ++shaped;
                line.layout = std::make_unique<QTextLayout>(rowText, font);
                line.layout->setTextOption(option);
                line.layout->setCacheEnabled(true);

                if (composing) {
                    const int offset = m_cursorPosition - rowStart;
                    line.layout->setPreeditArea(offset, m_preeditText);
                    // The input method says how it wants each part of it drawn
                    // - underlined for what is being composed, highlighted for
                    // the part under consideration - relative to the preedit.
                    for (QTextLayout::FormatRange range : std::as_const(m_preeditFormats)) {
                        range.start += offset;
                        formats.append(range);
                    }
                }
                line.layout->setFormats(flattenedFormats(formats));

                line.layout->beginLayout();
                textLine = line.layout->createLine();
                if (textLine.isValid()) {
                    textLine.setPosition(QPointF(0, 0));
                    // The slice is already one row's worth, so it must not
                    // break again - laying it out at the wrap width could
                    // split a row whose width came out a hair over.
                    textLine.setLineWidth(std::numeric_limits<qreal>::max());
                }
                line.layout->endLayout();
            }

            // The selected part of this row as one rectangle. A format range's
            // background is painted per glyph run, which leaves a gap wherever
            // the runs are split - at every highlight boundary and around the
            // spaces - so the fill is drawn behind the text instead.
            if (textLine.isValid()) {
                for (const QPair<int, int> &range : std::as_const(selectedRanges)) {
                    const int selFrom = qMax(range.first, rowStart);
                    const int selTo = qMin(range.second, rowStart + length);
                    if (selTo > selFrom) {
                        const qreal left = textLine.cursorToX(selFrom - rowStart);
                        const qreal right = textLine.cursorToX(selTo - rowStart);
                        line.selectionFills.append(
                            QRectF(left, 0, right - left, m_lineHeight));
                    }
                }
            }

            // Continuation rows sit under the text they continue; the row that
            // starts the line does not move.
            const qreal rowIndent = rowInBlock == 0 ? 0 : blockBreakIndent;

            // Where the line's message goes. The widget editor measures from
            // the end of the text and leaves two line spacings; the alignment
            // moves it right from there. "Between lines" is not here: it puts
            // the message on a line of its own, which needs the block to be
            // taller than the text in it.
            if (!line.annotation.isEmpty() && textLine.isValid()) {
                const qreal gap = 2 * metrics.lineSpacing();
                qreal x = rowIndent - m_scrollX + textLine.naturalTextWidth() + gap;
                // Past the box standing in for a fold, so that a folded line
                // with an error on it does not draw both in one place.
                if (!line.foldReplacement.isEmpty())
                    x += metrics.horizontalAdvance(line.foldReplacement) + gap;

                switch (displaySettings().annotationAlignment()) {
                case AnnotationAlignment::NextToMargin: {
                    const qreal margin = m_marginX > 0 ? m_marginX - m_scrollX : -1;
                    // Only when there is room left for a readable amount of
                    // it, which is what minimalAnnotationContent measures.
                    const qreal minimalContent = metrics.horizontalAdvance(QLatin1Char('x'))
                                                 * displaySettings().minimalAnnotationContent();
                    if (margin > x && width() > margin + minimalContent)
                        x = margin;
                    break;
                }
                case AnnotationAlignment::RightSide:
                    x = qMax(x, width() - metrics.horizontalAdvance(line.annotation));
                    break;
                case AnnotationAlignment::NextToContent:
                    break;
                case AnnotationAlignment::BetweenLines:
                    // On its own line under the row, lined up with the text
                    // rather than trailing it.
                    x = rowIndent - m_scrollX;
                    break;
                }
                line.annotationX = x;
                line.annotationY
                    = yOfRow(row)
                      + (displaySettings().annotationAlignment() == AnnotationAlignment::BetweenLines
                             ? m_lineHeight
                             : 0);
            }

            // The marker goes on the rows that continue a line: at the far
            // left where it is asked for before the indent, otherwise right in
            // front of the text.
            if (!breakMarker.isEmpty() && rowInBlock > 0) {
                line.breakMarker = breakMarker;
                line.breakMarkerX = markerBeforeIndent
                                        ? 0
                                        : rowIndent - metrics.horizontalAdvance(breakMarker);
            }

            // Where the whitespace on this row is, for whoever draws it. In the
            // row's own space plus the indent, which is where it is drawn.
            if (showWhitespace && textLine.isValid()) {
                const QString rowText = line.layout->text();
                for (int i = 0; i < rowText.size(); ++i) {
                    const QChar at = rowText.at(i);
                    if (at != QLatin1Char(' ') && at != QLatin1Char('\t'))
                        continue;
                    const qreal from = textLine.cursorToX(i);
                    const qreal to = textLine.cursorToX(i + 1);
                    line.whitespace.append(QVariantMap{{QStringLiteral("x"), rowIndent + from},
                                                       {QStringLiteral("width"), to - from},
                                                       {QStringLiteral("tab"), at == QLatin1Char('\t')}});
                }
            }

            line.at = QPointF(rowIndent - m_scrollX, yOfRow(row) - m_scrollY);
            line.blockPosition = rowStart;
            // The newline belongs to the last row: that is where the caret sits
            // at the end of the line. A continuation row ends where the next
            // one starts.
            line.blockLength = length + (lastRow ? 1 : 0);

            // The one thing a format range cannot cover: a selection that runs
            // past the end of the line covers the newline too, and there is no
            // character there to format. One rect, a quarter of a line wide,
            // which is what QTextLayout::draw() does for the same case.
            const int blockEnd = blockStart + block.length() - 1;
            if (lastRow && hasSelection && selectionFrom <= blockEnd && selectionTo > blockEnd
                && textLine.isValid()) {
                const qreal right = textLine.naturalTextRect().right();
                line.newlineTail = QRectF(right, 0, m_lineHeight / 4, m_lineHeight);
                line.newlineTailColour = selectionFormat.background().color();
            }

            m_lines.push_back(std::move(line));
        }

        rowInBlock = 0;
        do
            block = block.next();
        while (block.isValid() && !block.isVisible());
    }

    // The widest row on screen. With wrapping off a row carries its whole
    // line, so this is the full width of the widest line in view - which is
    // what setScrollX() clamps against.
    qreal widest = 0;
    for (const Line &line : m_lines) {
        const QTextLine textLine = line.layout->lineAt(0);
        if (textLine.isValid())
            widest = qMax(widest, textLine.naturalTextRect().right());
    }
    if (m_wrapping) {
        // Wrapping means no row is wider than the viewport, so there is nothing
        // to scroll sideways to. Without this the caret allowance below puts a
        // hair of a scroll bar on every wrapped document.
        m_contentWidth = qMin(widest, width());
    } else {
        if (widest > 0) {
            // The caret sits after the last character, so there is a little
            // more to scroll to than there is text - without this the end of
            // the longest line can be reached and the caret on it cannot.
            widest += m_lineHeight;
        }
        // Only the rows on screen are laid out, so widest is the widest line
        // *in view*. Taking that as the width outright means scrolling up past
        // a short line shrinks the content and setScrollX() clamps the reader
        // back to the left, losing where they were - so it is the widest seen
        // so far, the way the widget editor's maximumWidth accumulates. It
        // starts again when the document or the wrapping changes.
        m_contentWidth = qMax(m_contentWidth, widest);
    }

    // Re-clamp both offsets against what there is to scroll through now. The
    // content changes size when the document does, when the line height does,
    // and when the viewport is resized - and a view left scrolled past the end
    // of the new content shows blank space rather than text. Setting them to
    // what they already are is free when nothing moved.
    setScrollY(m_scrollY);
    setScrollX(m_scrollX);
    m_rowsShaped = shaped;
    layOutGhostRows();
    rebuildVisibleLines();

    updateScrollBarHighlights();
    emit metricsChanged();
    // Everything the caret's position on screen depends on was just recomputed.
    emit cursorRectangleChanged();

    // Now that the rows are laid out, the caret can be brought back into view
    // sideways. Changing the scroll asks for another polish, and the flag is
    // already cleared, so this settles rather than repeating.
    if (m_caretVisibleXPending)
        ensureCaretVisibleSideways();

    // The rows are there now, so the bracket that was matched before they
    // were laid out has somewhere to be pulsed.
    if (m_pendingPulse) {
        const PendingPulse pulse = *m_pendingPulse;
        m_pendingPulse.reset();
        TextDocument * const doc = m_document ? m_document->textDocument() : nullptr;
        QTextDocument * const text = doc ? doc->document() : nullptr;
        const QString character = text ? QString(text->characterAt(pulse.position)) : QString();
        QRectF at = rectangleAt(pulse.position);
        if (!at.isNull() && !character.isEmpty()) {
            at.setWidth(QFontMetricsF(m_font).horizontalAdvance(character));
            emit animateCharacter(at, character, pulse.foreground, pulse.background);
        }
    }

    // The rows are laid out again, so the screen point a page key remembered
    // can be turned back into a position.
    if (m_pendingPage) {
        const PendingPage page = *m_pendingPage;
        m_pendingPage.reset();
        const int position = positionAt(page.x, page.y);
        QTextCursor cursor = textCursor();
        if (position >= 0 && !cursor.isNull()) {
            cursor.setPosition(position, page.mode);
            setTextCursor(cursor);
        }
    }
    update();
}

QSGNode *TextViewport::updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *)
{
    // Nothing here reads the document. Everything it draws was decided in
    // updatePolish() on the GUI thread.
    delete oldNode;
    if (m_lines.empty())
        return nullptr;

    QQuickWindow * const win = window();
    if (!win)
        return nullptr;

    auto *root = new QSGNode;
    // First, so that a document row drawn over the same pixels wins: a ghost
    // row lives in a gap of its own, but a stale layout should not paint over
    // the file.
    for (size_t i = 0; i < m_ghostLines.size(); ++i) {
        const Line &line = m_ghostLines.at(i);
        if (!line.layout)
            continue;
        if (line.diffFill.isValid() && line.diffFill.alpha() > 0) {
            QSGRectangleNode * const fill = win->createRectangleNode();
            fill->setRect(QRectF(0, line.at.y(), width(), m_lineHeight));
            fill->setColor(line.diffFill);
            root->appendChildNode(fill);
        }
        QSGTextNode * const node = win->createTextNode();
        node->setRenderType(QSGTextNode::NativeRendering);
        node->addTextLayout(line.at, line.layout.get());
        root->appendChildNode(node);
    }
    for (const Line &line : m_lines) {
        // Below the selection and the text: this is the page the row is drawn
        // on, not a mark on it.
        if (line.diffFill.isValid() && line.diffFill.alpha() > 0) {
            QSGRectangleNode * const fill = win->createRectangleNode();
            fill->setRect(QRectF(0, line.at.y(), width(), m_lineHeight));
            fill->setColor(line.diffFill);
            root->appendChildNode(fill);
        }
        for (const QVariant &entry : line.scopeBands) {
            const QVariantMap band = entry.toMap();
            QSGRectangleNode * const rect = win->createRectangleNode();
            rect->setRect(QRectF(band.value("x").toReal(), line.at.y(),
                                 band.value("width").toReal(), m_lineHeight));
            rect->setColor(band.value("colour").value<QColor>());
            root->appendChildNode(rect);
        }
        for (const QRectF &selected : line.selectionFills) {
            QSGRectangleNode * const fill = win->createRectangleNode();
            fill->setRect(selected.translated(line.at));
            fill->setColor(m_selectionColour);
            root->appendChildNode(fill);
        }
        if (!line.newlineTail.isEmpty()) {
            QSGRectangleNode * const tail = win->createRectangleNode();
            tail->setRect(line.newlineTail.translated(line.at));
            tail->setColor(line.newlineTailColour);
            root->appendChildNode(tail);
        }
        QSGTextNode * const node = win->createTextNode();
        node->setRenderType(QSGTextNode::NativeRendering);
        node->addTextLayout(line.at, line.layout.get());
        root->appendChildNode(node);
    }
    return root;
}

} // namespace TextEditor
