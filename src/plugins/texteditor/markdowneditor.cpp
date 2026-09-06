// Copyright (C) 2023 Tasuku Suzuki
// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "markdowneditor.h"

#include "quicktexteditor.h"
#include "textviewport.h"
#include "highlighterhelper.h"
#include "texteditorconstants.h"

#include "textdocument.h"
#include "texteditor.h"
#include "texteditortr.h"

#include <coreplugin/actionmanager/actionmanager.h>
#include <coreplugin/coreconstants.h>
#include <coreplugin/editormanager/ieditorfactory.h>
#include <coreplugin/find/basetextfind.h>
#include <qtcquick/actionmodel.h>
#include <qtcquick/qtcquickwidget.h>
#include <coreplugin/find/ifindsupport.h>
#include <coreplugin/icore.h>
#include <coreplugin/minisplitter.h>

#include <utils/action.h>
#include <utils/algorithm.h>
#include <utils/aggregate.h>
#include <utils/link.h>
#include <utils/markdownbrowser.h>
#include <utils/qtcsettings.h>
#include <utils/stringutils.h>
#include <utils/textutils.h>

#ifdef WITH_TESTS
#include <utils/temporarydirectory.h>

#include <QScopeGuard>
#include <QToolBar>
#include <QWidgetAction>
#include <QTest>
#endif
#include <utils/utilsicons.h>

#include <QHBoxLayout>
#include <QRegularExpression>
#include <QScrollBar>
#include <QQuickItem>
#include <QQuickWidget>
#include <QTextBrowser>
#include <QTimer>
#include <QToolBar>

#include <optional>

using namespace Core;
using namespace Utils;

namespace TextEditor::Internal {

const char MARKDOWNVIEWER_ID[] = "Editors.MarkdownViewer";
const char MARKDOWNVIEWER_TEXT_CONTEXT[] = "Editors.MarkdownViewer.Text";
const char MARKDOWNVIEWER_PREVIEW_CONTEXT[] = "Editors.MarkdownViewer.Preview";
const char MARKDOWNVIEWER_MIME_TYPE[] = "text/markdown";
const char MARKDOWNVIEWER_TEXTEDITOR_RIGHT[] = "Markdown.TextEditorRight";
const char MARKDOWNVIEWER_SHOW_EDITOR[] = "Markdown.ShowEditor";
const char MARKDOWNVIEWER_SHOW_PREVIEW[] = "Markdown.ShowPreview";
const bool kTextEditorRightDefault = false;
const bool kShowEditorDefault = true;
const bool kShowPreviewDefault = true;
const char EMPHASIS_ACTION[] = "Markdown.Emphasis";
const char STRONG_ACTION[] = "Markdown.Strong";
const char INLINECODE_ACTION[] = "Markdown.InlineCode";
const char LINK_ACTION[] = "Markdown.Link";
const char TOGGLEEDITOR_ACTION[] = "Markdown.ToggleEditor";
const char TOGGLEPREVIEW_ACTION[] = "Markdown.TogglePreview";
const char SWAPVIEWS_ACTION[] = "Markdown.SwapViews";

// Where a Markdown link points. Carried by the document rather than by an
// editor widget, so that a view which is not one can follow a link too.
void findMarkdownLinkAt(TextDocument *document,
                        const QTextCursor &cursor,
                        const Utils::LinkHandler &processLinkCallback,
                        bool resolveTarget,
                        bool inNextSplit);

// A Core::IEditor rather than a BaseTextEditor: BaseTextEditor answers
// document(), toolBar() and the rest through TextEditorWidget::fromEditor(),
// which is the text pane - and this editor is two panes, only one of which is
// text. What it needed from that base is a handful of one-line answers, below.
class MarkdownEditor : public Core::IEditor
{
    Q_OBJECT
public:
    MarkdownEditor()
        : MarkdownEditor(TextDocumentPtr(new TextDocument(MARKDOWNVIEWER_ID)))
    {
        m_document->setMimeType(MARKDOWNVIEWER_MIME_TYPE);
        connect(
            m_document.data(),
            &TextDocument::mimeTypeChanged,
            m_document.data(),
            &TextDocument::changed);
    }

    explicit MarkdownEditor(const TextDocumentPtr &doc)
        : m_document(doc)
        , m_gate(new Internal::OptionalActionGate(this, [this] { return m_viewport; }))
        , m_jumps(new Internal::JumpRecorder(this, [this] { return viewState(); }))
    {
        setDuplicateSupported(true);

        QtcSettings *s = ICore::settings();
        const bool textEditorRight
            = s->value(MARKDOWNVIEWER_TEXTEDITOR_RIGHT, kTextEditorRightDefault).toBool();
        const bool showPreview = s->value(MARKDOWNVIEWER_SHOW_PREVIEW, kShowPreviewDefault).toBool();
        const bool showEditor = s->value(MARKDOWNVIEWER_SHOW_EDITOR, kShowEditorDefault).toBool()
                                || !showPreview; // ensure at least one is visible

        m_splitter = new MiniSplitter;

        // preview
        m_previewWidget = new Utils::MarkdownBrowser();
        m_previewWidget->setWheelZoomEnabled(true);
        m_previewWidget->setFrameShape(QFrame::NoFrame);
        m_previewWidget->setShowRulersForHeadings(true);
        auto previewFind = new BaseTextFind<QTextBrowser>(m_previewWidget);
        Aggregation::aggregate({m_previewWidget, previewFind});
        IContext::attach(m_previewWidget, Context(MARKDOWNVIEWER_PREVIEW_CONTEXT));
        connect(m_previewWidget,
                &MarkdownBrowser::openFileRequested,
                this,
                [this](Link link) {
                    // Absolute path or relative (to the document): open in Qt Creator.
                    link.targetFilePath = document()->filePath().parentDir().resolvePath(
                        link.targetFilePath);
                    EditorManager::openEditorAt(link);
                });

        // editor
        m_document->setLinkFinder(&findMarkdownLinkAt);
        // Markdown is coloured by a generic definition; the bare view
        // configures none of its own, unlike the Qt Quick editor.
        const HighlighterHelper::Definitions definitions
            = HighlighterHelper::definitionsForDocument(m_document.data());
        if (!definitions.isEmpty())
            HighlighterHelper::setDefinitionOn(m_document.data(), definitions.first());

        m_textView = Internal::createQuickTextViewOver(m_document, nullptr);
        m_viewport = Internal::viewportIn(m_textView);
        QTC_ASSERT(m_viewport, return);

        // Search results shown in one view are shown in the other. The text
        // side is a viewport now, and the find it is asked for is the one
        // aggregated onto its host rather than onto a widget editor.
        auto textFind = Aggregation::query<BaseTextFindBase>(m_textView);
        if (QTC_GUARD(textFind)) {
            m_editorHighlight.find = textFind;
            m_editorHighlight.view = m_textView;
            m_previewHighlight.find = previewFind;
            m_previewHighlight.view = m_previewWidget;
            connect(textFind,
                    &BaseTextFindBase::highlightAllRequested,
                    this,
                    [this](const QString &txt, FindFlags findFlags) {
                        mirrorHighlights(m_previewHighlight, txt, findFlags);
                    });
            connect(previewFind,
                    &BaseTextFindBase::highlightAllRequested,
                    this,
                    [this](const QString &txt, FindFlags findFlags) {
                        mirrorHighlights(m_editorHighlight, txt, findFlags);
                    });
        }

        // Where a jump landed, so Go Back returns to it - the rule being that
        // the entry is recorded when the reader leaves, not on arrival.
        connect(m_viewport, &TextViewport::cursorPositionChanged,
                m_jumps, &Internal::JumpRecorder::caretMoved);

        // Follow Symbol. The link finder above is what answers it; the gate is
        // what says this language can, which addOptionalActionsIn() may add to
        // later.
        const auto follow = [this](Utils::Id id, bool inNextSplit) {
            m_gate->gate(Core::ActionBuilder(this, id)
                             .setContext(Context(MARKDOWNVIEWER_TEXT_CONTEXT))
                             .addOnTriggered(this, [this, inNextSplit] {
                                 m_viewport->followSymbolUnderCursor(
                                     m_viewport->opensInNextSplit(inNextSplit));
                             })
                             .contextAction(),
                         OptionalActions::FollowSymbolUnderCursor);
        };
        follow(TextEditor::Constants::FOLLOW_SYMBOL_UNDER_CURSOR, false);
        follow(TextEditor::Constants::FOLLOW_SYMBOL_UNDER_CURSOR_IN_NEXT_SPLIT, true);
        m_gate->setOptionalActions(OptionalActions::FollowSymbolUnderCursor);

        IContext::attach(m_textView, Context(MARKDOWNVIEWER_TEXT_CONTEXT));

        m_splitter->addWidget(m_textView); // sets splitter->focusWidget() on non-Windows
        m_splitter->addWidget(m_previewWidget);

        // Ignore size hints and just distribute equally 50%/50%
        m_previewWidget->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
        m_textView->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
        m_splitter->setSizes({1, 1});

        setContext(Context(MARKDOWNVIEWER_ID));

        auto widget = new QWidget;
        auto layout = new QVBoxLayout;
        layout->setContentsMargins(0, 0, 0, 0);
        widget->setLayout(layout);
        layout->addWidget(m_splitter);
        setWidget(widget);
        m_widget->installEventFilter(this);
        using namespace Aggregation;
        Aggregate *agg = Aggregate::parentAggregate(m_textView);
        if (!agg) {
            agg = new Aggregate;
            agg->add(m_textView);
        }
        agg->add(m_widget.get());

        m_togglePreviewVisible = Command::createActionWithShortcutToolTip(TOGGLEPREVIEW_ACTION, this);
        m_togglePreviewVisible->setCheckable(true);
        m_togglePreviewVisible->setChecked(showPreview);
        m_previewWidget->setVisible(showPreview);

        m_toggleEditorVisible = Command::createActionWithShortcutToolTip(TOGGLEEDITOR_ACTION, this);
        m_toggleEditorVisible->setCheckable(true);
        m_toggleEditorVisible->setChecked(showEditor);
        m_textView->setVisible(showEditor);

        const auto markdownAction = [this](Utils::Id command, const QString &glyph,
                                           void (MarkdownEditor::*trigger)()) {
            QAction * const action = Command::createActionWithShortcutToolTip(command, this);
            if (!glyph.isEmpty())
                action->setIconText(glyph);
            connect(action, &QAction::triggered, this, trigger);
            m_markDownActions.append(action);
            return action;
        };
        QAction * const emphasis = markdownAction(EMPHASIS_ACTION, "i",
                                                  &MarkdownEditor::triggerEmphasis);
        QAction * const strong = markdownAction(STRONG_ACTION, "b",
                                                &MarkdownEditor::triggerStrong);
        markdownAction(INLINECODE_ACTION, "`", &MarkdownEditor::triggerInlineCode);
        markdownAction(LINK_ACTION, {}, &MarkdownEditor::triggerLink)
            ->setIcon(Utils::Icons::LINK_TOOLBAR.icon());

        for (QAction * const action : std::as_const(m_markDownActions)) {
            // do not call setVisible(true) at this point, this destroys the hover effect on macOS
            if (!showEditor)
                action->setVisible(false);
        }


        // These two are a styled glyph rather than an icon. A QAction carries
        // a font, so it says so itself and either tool bar can read it.
        emphasis->setFont([emphasis] { QFont f = emphasis->font(); f.setItalic(true); return f; }());
        strong->setFont([strong] { QFont f = strong->font(); f.setBold(true); return f; }());

        m_swapViews = Command::createActionWithShortcutToolTip(SWAPVIEWS_ACTION, this);
        m_swapViews->setEnabled(showEditor && showPreview);

        setWidgetOrder(textEditorRight);

        const auto viewToggled =
            [this](QWidget *view, bool visible, QWidget *otherView, QAction *otherAction) {
                if (view->isVisible() == visible)
                    return;
                view->setVisible(visible);
                if (visible) {
                    view->setFocus();
                } else if (otherView->isVisible()) {
                    otherView->setFocus();
                } else {
                    // make sure at least one view is visible
                    otherAction->toggle();
                }
                m_swapViews->setEnabled(view->isVisible() && otherView->isVisible());
            };
        const auto saveViewSettings = [this] {
            Utils::QtcSettings *s = ICore::settings();
            s->setValueWithDefault(MARKDOWNVIEWER_SHOW_PREVIEW,
                                   m_togglePreviewVisible->isChecked(),
                                   kShowPreviewDefault);
            s->setValueWithDefault(MARKDOWNVIEWER_SHOW_EDITOR,
                                   m_toggleEditorVisible->isChecked(),
                                   kShowEditorDefault);
        };

        connect(m_toggleEditorVisible,
                &QAction::toggled,
                this,
                [this, viewToggled, saveViewSettings](bool visible) {
                    viewToggled(m_textView,
                                visible,
                                m_previewWidget,
                                m_togglePreviewVisible);
                    for (QAction * const action : std::as_const(m_markDownActions))
                        action->setVisible(visible);
                    if (visible)
                        flushHighlights(m_editorHighlight);
                    saveViewSettings();
                });
        connect(
            m_togglePreviewVisible,
            &QAction::toggled,
            this,
            [this, viewToggled, saveViewSettings](bool visible) {
                viewToggled(m_previewWidget, visible, m_textView, m_toggleEditorVisible);
                if (visible && m_performDelayedUpdate) {
                    m_performDelayedUpdate = false;
                    updatePreviewNow();
                }
                if (visible)
                    flushHighlights(m_previewHighlight);
                saveViewSettings();
            });

        connect(m_swapViews, &QAction::triggered, m_textView, [this] {
            const bool textEditorRight = isTextEditorRight();
            setWidgetOrder(!textEditorRight);
            // save settings
            Utils::QtcSettings *s = ICore::settings();
            s->setValueWithDefault(MARKDOWNVIEWER_TEXTEDITOR_RIGHT,
                                   !textEditorRight,
                                   kTextEditorRightDefault);
        });

        // TODO directly update when we build with Qt 6.5.2
        m_previewTimer.setInterval(500);
        m_previewTimer.setSingleShot(true);
        connect(&m_previewTimer, &QTimer::timeout, this, &MarkdownEditor::updatePreview);

        connect(m_document->document(), &QTextDocument::contentsChanged, &m_previewTimer, [this] {
            m_previewTimer.start();
        });
    }

    Core::IEditor *duplicate() override
    {
        auto other = new MarkdownEditor(m_document);
        other->restoreState(saveState());
        other->updatePreview();
        emit editorDuplicated(other);
        return other;
    }

    void updatePreview()
    {
        if (m_togglePreviewVisible->isChecked())
            updatePreviewNow();
        else
            m_performDelayedUpdate = true;
    }

    void updatePreviewNow()
    {
        // save scroll positions
        const QPoint positions = m_previewRestoreScrollPosition
                                     ? *m_previewRestoreScrollPosition
                                     : QPoint(
                                           m_previewWidget->horizontalScrollBar()->value(),
                                           m_previewWidget->verticalScrollBar()->value());
        m_previewRestoreScrollPosition.reset();

        m_previewWidget->setMarkdown(m_document->plainText());

        m_previewWidget->horizontalScrollBar()->setValue(positions.x());
        m_previewWidget->verticalScrollBar()->setValue(positions.y());
    }

    void triggerEmphasis()
    {
        triggerFormatingAction([](QString *selectedText, int *cursorOffset) {
            if (selectedText->isEmpty()) {
                *selectedText = QStringLiteral("**");
                *cursorOffset = -1;
            } else {
                *selectedText = QStringLiteral("*%1*").arg(*selectedText);
            }
        });
    }
    void triggerStrong()
    {
        triggerFormatingAction([](QString *selectedText, int *cursorOffset) {
            if (selectedText->isEmpty()) {
                *selectedText = QStringLiteral("****");
                *cursorOffset = -2;
            } else {
                *selectedText = QStringLiteral("**%1**").arg(*selectedText);
            }
        });
    }
    void triggerInlineCode()
    {
        triggerFormatingAction([](QString *selectedText, int *cursorOffset) {
            if (selectedText->isEmpty()) {
                *selectedText = QStringLiteral("``");
                *cursorOffset = -1;
            } else {
                *selectedText = QStringLiteral("`%1`").arg(*selectedText);
            }
        });
    }
    void triggerLink()
    {
        triggerFormatingAction([](QString *selectedText, int *cursorOffset, int *selectionLength) {
            if (selectedText->isEmpty()) {
                *selectedText = QStringLiteral("[](https://)");
                *cursorOffset = -11; // ](https://) is 11 chars
            } else {
                *selectedText = QStringLiteral("[%1](https://)").arg(*selectedText);
                *cursorOffset = -1;
                *selectionLength = -8; // https:// is 8 chars
            }
        });
    }

    void toggleEditor() { m_toggleEditorVisible->toggle(); }
    void togglePreview() { m_togglePreviewVisible->toggle(); }
    void swapViews() { m_swapViews->trigger(); }

    void increasePreviewZoom() { m_previewWidget->increaseZoom(); }
    void decreasePreviewZoom() { m_previewWidget->decreaseZoom(); }
    void resetPreviewZoom() { m_previewWidget->resetZoom(); }

    bool isTextEditorRight() const { return m_splitter->widget(0) == m_previewWidget; }

    void setWidgetOrder(bool textEditorRight)
    {
        QTC_ASSERT(m_splitter->count() > 1, return);
        QWidget *left = textEditorRight ? static_cast<QWidget *>(m_previewWidget)
                                        : static_cast<QWidget *>(m_textView);
        QWidget *right = textEditorRight ? static_cast<QWidget *>(m_textView)
                                         : m_previewWidget;
        m_splitter->insertWidget(0, left);
        m_splitter->insertWidget(1, right);
        // buttons
        const auto leftAction = textEditorRight ? m_togglePreviewVisible : m_toggleEditorVisible;
        const auto rightAction = textEditorRight ? m_toggleEditorVisible : m_togglePreviewVisible;
        m_toolBarActions.setActions(m_markDownActions
                                    + QList<QAction *>{leftAction, rightAction, m_swapViews});
    }

    Core::IDocument *document() const override { return m_document.data(); }

    // The Qt Quick row over the text pane. Built on demand and once: the
    // editor manager asks whenever this editor becomes current.
    QWidget *toolBar() override
    {
        if (!m_toolBar) {
            m_toolBar = Internal::createQuickTextToolBar(m_viewport, &m_toolBarActions,
                                                         nullptr, m_document->toolBarChoice());
        }
        return m_toolBar;
    }

    int currentLine() const override { return m_viewport->textCursor().blockNumber() + 1; }

    int currentColumn() const override
    {
        const QTextCursor cursor = m_viewport->textCursor();
        return cursor.position() - cursor.block().position() + 1;
    }

    QString selectedText() const override { return m_viewport->selectedText(); }

    void gotoLine(int line, int column, bool centerLine) override
    {
        if (!m_toggleEditorVisible->isChecked())
            m_toggleEditorVisible->toggle();
        m_viewport->gotoLine(line, column, centerLine);
        m_jumps->jumped();
    }

    bool eventFilter(QObject *obj, QEvent *ev) override
    {
        if (obj == m_widget && ev->type() == QEvent::FocusIn) {
            if (m_splitter->focusWidget())
                m_splitter->focusWidget()->setFocus();
            else if (m_textView->isVisible())
                m_textView->quickWidget()->setFocus();
            else
                m_splitter->widget(0)->setFocus();
            return true;
        }
        return IEditor::eventFilter(obj, ev);
    }

    // Where the reader was in the text pane. A private format: only this
    // editor is ever handed it back.
    QByteArray viewState() const
    {
        QByteArray state;
        QDataStream stream(&state, QIODevice::WriteOnly);
        stream << m_viewport->cursorPosition() << m_viewport->scrollY();
        return state;
    }

    void restoreViewState(const QByteArray &state)
    {
        if (state.isEmpty())
            return;
        QDataStream stream(state);
        int position = 0;
        qreal scrollY = 0;
        stream >> position >> scrollY;
        m_viewport->setCursorPosition(position);
        m_viewport->setScrollY(scrollY);
    }

    QByteArray saveState() const override
    {
        QByteArray state;
        QDataStream stream(&state, QIODevice::WriteOnly);
        stream << 1; // version number
        stream << viewState();
        stream << m_previewWidget->horizontalScrollBar()->value();
        stream << m_previewWidget->verticalScrollBar()->value();
        stream << isTextEditorRight();
        stream << m_togglePreviewVisible->isChecked();
        stream << m_toggleEditorVisible->isChecked();
        stream << m_splitter->saveState();
        return state;
    }

    void restoreState(const QByteArray &state) override
    {
        if (state.isEmpty())
            return;
        int version;
        QByteArray editorState;
        int previewHV;
        int previewVV;
        bool textEditorRight;
        bool previewShown;
        bool textEditorShown;
        QByteArray splitterState;
        QDataStream stream(state);
        stream >> version;
        stream >> editorState;
        stream >> previewHV;
        stream >> previewVV;
        stream >> textEditorRight;
        stream >> previewShown;
        stream >> textEditorShown;
        stream >> splitterState;
        restoreViewState(editorState);
        m_previewRestoreScrollPosition.emplace(previewHV, previewVV);
        setWidgetOrder(textEditorRight);
        m_splitter->restoreState(splitterState);
        m_togglePreviewVisible->setChecked(previewShown);
        m_previewWidget->setVisible(m_togglePreviewVisible->isChecked());
        // ensure at least one is shown
        m_toggleEditorVisible->setChecked(textEditorShown || !previewShown);
        m_textView->setVisible(m_toggleEditorVisible->isChecked());
    }

private:
    void triggerFormatingAction(std::function<void(QString *selectedText, int *cursorOffset)> action)
    {
        auto formattedText = m_viewport->selectedText();
        int cursorOffset = 0;
        action(&formattedText, &cursorOffset);
        format(formattedText, cursorOffset);
    }
    void triggerFormatingAction(std::function<void(QString *selectedText, int *cursorOffset, int *selectionLength)> action)
    {
        auto formattedText = m_viewport->selectedText();
        int cursorOffset = 0;
        int selectionLength = 0;
        action(&formattedText, &cursorOffset, &selectionLength);
        format(formattedText, cursorOffset, selectionLength);
    }
    void format(const QString &formattedText, int cursorOffset = 0, int selectionLength = 0)
    {
        auto cursor = m_viewport->textCursor();
        int start = cursor.selectionStart();
        int end = cursor.selectionEnd();
        cursor.setPosition(start, QTextCursor::MoveAnchor);
        cursor.setPosition(end, QTextCursor::KeepAnchor);

        cursor.insertText(formattedText);
        if (cursorOffset != 0) {
            auto pos = cursor.position();
            cursor.setPosition(pos + cursorOffset);
            m_viewport->setTextCursor(cursor);
        }

        if (selectionLength != 0) {
            cursor.setPosition(cursor.position(), QTextCursor::MoveAnchor);
            cursor.setPosition(cursor.position() + selectionLength, QTextCursor::KeepAnchor);
            m_viewport->setTextCursor(cursor);
        }
    }

    struct MirroredHighlight
    {
        BaseTextFindBase *find = nullptr;
        QWidget *view = nullptr;
        std::optional<QString> pendingText;
        FindFlags pendingFlags;
    };

    void mirrorHighlights(MirroredHighlight &target, const QString &txt, FindFlags findFlags)
    {
        if (m_blockMirrorHighlights)
            return;
        target.pendingText = txt;
        target.pendingFlags = findFlags;
        if (target.view->isVisible())
            flushHighlights(target);
    }

    void flushHighlights(MirroredHighlight &target)
    {
        if (!target.pendingText)
            return;
        const QString txt = *std::exchange(target.pendingText, std::nullopt);
        m_blockMirrorHighlights = true;
        target.find->highlightAll(txt, target.pendingFlags);
        m_blockMirrorHighlights = false;
    }

    void saveCurrentStateForNavigationHistory() { m_savedNavigationState = saveState(); }

    void addSavedStateToNavigationHistory()
    {
        EditorManager::addCurrentPositionToNavigationHistory(m_savedNavigationState);
    }

    void addCurrentStateToNavigationHistory()
    {
        EditorManager::addCurrentPositionToNavigationHistory();
    }

private:
    QTimer m_previewTimer;
    bool m_performDelayedUpdate = false;
    MiniSplitter *m_splitter;
    Utils::MarkdownBrowser *m_previewWidget;
    QtcQuick::QuickWidget *m_textView = nullptr;
    TextViewport *m_viewport = nullptr;
    QtcQuick::ActionModel m_toolBarActions;
    QPointer<QWidget> m_toolBar;
    Internal::OptionalActionGate * const m_gate;
    Internal::JumpRecorder * const m_jumps;
    TextDocumentPtr m_document;
    QByteArray m_savedNavigationState;
    QList<QAction *> m_markDownActions;
    QAction *m_toggleEditorVisible;
    QAction *m_togglePreviewVisible;
    QAction *m_swapViews;
    std::optional<QPoint> m_previewRestoreScrollPosition;
    MirroredHighlight m_editorHighlight;
    MirroredHighlight m_previewHighlight;
    bool m_blockMirrorHighlights = false;
};

class MarkdownEditorFactory final : public IEditorFactory
{
public:
    MarkdownEditorFactory();

private:
    Action m_emphasisAction;
    Action m_strongAction;
    Action m_inlineCodeAction;
    Action m_linkAction;
    Action m_toggleEditorAction;
    Action m_togglePreviewAction;
    Action m_swapAction;
    Action m_previewZoomInAction;
    Action m_previewZoomOutAction;
    Action m_previewZoomResetAction;
};

MarkdownEditorFactory::MarkdownEditorFactory()
{
    setId(MARKDOWNVIEWER_ID);
    setDisplayName(Tr::tr("Markdown Editor"));
    addMimeType(MARKDOWNVIEWER_MIME_TYPE);
    setEditorCreator([] { return new MarkdownEditor; });

    const auto textContext = Context(MARKDOWNVIEWER_TEXT_CONTEXT);
    const auto previewContext = Context(MARKDOWNVIEWER_PREVIEW_CONTEXT);
    const auto context = Context(MARKDOWNVIEWER_ID);

    ActionBuilder(nullptr, EMPHASIS_ACTION)
        .adopt(&m_emphasisAction)
        .setText(Tr::tr("Emphasis"))
        .setContext(textContext)
        .addOnTriggered(EditorManager::instance(), [] {
            auto editor = qobject_cast<MarkdownEditor *>(EditorManager::currentEditor());
            if (editor)
                editor->triggerEmphasis();
        });

    ActionBuilder(nullptr, STRONG_ACTION)
        .adopt(&m_strongAction)
        .setText("Strong")
        .setContext(textContext)
        .addOnTriggered(EditorManager::instance(), [] {
            auto editor = qobject_cast<MarkdownEditor *>(EditorManager::currentEditor());
            if (editor)
                editor->triggerStrong();
        });

    ActionBuilder(nullptr, INLINECODE_ACTION)
        .adopt(&m_inlineCodeAction)
        .setText(Tr::tr("Inline Code"))
        .setContext(textContext)
        .addOnTriggered(EditorManager::instance(), [] {
            auto editor = qobject_cast<MarkdownEditor *>(EditorManager::currentEditor());
            if (editor)
                editor->triggerInlineCode();
        });

    ActionBuilder(nullptr, LINK_ACTION)
        .adopt(&m_linkAction)
        .setText(Tr::tr("Hyperlink"))
        .setContext(textContext)
        .addOnTriggered(EditorManager::instance(), [] {
            auto editor = qobject_cast<MarkdownEditor *>(EditorManager::currentEditor());
            if (editor)
                editor->triggerLink();
        });

    ActionBuilder(nullptr, TOGGLEEDITOR_ACTION)
        .adopt(&m_toggleEditorAction)
        .setText(Tr::tr("Show Editor"))
        .setContext(context)
        .addOnTriggered(EditorManager::instance(), [] {
            auto editor = qobject_cast<MarkdownEditor *>(EditorManager::currentEditor());
            if (editor)
                editor->toggleEditor();
        });

    ActionBuilder(nullptr, TOGGLEPREVIEW_ACTION)
        .adopt(&m_togglePreviewAction)
        .setText(Tr::tr("Show Preview"))
        .setContext(context)
        .addOnTriggered(EditorManager::instance(), [] {
            auto editor = qobject_cast<MarkdownEditor *>(EditorManager::currentEditor());
            if (editor)
                editor->togglePreview();
        });

    ActionBuilder(nullptr, SWAPVIEWS_ACTION)
        .adopt(&m_swapAction)
        .setText(Tr::tr("Swap Views"))
        .setContext(context)
        .addOnTriggered(EditorManager::instance(), [] {
            auto editor = qobject_cast<MarkdownEditor *>(EditorManager::currentEditor());
            if (editor)
                editor->swapViews();
        });

    ActionBuilder(nullptr, Core::Constants::ZOOM_IN)
        .adopt(&m_previewZoomInAction)
        .setContext(previewContext)
        .addOnTriggered(EditorManager::instance(), [] {
            auto editor = qobject_cast<MarkdownEditor *>(EditorManager::currentEditor());
            if (editor)
                editor->increasePreviewZoom();
        });

    ActionBuilder(nullptr, Core::Constants::ZOOM_OUT)
        .adopt(&m_previewZoomOutAction)
        .setContext(previewContext)
        .addOnTriggered(EditorManager::instance(), [] {
            auto editor = qobject_cast<MarkdownEditor *>(EditorManager::currentEditor());
            if (editor)
                editor->decreasePreviewZoom();
        });

    ActionBuilder(nullptr, Core::Constants::ZOOM_RESET)
        .adopt(&m_previewZoomResetAction)
        .setContext(previewContext)
        .addOnTriggered(EditorManager::instance(), [] {
            auto editor = qobject_cast<MarkdownEditor *>(EditorManager::currentEditor());
            if (editor)
                editor->resetPreviewZoom();
        });
}

#ifdef WITH_TESTS

// Declared above the regular expression below: moc's namespace tracking does
// not survive the raw string literals in it, and a Q_OBJECT class after them
// comes out unqualified.
class MarkdownEditorTest : public QObject
{
    Q_OBJECT

private slots:
    // The tool bar used to be seven QToolButtons handed to
    // insertExtraToolBarWidget(), which a view that is not a widget has
    // nowhere to put. It is seven QActions now.
    // BaseTextEditor answers document(), toolBar(), currentLine() and the
    // rest through TextEditorWidget::fromEditor() - the text pane. This editor
    // is two panes and answers for itself, so that the text one can be
    // replaced without the editor going with it.
    static QWidget *m_textViewOf(Core::IEditor *editor)
    {
        return editor->widget()->findChild<QtcQuick::QuickWidget *>();
    }

    // The buttons the Qt Quick tool bar row drew, by the action text each
    // carries. The row is a QML form now, so there is no QToolBar to ask.
    static QHash<QString, QQuickItem *> toolBarButtons(Core::IEditor *editor)
    {
        QHash<QString, QQuickItem *> byText;
        QWidget * const bar = editor->toolBar();
        if (!bar)
            return byText;
        auto * const quick = bar->findChild<QQuickWidget *>();
        if (!quick || !quick->rootObject())
            return byText;
        // The visual tree, not the QObject one: a Repeater parents its
        // delegates into the item they are drawn in, so findChildren() misses
        // every one of them.
        const std::function<void(QQuickItem *)> collect = [&](QQuickItem *item) {
            if (!item)
                return;
            if (item->objectName() == "languageToolBarButton")
                byText.insert(item->property("text").toString(), item);
            const QList<QQuickItem *> children = item->childItems();
            for (QQuickItem * const child : children)
                collect(child);
        };
        collect(quick->rootObject());
        return byText;
    }

    // The emphasis and strong buttons are a styled glyph rather than an icon.
    // The action carries the font now, so both kinds of tool bar can read it -
    // this checks the widget one still draws it, which is what styling the
    // button by hand used to do.
    void testTheStyledGlyphsAreCarriedByTheirActions()
    {
        Utils::TemporaryDirectory dir("markdown-glyphs");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("doc.md");
        QVERIFY(file.writeFileContents("word\n"));

        Core::IEditor * const editor = Core::EditorManager::openEditor(file);
        QVERIFY(editor);
        const QScopeGuard closeIt(
            [editor] { Core::EditorManager::closeEditors({editor}, false); });
        QHash<QString, QAction *> byText;
        for (QAction * const action : editor->findChildren<QAction *>())
            byText.insert(action->text(), action);

        QAction * const emphasis = byText.value("Emphasis");
        QAction * const strong = byText.value("Strong");
        QAction * const inlineCode = byText.value("Inline Code");
        QVERIFY(emphasis && strong && inlineCode);

        // The action says so.
        QVERIFY2(emphasis->font().italic(), "the emphasis action is not italic");
        QVERIFY2(strong->font().bold(), "the strong action is not bold");
        // And one that is neither, so the assertions above are not true of
        // every action in the row.
        QVERIFY2(!inlineCode->font().italic() && !inlineCode->font().bold(),
                 "a plain action came out styled");

        // And the row draws it.
        QHash<QString, QQuickItem *> drawn;
        QTRY_VERIFY2(!(drawn = toolBarButtons(editor)).isEmpty(), "the row drew nothing");
        QQuickItem * const italicButton = drawn.value("Emphasis");
        QVERIFY2(italicButton, "the row drew no button for the emphasis action");
        const QFont drawnFont = italicButton->property("emphasisedLabelFont").value<QFont>();
        QVERIFY2(drawnFont.italic(), "the row drew the emphasis action upright");
        QQuickItem * const plainButton = drawn.value("Inline Code");
        QVERIFY2(plainButton, "the row drew no button for the inline code action");
        QVERIFY2(!plainButton->property("emphasisedLabelFont").value<QFont>().italic(),
                 "the row drew every label italic");
    }

    void testTheEditorAnswersWithoutBeingAWidgetEditor()
    {
        Utils::TemporaryDirectory dir("markdown-editor-shape");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("doc.md");
        QVERIFY(file.writeFileContents("one\ntwo\nthree\n"));

        Core::IEditor * const editor = Core::EditorManager::openEditor(file);
        QVERIFY(editor);
        const QScopeGuard closeIt(
            [editor] { Core::EditorManager::closeEditors({editor}, false); });

        QVERIFY2(!qobject_cast<BaseTextEditor *>(editor),
                 "the Markdown editor is a BaseTextEditor again");

        // No widget pane at all now: the text side is a Qt Quick view.
        QVERIFY2(!TextEditorWidget::fromEditor(editor),
                 "the text pane is a widget editor again");
        TextViewport * const view = Internal::viewportIn(
            editor->widget()->findChild<QtcQuick::QuickWidget *>());
        QVERIFY2(view, "the editor has no Qt Quick text view");

        auto * const document = qobject_cast<TextDocument *>(editor->document());
        QVERIFY(document);
        QCOMPARE(view->textDocument(), document);
        QVERIFY2(editor->toolBar(), "the editor offers no toolbar row");

        // The text pane carries the Markdown text context, so a command
        // registered for it is live only while that pane has focus. Asked of
        // the attachment rather than of focus, which a test that shows no
        // window cannot give it.
        const QList<Core::IContext *> attached = m_textViewOf(editor)->findChildren<Core::IContext *>();
        QVERIFY2(Utils::anyOf(attached, [](Core::IContext *c) {
                     return c->context().contains(Utils::Id(MARKDOWNVIEWER_TEXT_CONTEXT));
                 }),
                 "the text pane carries no Markdown text context");

        // Ctrl+F in the text pane, which comes with the view rather than
        // being arranged here.
        QVERIFY2(Utils::Aggregation::query<Core::IFindSupport>(editor->widget()),
                 "nothing in the editor answers Ctrl+F");

        // Where the caret is, which BaseTextEditor used to answer.
        QTextCursor cursor(document->document());
        cursor.setPosition(document->document()->findBlockByNumber(1).position() + 2);
        view->setTextCursor(cursor);
        QCOMPARE(editor->currentLine(), 2);
        QCOMPARE(editor->currentColumn(), 3);

        // And the state it saves puts the caret back, which is what the
        // navigation history and reopening both ride on.
        const QByteArray state = editor->saveState();
        QTextCursor elsewhere(document->document());
        elsewhere.setPosition(0);
        view->setTextCursor(elsewhere);
        QCOMPARE(editor->currentLine(), 1);
        editor->restoreState(state);
        QCOMPARE(editor->currentLine(), 2);
        QCOMPARE(editor->currentColumn(), 3);
    }

    void testTheToolBarIsDescribedByActions()
    {
        Utils::TemporaryDirectory dir("markdown-toolbar");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("doc.md");
        QVERIFY(file.writeFileContents("word\n"));

        Core::IEditor * const editor = Core::EditorManager::openEditor(file);
        QVERIFY(editor);
        const QScopeGuard closeIt(
            [editor] { Core::EditorManager::closeEditors({editor}, false); });
        QHash<QString, QQuickItem *> drawn;
        QTRY_VERIFY2(!(drawn = toolBarButtons(editor)).isEmpty(),
                     "the toolbar row drew nothing at all");

        const QStringList expected{"Emphasis", "Strong", "Inline Code", "Hyperlink",
                                   "Show Editor", "Show Preview", "Swap Views"};
        QStringList missing;
        for (const QString &name : expected) {
            if (!drawn.contains(name))
                missing << name;
        }
        QVERIFY2(missing.isEmpty(),
                 qPrintable("not drawn in the row: " + missing.join(", ")
                            + " (drawn: " + QStringList(drawn.keys()).join(", ") + ")"));

        // The two that say which views are showing carry that state
        // themselves, and the row draws it.
        QVERIFY2(drawn.value("Show Editor")->property("checkable").toBool(),
                 "Show Editor is not drawn as something that can be checked");
        QVERIFY2(drawn.value("Show Preview")->property("checkable").toBool(),
                 "Show Preview is not drawn as something that can be checked");

        // And pressing one does what the button did.
        auto * const document = qobject_cast<TextDocument *>(editor->document());
        QVERIFY(document);
        TextViewport * const view = Internal::viewportIn(
            editor->widget()->findChild<QtcQuick::QuickWidget *>());
        QVERIFY(view);
        QTextCursor cursor(document->document());
        cursor.setPosition(0);
        cursor.movePosition(QTextCursor::EndOfWord, QTextCursor::KeepAnchor);
        view->setTextCursor(cursor);
        QMetaObject::invokeMethod(drawn.value("Strong"), "clicked");
        QTRY_COMPARE(document->document()->findBlockByNumber(0).text(), QString("**word**"));
    }

    void testFollowingALinkNeedsNoEditorWidget()
    {
        Utils::TemporaryDirectory dir("markdown-links");
        QVERIFY(dir.isValid());
        const Utils::FilePath target = dir.filePath("target.md");
        QVERIFY(target.writeFileContents("# There\n"));
        const Utils::FilePath source = dir.filePath("source.md");
        QVERIFY(source.writeFileContents("See [there](target.md) for more.\n"));

        Core::IEditor * const editor = Core::EditorManager::openEditor(source);
        QVERIFY(editor);
        const QScopeGuard closeIt(
            [editor] { Core::EditorManager::closeEditors({editor}, false); });
        auto * const document = qobject_cast<TextDocument *>(editor->document());
        QVERIFY(document);

        // The document carries it, so anything holding the document can ask -
        // which is what a view that is not a widget has to be able to do.
        const TextEditorFactory::LinkFinder finder
            = TextEditorFactory::linkFinderFor(document);
        QVERIFY2(finder, "a Markdown document offers no link finder");

        // Inside the link text, which is where a reader would be pointing.
        QTextCursor cursor(document->document());
        cursor.setPosition(document->plainText().indexOf("there"));

        Utils::Link found;
        int answers = 0;
        finder(document, cursor, [&found, &answers](const Utils::Link &link) {
            found = link;
            ++answers;
        }, true, false);

        QCOMPARE(answers, 1);
        QCOMPARE(found.targetFilePath, target);
        // The span it would underline, not just the file it points at.
        QVERIFY2(found.linkTextStart >= 0 && found.linkTextEnd > found.linkTextStart,
                 "the link has no extent, so nothing would be drawn as a link");
    }
};

QObject *createMarkdownEditorTest()
{
    return new MarkdownEditorTest;
}

#endif // WITH_TESTS

void findMarkdownLinkAt(TextDocument *document,
                        const QTextCursor &cursor,
                        const LinkHandler &processLinkCallback,
                        bool /*resolveTarget*/,
                        bool /*inNextSplit*/)
{
    QTC_ASSERT(document, return);

    static const QStringView CAPTURE_GROUP_LINK   = u"link";
    static const QStringView CAPTURE_GROUP_ANCHOR = u"anchor";
    static const QStringView CAPTURE_GROUP_RAWURL = u"rawurl";

    static const QRegularExpression markdownLink{
        R"(\[[^[\]]*\]\((?<link>.+?)\))"
        R"(|(?<anchor>\[\^[^\]]+\])(?:[^:]|$))"
        R"(|(?<rawurl>(?:https?|ftp)\://[^">)\s]+))"
    };

    QTC_ASSERT(markdownLink.isValid(), return);

    const int blockOffset = cursor.block().position();
    const QString &currentBlock = cursor.block().text();

    for (const QRegularExpressionMatch &match : markdownLink.globalMatch(currentBlock)) {
        // Ignore matches outside of the current cursor position.
        if (cursor.positionInBlock() < match.capturedStart())
            break;
        if (cursor.positionInBlock() >= match.capturedEnd())
            continue;

        if (const QStringView link = match.capturedView(CAPTURE_GROUP_LINK); !link.isEmpty()) {
            // Process regular Markdown links of the form `[description](link)`.

            const QUrl url(link.toString());
            Link result = MarkdownBrowser::fileLink(url);
            if (result.hasValidTarget()) {
                result.targetFilePath = document->filePath().parentDir().resolvePath(
                    result.targetFilePath);
                if (!result.targetFilePath.isFile())
                    continue;
            } else if (!url.scheme().isEmpty()) {
                result.targetFilePath = FilePath::fromString(url.toString());
            } else {
                continue;
            }
            result.linkTextStart = match.capturedStart() + blockOffset;
            result.linkTextEnd = match.capturedEnd() + blockOffset;
            processLinkCallback(result);
            break;
        } else if (const QStringView anchor = match.capturedView(CAPTURE_GROUP_ANCHOR);
                   !anchor.isEmpty()) {
            // Process local anchor links of the form `[^footnote]` that point
            // to anchors in the current document: `[^footnote]: Description`.

            const QTextCursor target = cursor.document()->find(anchor + u':');

            if (target.isNull())
                continue;

            int line = 0;
            int column = 0;

            Utils::Text::convertPosition(document->document(), target.position(),
                                         &line, &column);
            Link result{document->filePath(), line, column};
            result.linkTextStart = match.capturedStart(CAPTURE_GROUP_ANCHOR) + blockOffset;
            result.linkTextEnd = match.capturedEnd(CAPTURE_GROUP_ANCHOR) + blockOffset;
            processLinkCallback(result);
            break;
        } else if (const QStringView rawurl = match.capturedView(CAPTURE_GROUP_RAWURL);
                   !rawurl.isEmpty()) {
            // Process raw links starting with "http://", "https://", or "ftp://".

            Link result{FilePath::fromString(rawurl.toString())};
            result.linkTextStart = match.capturedStart() + blockOffset;
            result.linkTextEnd = match.capturedEnd() + blockOffset;
            processLinkCallback(result);
            break;
        } else {
            QTC_ASSERT_STRING("This line should not be reached unless 'markdownLink' is wrong");
            return;
        }
    }
}

void setupMarkdownEditor()
{
    static MarkdownEditorFactory theMarkdownEditorFactory;
}

} // namespace TextEditor::Internal

#include "markdowneditor.moc"
