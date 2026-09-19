// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "vcsbaseeditor.h"

#include "baseannotationhighlighter.h"
#include "diffandloghighlighter.h"
#include "vcsbaseeditorconfig.h"
#include "vcsbaseplugin.h"
#include "vcsbasetr.h"
#include "vcscommand.h"
#include "vcsoutputwindow.h"

#include <coreplugin/coreconstants.h>
#include <coreplugin/editormanager/editormanager.h>
#include <coreplugin/editormanager/ieditorfactory.h>
#include <coreplugin/icore.h>
#include <coreplugin/patchtool.h>
#include <coreplugin/vcsmanager.h>

#include <cpaster/codepasterservice.h>

#include <diffeditor/diffeditorconstants.h>

#include <extensionsystem/pluginmanager.h>

#include <projectexplorer/editorconfiguration.h>
#include <projectexplorer/project.h>
#include <projectexplorer/projectexplorer.h>
#include <projectexplorer/projectmanager.h>

#include <solutions/spinner/spinner.h>

#include <utils/fancylineedit.h>


#include <texteditor/marginsettings.h>
#include <texteditor/displaysettings.h>
#include <texteditor/textdocument.h>
#include <texteditor/textdocumentlayout.h>
#include <texteditor/syntaxhighlighter.h>

#include <utils/aggregate.h>
#include <utils/algorithm.h>
#include <utils/ansiescapecodehandler.h>
#include <utils/qtcassert.h>
#include <utils/stringutils.h>

#include <QAction>
#include <QComboBox>
#include <QDebug>
#include <QDesktopServices>
#include <QFile>
#include <QFileInfo>
#include <QKeyEvent>
#include <QMenu>
#include <QRegularExpression>
#include <QSet>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextEdit>
#include <QTimer>
#include <QToolBar>
#include <QUrl>

#include <algorithm>

/*!
    \enum VcsBase::EditorContentType

    This enum describes the contents of a VcsBaseEditor and its interaction.

    \value RegularCommandOutput  No special handling.
    \value LogOutput  Log of a file under revision control. Provide a
           description of the change that users can click to view detailed
           information about the change and \e Annotate for the log of a
           single file.
    \value AnnotateOutput  Color contents per change number and provide a
           clickable change description.
           Context menu offers annotate previous version functionality.
           Expected format:
           \code
           <change description>: file line
           \endcode
    \value DiffOutput  Diff output. Might include describe output, which consists of a
           header and diffs. Double-clicking the chunk opens the file. The context
           menu offers the functionality to revert the chunk.

    \sa VcsBase::VcsBaseEditorWidget
*/

using namespace Core;
using namespace SpinnerSolution;
using namespace QtTaskTree;
using namespace TextEditor;
using namespace Utils;

namespace VcsBase {

} // namespace VcsBase

namespace VcsBase {

/*!
    \class VcsBase::VcsBaseEditor

    \brief The VcsBaseEditor class implements an editor with no support for
    duplicates.

    Creates a browse combo in the toolbar for diff output.
    It also mirrors the signals of the VcsBaseEditor since the editor
    manager passes the editor around.
*/

VcsBaseEditor::VcsBaseEditor()
{
}

void VcsBaseEditor::finalizeInitialization()
{
    QTC_ASSERT(qobject_cast<VcsBaseEditorWidget *>(editorWidget()), return);
    editorWidget()->setReadOnly(true);
}

// ----------- VcsBaseEditorPrivate

namespace Internal {

/*! \class AbstractTextCursorHandler
 *  \brief The AbstractTextCursorHandler class provides an interface to handle
 *  the contents under a text cursor inside an editor.
 */
class AbstractTextCursorHandler : public QObject
{
public:
    AbstractTextCursorHandler(VcsBaseEditorWidget *editorWidget = nullptr);

    /*! Tries to find some matching contents under \a cursor.
     *
     *  It is the first function to be called because it changes the internal
     *  state of the handler. Other functions (such as
     *  highlightCurrentContents() and handleCurrentContents()) use the result
     *  of the matching.
     *
     *  Returns \c true if contents could be found.
     */
    virtual bool findContentsUnderCursor(const QTextCursor &cursor);

    //! Highlight (eg underline) the contents matched with findContentsUnderCursor()
    virtual void highlightCurrentContents() = 0;

    //! React to user-interaction with the contents matched with findContentsUnderCursor()
    virtual void handleCurrentContents() = 0;

    //! Contents matched with the last call to findContentsUnderCursor()
    virtual QString currentContents() const = 0;

    /*! Fills \a menu with contextual actions applying to the contents matched
     *  with findContentsUnderCursor().
     */
    virtual void fillContextMenu(QMenu *menu, EditorContentType type) const = 0;

    //! Editor passed on construction of this handler
    VcsBaseEditorWidget *editorWidget() const;

    //! Text cursor used to match contents with findContentsUnderCursor()
    QTextCursor currentCursor() const;

private:
    VcsBaseEditorWidget *m_editorWidget;
    QTextCursor m_currentCursor;
};

AbstractTextCursorHandler::AbstractTextCursorHandler(VcsBaseEditorWidget *editorWidget)
    : QObject(editorWidget),
      m_editorWidget(editorWidget)
{
}

bool AbstractTextCursorHandler::findContentsUnderCursor(const QTextCursor &cursor)
{
    m_currentCursor = cursor;
    return false;
}

VcsBaseEditorWidget *AbstractTextCursorHandler::editorWidget() const
{
    return m_editorWidget;
}

QTextCursor AbstractTextCursorHandler::currentCursor() const
{
    return m_currentCursor;
}

/*! \class ChangeTextCursorHandler
 *  \brief The ChangeTextCursorHandler class provides a handler for version control change
 *  identifiers.
 */
class ChangeTextCursorHandler : public AbstractTextCursorHandler
{
    Q_OBJECT

public:
    ChangeTextCursorHandler(VcsBaseEditorWidget *editorWidget = nullptr);

    bool findContentsUnderCursor(const QTextCursor &cursor) override;
    void highlightCurrentContents() override;
    void handleCurrentContents() override;
    QString currentContents() const override;
    void fillContextMenu(QMenu *menu, EditorContentType type) const override;

private slots:
    void slotDescribe();
    void slotCopyRevision();

private:
    void addDescribeAction(QMenu *menu, const QString &change) const;
    QAction *createAnnotateAction(const QString &change, bool previous) const;
    QAction *createCopyRevisionAction(const QString &change) const;

    QString m_currentChange;
    int m_changeLine = 0;
};

ChangeTextCursorHandler::ChangeTextCursorHandler(VcsBaseEditorWidget *editorWidget)
    : AbstractTextCursorHandler(editorWidget)
{
}

bool ChangeTextCursorHandler::findContentsUnderCursor(const QTextCursor &cursor)
{
    AbstractTextCursorHandler::findContentsUnderCursor(cursor);
    m_currentChange = editorWidget()->changeUnderCursor(cursor);
    m_changeLine = editorWidget()->originalLineUnderCursor(cursor);
    return !m_currentChange.isEmpty();
}

void ChangeTextCursorHandler::highlightCurrentContents()
{
    QTextEdit::ExtraSelection sel;
    sel.cursor = currentCursor();
    sel.cursor.select(QTextCursor::WordUnderCursor);
    sel.format.setFontUnderline(true);
    sel.format.setProperty(QTextFormat::UserProperty, m_currentChange);
    editorWidget()->setExtraSelections(VcsBaseEditorWidget::OtherSelection,
                                       QList<QTextEdit::ExtraSelection>() << sel);
}

void ChangeTextCursorHandler::handleCurrentContents()
{
    slotDescribe();
}

void ChangeTextCursorHandler::fillContextMenu(QMenu *menu, EditorContentType type) const
{
    VcsBaseEditorWidget *widget = editorWidget();
    switch (type) {
    case AnnotateOutput: { // Describe current / annotate previous
        bool currentValid = widget->isValidRevision(m_currentChange);
        menu->addSeparator();
        menu->addAction(createCopyRevisionAction(m_currentChange));
        if (currentValid)
            addDescribeAction(menu, m_currentChange);
        menu->addSeparator();
        if (currentValid)
            menu->addAction(createAnnotateAction(widget->decorateVersion(m_currentChange), false));
        const QStringList previousVersions = widget->annotationPreviousVersions(m_currentChange);
        if (!previousVersions.isEmpty()) {
            for (const QString &pv : previousVersions)
                menu->addAction(createAnnotateAction(widget->decorateVersion(pv), true));
        }
        break;
    }
    default: // Describe current / Annotate file of current
        menu->addSeparator();
        menu->addAction(createCopyRevisionAction(m_currentChange));
        addDescribeAction(menu, m_currentChange);
        if (widget->isFileLogAnnotateEnabled())
            menu->addAction(createAnnotateAction(m_currentChange, false));
        break;
    }
    widget->addChangeActions(menu, m_currentChange, m_changeLine);
}

QString ChangeTextCursorHandler::currentContents() const
{
    return m_currentChange;
}

void ChangeTextCursorHandler::slotDescribe()
{
    emit editorWidget()->describeRequested(editorWidget()->source(), m_currentChange);
}

void ChangeTextCursorHandler::slotCopyRevision()
{
    setClipboardAndSelection(m_currentChange);
}

void ChangeTextCursorHandler::addDescribeAction(QMenu *menu, const QString &change) const
{
    auto a = new QAction(Tr::tr("&Describe Change %1").arg(change), nullptr);
    connect(a, &QAction::triggered, this, &ChangeTextCursorHandler::slotDescribe);
    menu->addAction(a);
    menu->setDefaultAction(a);
}

QAction *ChangeTextCursorHandler::createAnnotateAction(const QString &change, bool previous) const
{
    // Use 'previous' format if desired and available, else default to standard.
    const QString &format =
            previous && !editorWidget()->annotatePreviousRevisionTextFormat().isEmpty() ?
                editorWidget()->annotatePreviousRevisionTextFormat() :
                editorWidget()->annotateRevisionTextFormat();
    auto a = new QAction(format.arg(change), nullptr);
    VcsBaseEditorWidget *editor = editorWidget();
    connect(a, &QAction::triggered, editor, [editor, change] {
        editor->slotAnnotateRevision(change);
    });
    return a;
}

QAction *ChangeTextCursorHandler::createCopyRevisionAction(const QString &change) const
{
    auto a = new QAction(Tr::tr("Copy \"%1\"").arg(change), nullptr);
    a->setData(change);
    connect(a, &QAction::triggered, this, &ChangeTextCursorHandler::slotCopyRevision);
    return a;
}

/*! \class UrlTextCursorHandler
 *  \brief The UrlTextCursorHandler class provides a handler for URLs, such as
 *  http://qt-project.org/.
 *
 *  The URL pattern can be redefined in sub-classes with setUrlPattern(), by default the pattern
 *  works for hyper-text URLs.
 */
class UrlTextCursorHandler : public AbstractTextCursorHandler
{
    Q_OBJECT

public:
    UrlTextCursorHandler(VcsBaseEditorWidget *editorWidget = nullptr);

    bool findContentsUnderCursor(const QTextCursor &cursor) override;
    void highlightCurrentContents() override;
    void handleCurrentContents() override;
    void fillContextMenu(QMenu *menu, EditorContentType type) const override;
    QString currentContents() const override;

protected slots:
    virtual void slotCopyUrl();
    virtual void slotOpenUrl();

protected:
    // What this handler is for under \a cursor: the same finder the document
    // offers a view that is not this widget.
    virtual UrlUnderCursor urlAt(const QTextCursor &cursor) const;
    QAction *createOpenUrlAction(const QString &text) const;
    QAction *createCopyUrlAction(const QString &text) const;

private:
    class UrlData
    {
    public:
        int startColumn = 0;
        QString url;
        qsizetype urlLength = 0;
    };

    UrlData m_urlData;
};

UrlTextCursorHandler::UrlTextCursorHandler(VcsBaseEditorWidget *editorWidget)
    : AbstractTextCursorHandler(editorWidget)
{
}

bool UrlTextCursorHandler::findContentsUnderCursor(const QTextCursor &cursor)
{
    AbstractTextCursorHandler::findContentsUnderCursor(cursor);

    const UrlUnderCursor found = urlAt(cursor);
    m_urlData.url = found.url;
    m_urlData.startColumn = found.isValid() ? found.startColumn : -1;
    m_urlData.urlLength = found.length;
    return found.isValid();
}

UrlUnderCursor UrlTextCursorHandler::urlAt(const QTextCursor &cursor) const
{
    return urlUnderCursor(cursor);
}

void UrlTextCursorHandler::highlightCurrentContents()
{
    const QColor linkColor = creatorColor(Theme::TextColorLink);
    QTextEdit::ExtraSelection sel;
    sel.cursor = currentCursor();
    sel.cursor.setPosition(currentCursor().position()
                           - (currentCursor().columnNumber() - m_urlData.startColumn));
    sel.cursor.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor, m_urlData.urlLength);
    sel.format.setFontUnderline(true);
    sel.format.setForeground(linkColor);
    sel.format.setUnderlineColor(linkColor);
    sel.format.setProperty(QTextFormat::UserProperty, m_urlData.url);
    editorWidget()->setExtraSelections(VcsBaseEditorWidget::OtherSelection,
                                       QList<QTextEdit::ExtraSelection>() << sel);
}

void UrlTextCursorHandler::handleCurrentContents()
{
    slotOpenUrl();
}

void UrlTextCursorHandler::fillContextMenu(QMenu *menu, EditorContentType type) const
{
    Q_UNUSED(type)
    menu->addSeparator();
    menu->addAction(createOpenUrlAction(Tr::tr("Open URL in Browser...")));
    menu->addAction(createCopyUrlAction(Tr::tr("Copy URL Location")));
}

QString UrlTextCursorHandler::currentContents() const
{
    return  m_urlData.url;
}

void UrlTextCursorHandler::slotCopyUrl()
{
    setClipboardAndSelection(m_urlData.url);
}

void UrlTextCursorHandler::slotOpenUrl()
{
    QDesktopServices::openUrl(QUrl(m_urlData.url));
}

QAction *UrlTextCursorHandler::createOpenUrlAction(const QString &text) const
{
    auto a = new QAction(text);
    a->setData(m_urlData.url);
    connect(a, &QAction::triggered, this, &UrlTextCursorHandler::slotOpenUrl);
    return a;
}

QAction *UrlTextCursorHandler::createCopyUrlAction(const QString &text) const
{
    auto a = new QAction(text);
    a->setData(m_urlData.url);
    connect(a, &QAction::triggered, this, &UrlTextCursorHandler::slotCopyUrl);
    return a;
}

/*! \class EmailTextCursorHandler
 *  \brief The EmailTextCursorHandler class provides a handler for email
 *  addresses.
 */
class EmailTextCursorHandler : public UrlTextCursorHandler
{
    Q_OBJECT

public:
    EmailTextCursorHandler(VcsBaseEditorWidget *editorWidget = nullptr);
    void fillContextMenu(QMenu *menu, EditorContentType type) const override;

protected slots:
    void slotOpenUrl() override;

protected:
    UrlUnderCursor urlAt(const QTextCursor &cursor) const override
    {
        return emailUnderCursor(cursor);
    }
};

EmailTextCursorHandler::EmailTextCursorHandler(VcsBaseEditorWidget *editorWidget)
    : UrlTextCursorHandler(editorWidget)
{
}

void EmailTextCursorHandler::fillContextMenu(QMenu *menu, EditorContentType type) const
{
    Q_UNUSED(type)
    menu->addSeparator();
    menu->addAction(createOpenUrlAction(Tr::tr("Send Email To...")));
    menu->addAction(createCopyUrlAction(Tr::tr("Copy Email Address")));
}

void EmailTextCursorHandler::slotOpenUrl()
{
    QDesktopServices::openUrl(QUrl("mailto:" + currentContents()));
}

class VcsBaseEditorWidgetPrivate
{
public:
    VcsBaseEditorWidgetPrivate(VcsBaseEditorWidget *editorWidget);

    AbstractTextCursorHandler *findTextCursorHandler(const QTextCursor &cursor);
    // creates a browse combo in the toolbar for quick access to entries.
    // Can be used for diff and log. Combo created on first call.
    QComboBox *entriesComboBox();

    TextEditorWidget *q;
    // What a subclass declares in its constructor, before there is a document
    // to keep it: handed over in finalizeInitialization() and read only until
    // then. The document is the one place for all of it afterwards.
    VcsBaseEditorParameters m_parameters;
    QString m_diffFilePattern;
    QString m_logEntryPattern;
    QString m_annotationEntryPattern;
    QString m_annotationSeparatorPattern;
    QString m_annotateRevisionTextFormat;
    QString m_annotatePreviousRevisionTextFormat;

    int m_cursorLine = -1;
    QList<AbstractTextCursorHandler *> m_textCursorHandlers;
    bool m_mouseDragging = false;
    bool m_marginsEnabled = false;

    Spinner *m_spinner = nullptr;

private:
    QComboBox *m_entriesComboBox = nullptr;
};

VcsBaseEditorWidgetPrivate::VcsBaseEditorWidgetPrivate(VcsBaseEditorWidget *editorWidget)  :
    q(editorWidget)
{
    m_textCursorHandlers.append(new ChangeTextCursorHandler(editorWidget));
    m_textCursorHandlers.append(new UrlTextCursorHandler(editorWidget));
    m_textCursorHandlers.append(new EmailTextCursorHandler(editorWidget));
}

AbstractTextCursorHandler *VcsBaseEditorWidgetPrivate::findTextCursorHandler(const QTextCursor &cursor)
{
    for (AbstractTextCursorHandler *handler : std::as_const(m_textCursorHandlers)) {
        if (handler->findContentsUnderCursor(cursor))
            return handler;
    }
    return nullptr;
}

QComboBox *VcsBaseEditorWidgetPrivate::entriesComboBox()
{
    if (m_entriesComboBox)
        return m_entriesComboBox;
    m_entriesComboBox = new QComboBox;
    m_entriesComboBox->setMinimumContentsLength(20);
    // Make the combo box prefer to expand
    QSizePolicy policy = m_entriesComboBox->sizePolicy();
    policy.setHorizontalPolicy(QSizePolicy::Expanding);
    m_entriesComboBox->setSizePolicy(policy);

    q->insertExtraToolBarWidget(TextEditorWidget::Left, m_entriesComboBox);
    return m_entriesComboBox;
}

} // namespace Internal

/*!
    \class VcsBase::VcsBaseEditorParameters

    \brief The VcsBaseEditorParameters class is a helper class used to
    parametrize an editor with MIME type, context
    and id.

    The extension is currently only a suggestion when running
    version control commands with redirection.

    \sa VcsBase::VcsBaseEditorWidget, VcsBase::BaseVcsEditorFactory, VcsBase::EditorContentType
*/

/*!
    \class VcsBase::VcsBaseEditorWidget

    \brief The VcsBaseEditorWidget class is the base class for editors showing
    version control system output
    of the type enumerated by EditorContentType.

    The source property should contain the file or directory the log
    refers to and will be emitted with describeRequested().
    This is for version control systems that need a current directory.

    \sa VcsBase::BaseVcsEditorFactory, VcsBase::VcsBaseEditorParameters, VcsBase::EditorContentType
*/

VcsBaseEditorWidget::VcsBaseEditorWidget()
  : d(new Internal::VcsBaseEditorWidgetPrivate(this))
{
    viewport()->setMouseTracking(true);
}

VcsBaseDescriptionEditorWidget::VcsBaseDescriptionEditorWidget(
    const VcsBaseDescriptionEditorParameters &parameters, QWidget *parent)
    : VcsBaseEditorWidget()
    , m_parameters(parameters)
{
    setParent(parent);
    setupFallBackEditor("VcsBase.DescriptionEditor");

    DisplaySettingsData settings = displaySettings();
    settings.m_textWrapping = false;
    settings.m_displayLineNumbers = false;
    settings.m_displayFoldingMarkers = false;
    settings.m_markTextChanges = false;
    settings.m_highlightBlocks = false;
    TextEditorWidget::setDisplaySettings(settings);

    setCodeFoldingSupported(true);
    setFrameStyle(QFrame::NoFrame);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    IContext::attach(this, Core::Context(DiffEditor::Constants::C_DIFF_EDITOR_DESCRIPTION));
    textDocument()->resetSyntaxHighlighter([] { return new SyntaxHighlighter(); });

    VcsBaseEditorParameters editorParameters;
    editorParameters.type = OtherContent;
    editorParameters.id = "VcsBase.DescriptionEditor";
    editorParameters.displayName = Tr::tr("Version Control Description");
    editorParameters.describeFunc = [this](const FilePath &source, const QString &change) {
        if (m_parameters.describe)
            m_parameters.describe(source, change);
    };
    setParameters(editorParameters);
    setSource(parameters.source);
    setReadOnly(true);
    VcsBaseEditorWidget::finalizeInitialization();
}

QSize VcsBaseDescriptionEditorWidget::sizeHint() const
{
    QSize size = TextEditorWidget::sizeHint();
    size.setHeight(size.height() / 5);
    return size;
}

void VcsBaseDescriptionEditorWidget::setDescription(const QString &text, bool ansiEnabled)
{
    if (ansiEnabled)
        AnsiEscapeCodeHandler::setTextInDocument(document(), text);
    else
        textDocument()->setPlainText(text);
}

bool VcsBaseDescriptionEditorWidget::supportChangeLinks() const
{
    return true;
}

QString VcsBaseDescriptionEditorWidget::changeUnderCursor(const QTextCursor &cursor) const
{
    return m_parameters.changeUnderCursor ? m_parameters.changeUnderCursor(cursor) : QString();
}

void VcsBaseDescriptionEditorWidget::addChangeActions(QMenu *menu, const QString &change, int line)
{
    if (m_parameters.addChangeActions)
        m_parameters.addChangeActions(menu, change, line);
}

bool VcsBaseDescriptionEditorWidget::isValidRevision(const QString &revision) const
{
    return m_parameters.isValidRevision ? m_parameters.isValidRevision(revision) : true;
}

void VcsBaseEditorWidget::setParameters(const VcsBaseEditorParameters &parameters)
{
    d->m_parameters = parameters;
}

VcsEditorDocument *VcsBaseEditorWidget::vcsDocument() const
{
    return qobject_cast<VcsEditorDocument *>(textDocument());
}

void VcsBaseEditorWidget::setDiffFilePattern(const QString &pattern)
{
    if (VcsEditorDocument * const document = vcsDocument())
        document->setDiffFilePattern(pattern);
    else
        d->m_diffFilePattern = pattern;
}

void VcsBaseEditorWidget::setLogEntryPattern(const QString &pattern)
{
    if (VcsEditorDocument * const document = vcsDocument())
        document->setLogEntryPattern(pattern);
    else
        d->m_logEntryPattern = pattern;
}

void VcsBaseEditorWidget::setAnnotationEntryPattern(const QString &pattern)
{
    if (VcsEditorDocument * const document = vcsDocument())
        document->setAnnotationEntryPattern(pattern);
    else
        d->m_annotationEntryPattern = pattern;
}

void VcsBaseEditorWidget::setAnnotationSeparatorPattern(const QString &pattern)
{
    if (VcsEditorDocument * const document = vcsDocument())
        document->setAnnotationSeparatorPattern(pattern);
    else
        d->m_annotationSeparatorPattern = pattern;
}

bool VcsBaseEditorWidget::supportChangeLinks() const
{
    switch (contentType()) {
    case LogOutput:
    case AnnotateOutput:
        return true;
    default:
        return false;
    }
}

void VcsBaseEditorWidget::setMarginsEnabled(bool enabled)
{
    d->m_marginsEnabled = enabled;
}

FilePath VcsBaseEditorWidget::fileNameForLine(int line) const
{
    Q_UNUSED(line)
    return source();
}

int VcsBaseEditorWidget::firstLineNumber() const
{
    const TextDocument * const document = textDocument();
    return document ? document->firstLineNumber() : 1;
}

void VcsBaseEditorWidget::setFirstLineNumber(int firstLineNumber)
{
    TextDocument * const document = textDocument();
    QTC_ASSERT(document, return);
    document->setFirstLineNumber(firstLineNumber);
}

void VcsBaseEditorWidget::finalizeInitialization()
{
    VcsEditorDocument * const document = vcsDocument();
    QTC_ASSERT(document, return);
    // What the constructor declared before there was a document.
    if (!d->m_diffFilePattern.isEmpty())
        document->setDiffFilePattern(d->m_diffFilePattern);
    if (!d->m_logEntryPattern.isEmpty())
        document->setLogEntryPattern(d->m_logEntryPattern);
    if (!d->m_annotationEntryPattern.isEmpty())
        document->setAnnotationEntryPattern(d->m_annotationEntryPattern);
    if (!d->m_annotationSeparatorPattern.isEmpty())
        document->setAnnotationSeparatorPattern(d->m_annotationSeparatorPattern);
    if (!d->m_annotateRevisionTextFormat.isEmpty())
        document->setAnnotateRevisionTextFormat(d->m_annotateRevisionTextFormat);
    if (!d->m_annotatePreviousRevisionTextFormat.isEmpty())
        document->setAnnotatePreviousRevisionTextFormat(d->m_annotatePreviousRevisionTextFormat);

    QTC_CHECK(document->parameters().describeFunc);
    connect(this, &VcsBaseEditorWidget::describeRequested, this, document->parameters().describeFunc);
    // What the document's own menu entries ask for reaches whoever listens
    // to this widget, as the widget's entries' requests do.
    connect(document, &VcsEditorDocument::annotateRevisionRequested,
            this, &VcsBaseEditorWidget::annotateRevisionRequested);
    connect(document, &VcsEditorDocument::diffChunkReverted,
            this, &VcsBaseEditorWidget::diffChunkReverted);
    // A command's output goes through setPlainText(), which a subclass may
    // override to reshape it; and while the command runs the document is
    // busy, shown here after a moment, so that a command that answers at
    // once flashes nothing.
    document->setOutputHook([this](const QString &output) { setPlainText(output); });
    d->m_spinner = new Spinner(SpinnerSize::Large, this);
    d->m_spinner->hide();
    connect(document, &TextDocument::busyChanged, this, [this] {
        const TextDocument * const doc = textDocument();
        if (doc && doc->isBusy()) {
            QTimer::singleShot(100, this, [this] {
                const TextDocument * const stillDoc = textDocument();
                if (stillDoc && stillDoc->isBusy())
                    d->m_spinner->show();
            });
        } else {
            d->m_spinner->hide();
        }
    });
    init();
}

void VcsBaseEditorWidget::init()
{
    VcsEditorDocument * const document = vcsDocument();
    QTC_ASSERT(document, return);
    switch (document->contentType()) {
    case OtherContent:
        break;
    case LogOutput:
    case DiffOutput:
        // The document finds the sections; the browser in the tool bar shows
        // them, follows the caret through them, and jumps to the one chosen.
        // What a diff header stands for on disk and what a log entry is about
        // are this widget's to say, so it says them to the document.
        document->setDiffFileResolver([this](const QString &fileName) { return findDiffFile(fileName); });
        if (!document->parameters().revisionSubject) {
            document->setRevisionSubjectHook(
                [this](const QTextBlock &block) { return revisionSubject(block); });
        }
        connect(document->sections(), &QAbstractItemModel::modelReset,
                this, &VcsBaseEditorWidget::refillEntriesComboBox);
        refillEntriesComboBox();
        connect(d->entriesComboBox(), &QComboBox::activated,
                this, &VcsBaseEditorWidget::slotJumpToEntry);
        connect(this, &PlainTextEdit::cursorPositionChanged,
                this, &VcsBaseEditorWidget::slotCursorPositionChanged);
        break;
    case AnnotateOutput:
        // What colours the annotation is this widget's to say where its VCS's
        // parameters do not - until they do.
        if (!document->parameters().annotationHighlighterCreator)
            document->setAnnotationHighlighterCreator(annotationHighlighterCreator());
        break;
    }
    // The document highlights a log or a diff itself; the view folds.
    if (hasDiff())
        setCodeFoldingSupported(true);
    // override revisions display (green or red bar on the left, marking changes):
    setRevisionsVisible(false);
}

VcsBaseEditorWidget::~VcsBaseEditorWidget()
{
    delete d;
}

void VcsBaseEditorWidget::setForceReadOnly(bool b)
{
    setReadOnly(b);
    textDocument()->setTemporary(b);
}

FilePath VcsBaseEditorWidget::source() const
{
    return VcsBase::source(textDocument());
}

void VcsBaseEditorWidget::setSource(const FilePath &source)
{
    VcsBase::setSource(textDocument(), source);
}

QString VcsBaseEditorWidget::annotateRevisionTextFormat() const
{
    const VcsEditorDocument * const document = vcsDocument();
    return document ? document->annotateRevisionTextFormat() : d->m_annotateRevisionTextFormat;
}

void VcsBaseEditorWidget::setAnnotateRevisionTextFormat(const QString &f)
{
    if (VcsEditorDocument * const document = vcsDocument())
        document->setAnnotateRevisionTextFormat(f);
    else
        d->m_annotateRevisionTextFormat = f;
}

QString VcsBaseEditorWidget::annotatePreviousRevisionTextFormat() const
{
    const VcsEditorDocument * const document = vcsDocument();
    return document ? document->annotatePreviousRevisionTextFormat()
                    : d->m_annotatePreviousRevisionTextFormat;
}

void VcsBaseEditorWidget::setAnnotatePreviousRevisionTextFormat(const QString &f)
{
    if (VcsEditorDocument * const document = vcsDocument())
        document->setAnnotatePreviousRevisionTextFormat(f);
    else
        d->m_annotatePreviousRevisionTextFormat = f;
}

bool VcsBaseEditorWidget::isFileLogAnnotateEnabled() const
{
    const VcsEditorDocument * const document = vcsDocument();
    return document && document->isFileLogAnnotateEnabled();
}

void VcsBaseEditorWidget::setFileLogAnnotateEnabled(bool e)
{
    VcsEditorDocument * const document = vcsDocument();
    QTC_ASSERT(document, return);
    document->setFileLogAnnotateEnabled(e);
}

void VcsBaseEditorWidget::setHighlightingEnabled(bool e)
{
    if (VcsEditorDocument * const document = vcsDocument())
        document->setHighlightingEnabled(e);
}

FilePath VcsBaseEditorWidget::workingDirectory() const
{
    const VcsEditorDocument * const document = vcsDocument();
    return document ? document->workingDirectory() : FilePath();
}

void VcsBaseEditorWidget::setWorkingDirectory(const FilePath &wd)
{
    VcsEditorDocument * const document = vcsDocument();
    QTC_ASSERT(document, return);
    document->setWorkingDirectory(wd);
}

TextEncoding VcsBaseEditorWidget::encoding() const
{
    return textDocument()->encoding();
}

void VcsBaseEditorWidget::setEncoding(const TextEncoding &encoding)
{
    if (encoding.isValid())
        textDocument()->setEncoding(encoding);
    else
        qWarning("%s: Attempt to set invalid encoding.", Q_FUNC_INFO);
}

EditorContentType VcsBaseEditorWidget::contentType() const
{
    const VcsEditorDocument * const document = vcsDocument();
    return document ? document->contentType() : d->m_parameters.type;
}

void VcsBaseEditorWidget::refillEntriesComboBox()
{
    VcsEditorDocument * const document = vcsDocument();
    QTC_ASSERT(document, return);
    QComboBox * const entriesComboBox = d->entriesComboBox();
    entriesComboBox->clear();
    entriesComboBox->addItems(document->sections()->entries());
}

void VcsBaseEditorWidget::slotJumpToEntry(int index)
{
    // goto diff/log entry as indicated by index/line number
    VcsEditorDocument * const document = vcsDocument();
    QTC_ASSERT(document, return);
    const QList<int> sections = document->sections()->lines();
    if (index < 0 || index >= sections.size())
        return;
    const int lineNumber = sections.at(index) + 1; // TextEdit uses 1..n convention
    // check if we need to do something, especially to avoid messing up navigation history
    int currentLine, currentColumn;
    convertPosition(position(), &currentLine, &currentColumn);
    if (lineNumber != currentLine) {
        EditorManager::addCurrentPositionToNavigationHistory();
        gotoLine(lineNumber, 0);
    }
}

QString VcsBaseEditorWidget::revisionForLine(int line) const
{
    const VcsEditorDocument * const document = vcsDocument();
    return document ? document->revisionForLine(line) : QString();
}

void VcsBaseEditorWidget::slotCursorPositionChanged()
{
    // Adapt entries combo to new position
    // if the cursor goes across a file line.
    const int newCursorLine = textCursor().blockNumber();
    if (newCursorLine != d->m_cursorLine) {
        // Which section does it belong to?
        d->m_cursorLine = newCursorLine;
        const VcsEditorDocument * const document = vcsDocument();
        const int section = document ? document->sections()->sectionOfLine(d->m_cursorLine) : -1;
        if (section != -1) {
            QComboBox *entriesComboBox = d->entriesComboBox();
            if (entriesComboBox->currentIndex() != section) {
                QSignalBlocker blocker(entriesComboBox);
                entriesComboBox->setCurrentIndex(section);
            }
        }
    }
    TextEditorWidget::slotCursorPositionChanged();
}

void VcsBaseEditorWidget::contextMenuEvent(QContextMenuEvent *e)
{
    QPointer<QMenu> menu;
    // 'click on change-interaction'
    if (supportChangeLinks()) {
        const QTextCursor cursor = cursorForPosition(e->pos());
        if (Internal::AbstractTextCursorHandler *handler = d->findTextCursorHandler(cursor)) {
            menu = new QMenu;
            handler->fillContextMenu(menu, contentType());
        }
    }
    if (!menu) {
        menu = new QMenu;
        appendStandardContextMenuActions(menu);
    }
    switch (contentType()) {
    case LogOutput: // log might have diff
    case DiffOutput: {
        if (ExtensionSystem::PluginManager::getObject<CodePaster::Service>()) {
            // optional code pasting service
            menu->addSeparator();
            connect(menu->addAction(Tr::tr("Send to CodePaster...")), &QAction::triggered,
                    this, &VcsBaseEditorWidget::slotPaste);
        }
        menu->addSeparator();
        // Apply/revert diff chunk.
        const DiffChunk chunk = diffChunk(cursorForPosition(e->pos()));
        if (!canApplyDiffChunk(chunk))
            break;
        // Apply a chunk from a diff loaded into the editor. This typically will
        // not have the 'source' property set and thus will only work if the working
        // directory matches that of the patch (see findDiffFile()). In addition,
        // the user has "Open With" and choose the right diff editor so that
        // fileNameFromDiffSpecification() works.
        QAction *applyAction = menu->addAction(Tr::tr("Apply Chunk..."));
        connect(
            applyAction,
            &QAction::triggered,
            this,
            [this, chunk] { slotApplyDiffChunk(chunk, PatchAction::Apply); },
            Qt::QueuedConnection);
        // Revert a chunk from a version control diff, which might be linked to reloading the diff.
        QAction *revertAction = menu->addAction(Tr::tr("Revert Chunk..."));
        connect(
            revertAction,
            &QAction::triggered,
            this,
            [this, chunk] { slotApplyDiffChunk(chunk, PatchAction::Revert); },
            Qt::QueuedConnection);
        // Custom diff actions
        addDiffActions(menu, chunk);
        break;
    }
    default:
        break;
    }
    connect(this, &QObject::destroyed, menu.data(), &QObject::deleteLater);
    menu->exec(e->globalPos());
    delete menu;
}

void VcsBaseEditorWidget::mouseMoveEvent(QMouseEvent *e)
{
    if (e->buttons()) {
        d->m_mouseDragging = true;
        TextEditorWidget::mouseMoveEvent(e);
        return;
    }

    bool overrideCursor = false;
    Qt::CursorShape cursorShape;

    if (supportChangeLinks()) {
        // Link emulation behaviour for 'click on change-interaction'
        const QTextCursor cursor = cursorForPosition(e->pos());
        Internal::AbstractTextCursorHandler *handler = d->findTextCursorHandler(cursor);
        if (handler != nullptr) {
            handler->highlightCurrentContents();
            overrideCursor = true;
            cursorShape = Qt::PointingHandCursor;
        } else {
            setExtraSelections(OtherSelection, QList<QTextEdit::ExtraSelection>());
            overrideCursor = true;
            cursorShape = Qt::IBeamCursor;
        }
    }
    TextEditorWidget::mouseMoveEvent(e);

    if (overrideCursor)
        viewport()->setCursor(cursorShape);
}

void VcsBaseEditorWidget::mouseReleaseEvent(QMouseEvent *e)
{
    const bool wasDragging = d->m_mouseDragging;
    d->m_mouseDragging = false;
    if (!wasDragging && supportChangeLinks()) {
        if (e->button() == Qt::LeftButton &&!(e->modifiers() & Qt::ShiftModifier)) {
            const QTextCursor cursor = cursorForPosition(e->pos());
            Internal::AbstractTextCursorHandler *handler = d->findTextCursorHandler(cursor);
            if (handler != nullptr) {
                handler->handleCurrentContents();
                e->accept();
                return;
            }
        }
    }
    TextEditorWidget::mouseReleaseEvent(e);
}

void VcsBaseEditorWidget::mouseDoubleClickEvent(QMouseEvent *e)
{
    if (hasDiff() && e->button() == Qt::LeftButton && !(e->modifiers() & Qt::ShiftModifier)) {
        QTextCursor cursor = cursorForPosition(e->pos());
        jumpToChangeFromDiff(cursor);
    }
    TextEditorWidget::mouseDoubleClickEvent(e);
}

void VcsBaseEditorWidget::keyPressEvent(QKeyEvent *e)
{
    // Do not intercept return in editable patches.
    if (hasDiff() && isReadOnly() && (e->key() == Qt::Key_Enter || e->key() == Qt::Key_Return)) {
        jumpToChangeFromDiff(textCursor());
        return;
    }
    TextEditorWidget::keyPressEvent(e);
}

void VcsBaseEditorWidget::setMarginSettings(const TextEditor::MarginSettingsData &ms)
{
    if (d->m_marginsEnabled)
        TextEditorWidget::setMarginSettings(ms);
    else
        TextEditorWidget::setMarginSettings({});
}

BaseAnnotationHighlighterCreator VcsBaseEditorWidget::annotationHighlighterCreator() const
{
    return {};
}

void VcsBaseEditorWidget::jumpToChangeFromDiff(QTextCursor cursor)
{
    const VcsEditorDocument * const document = vcsDocument();
    QTC_ASSERT(document, return);
    const DiffTarget target = document->diffTargetAt(cursor);
    if (target.isValid())
        jumpToDiffTarget(target.filePath, target.line, target.contextBlock);
}

void VcsBaseEditorWidget::jumpToDiffTarget(const FilePath &filePath,
                                           int lineNumber,
                                           const QTextBlock &contextBlock)
{
    Q_UNUSED(contextBlock)
    if (IEditor *ed = EditorManager::openEditor(filePath))
        ed->gotoLine(lineNumber);
}

DiffChunk VcsBaseEditorWidget::diffChunk(QTextCursor cursor) const
{
    const VcsEditorDocument * const document = vcsDocument();
    QTC_ASSERT(document && hasDiff(), return {});
    return document->diffChunk(cursor);
}

// Find the codec used for a file querying the editor.
static TextEncoding findFileCodec(const FilePath &source)
{
    IDocument *document = DocumentModel::documentForFilePath(source);
    if (auto textDocument = qobject_cast<BaseTextDocument *>(document))
        return textDocument->encoding();
    return {};
}

// Find the codec by checking the projects (root dir of project file)
static TextEncoding findProjectEncoding(const FilePath &dirPath)
{
    // Find the project dirPath relates to. A plain equality check would only
    // match when dirPath is exactly the project root, missing both files in
    // subdirectories (dirPath below the project) and repository directories
    // that host the project in a subdirectory (dirPath above the project, as
    // happens for "git show", which runs at the repository top level).
    const auto projects = ProjectExplorer::ProjectManager::projects();

    // Prefer the most specific project whose file tree contains dirPath.
    const ProjectExplorer::Project *containing = nullptr;
    for (const ProjectExplorer::Project *p : projects) {
        const FilePath projectDir = p->projectDirectory();
        if (dirPath != projectDir && !dirPath.isChildOf(projectDir))
            continue;
        if (!containing
            || containing->projectDirectory().path().size() < projectDir.path().size()) {
            containing = p;
        }
    }
    if (containing)
        return containing->editorConfiguration()->textEncoding();

    // Otherwise accept a project located below dirPath (repository hosting it).
    const auto *p = findOrDefault(projects, [&dirPath](const ProjectExplorer::Project *p) {
        return p->projectDirectory().isChildOf(dirPath);
    });
    return p ? p->editorConfiguration()->textEncoding() : TextEncoding();
}

TextEncoding VcsBaseEditor::getEncoding(const FilePath &source)
{
    if (!source.isEmpty()) {
        // Check file
        if (source.isFile())
            if (TextEncoding fc = findFileCodec(source); fc.isValid())
                return fc;
        // Find by project via directory
        if (TextEncoding pc = findProjectEncoding(source.isFile() ? source.absolutePath() : source); pc.isValid())
            return pc;
    }
    return QStringConverter::System;
}

TextEncoding VcsBaseEditor::getEncoding(const FilePath &workingDirectory, const QStringList &files)
{
    if (files.empty())
        return getEncoding(workingDirectory);
    return getEncoding(workingDirectory / files.front());
}

VcsBaseEditorWidget *VcsBaseEditor::getVcsBaseEditor(const IEditor *editor)
{
    if (auto be = qobject_cast<const BaseTextEditor *>(editor))
        return qobject_cast<VcsBaseEditorWidget *>(be->editorWidget());
    return nullptr;
}

// Return line number of current editor if it matches.
int VcsBaseEditor::lineNumberOfCurrentEditor(const FilePath &currentFile)
{
    IEditor *ed = EditorManager::currentEditor();
    if (!ed)
        return -1;
    if (!currentFile.isEmpty()) {
        const IDocument *idocument  = ed->document();
        if (!idocument || idocument->filePath() != currentFile)
            return -1;
    }
    auto eda = qobject_cast<const BaseTextEditor *>(ed);
    if (!eda)
        return -1;
    const int cursorLine = eda->textCursor().blockNumber() + 1;
    if (auto edw = qobject_cast<const TextEditorWidget *>(ed->widget())) {
        const int firstLine = edw->firstVisibleBlockNumber() + 1;
        const int lastLine = edw->lastVisibleBlockNumber() + 1;
        if (firstLine <= cursorLine && cursorLine < lastLine)
            return cursorLine;
        return edw->centerVisibleBlockNumber() + 1;
    }
    return cursorLine;
}

bool VcsBaseEditor::gotoLineOfEditor(IEditor *e, int lineNumber)
{
    if (lineNumber >= 0 && e) {
        e->gotoLine(lineNumber);
        return true;
    }
    return false;
}

// Return source file or directory string depending on parameters
// ('git diff XX' -> 'XX' , 'git diff XX file' -> 'XX/file').
FilePath VcsBaseEditor::getSource(const FilePath &workingDirectory, const QString &fileName)
{
    return workingDirectory.resolvePath(fileName);
}

FilePath VcsBaseEditor::getSource(const FilePath &workingDirectory, const QStringList &fileNames)
{
    return fileNames.size() == 1
            ? getSource(workingDirectory, fileNames.front())
            : workingDirectory;
}

QString VcsBaseEditor::getTitleId(const FilePath &workingDirectory,
                                  const QStringList &fileNames,
                                  const QString &revision)
{
    QStringList nonEmptyFileNames;
    for (const QString &fileName : fileNames) {
        if (!fileName.trimmed().isEmpty())
            nonEmptyFileNames.append(fileName);
    }

    QString rc;
    switch (nonEmptyFileNames.size()) {
    case 0:
        rc = workingDirectory.toUrlishString();
        break;
    case 1:
        rc = nonEmptyFileNames.front();
        break;
    default:
        rc = nonEmptyFileNames.join(", ");
        break;
    }
    if (!revision.isEmpty()) {
        rc += ':';
        rc += revision;
    }
    return rc;
}

void VcsBaseEditorWidget::setEditorConfig(VcsBaseEditorConfig *config)
{
    VcsEditorDocument * const document = vcsDocument();
    QTC_ASSERT(document, return);
    document->setEditorConfig(config);
    if (!config)
        return;
    // The toggles reach the tool bar as the document's actions. A choice is a
    // combo box here, kept in step with the choice both ways.
    toolBar()->setToolButtonStyle(Qt::ToolButtonIconOnly);
    const QList<VcsBaseEditorChoice *> choices = config->choices();
    for (VcsBaseEditorChoice * const choice : choices) {
        auto combo = new QComboBox;
        combo->setToolTip(choice->toolTip());
        for (int i = 0; i < choice->count(); ++i)
            combo->addItem(choice->model()->index(i, 0).data().toString());
        combo->setCurrentIndex(choice->currentIndex());
        connect(combo, &QComboBox::currentIndexChanged, choice, &VcsBaseEditorChoice::choose);
        connect(choice, &TextEditor::ToolBarChoice::changed, combo, [combo, choice] {
            const QSignalBlocker blocker(combo);
            combo->setCurrentIndex(choice->currentIndex());
        });
        insertExtraToolBarWidget(Left, combo);
    }
    // A field is a line edit here: the reader's typing goes to the field,
    // Return and the clear button commit to it, and it is shown as the field
    // is - through the action the tool bar wraps it in, which is what the
    // tool bar lays out.
    const QList<TextEditor::ToolBarField *> fields = config->fields();
    for (TextEditor::ToolBarField * const field : fields) {
        auto edit = new FancyLineEdit;
        edit->setFiltering(true);
        edit->setPlaceholderText(field->placeholderText());
        edit->setToolTip(field->toolTip());
        edit->setText(field->text());
        connect(edit, &QLineEdit::textChanged, field, &TextEditor::ToolBarField::setText);
        connect(edit, &QLineEdit::returnPressed, field, &TextEditor::ToolBarField::commit);
        connect(edit, &FancyLineEdit::rightButtonClicked, field, &TextEditor::ToolBarField::commit);
        connect(field, &TextEditor::ToolBarField::textChanged, edit, [edit, field] {
            if (edit->text() != field->text())
                edit->setText(field->text());
        });
        QAction * const inBar = insertExtraToolBarWidget(Left, edit);
        inBar->setVisible(field->isVisible());
        connect(field, &TextEditor::ToolBarField::visibleChanged, inBar, [inBar, field] {
            inBar->setVisible(field->isVisible());
        });
    }
}

VcsBaseEditorConfig *VcsBaseEditorWidget::editorConfig() const
{
    const VcsEditorDocument * const document = vcsDocument();
    return document ? document->editorConfig() : nullptr;
}

void VcsBaseEditorWidget::executeTask(const ExecutableItem &task,
                                      const Storage<CommandResult> &resultStorage)
{
    VcsEditorDocument * const document = vcsDocument();
    QTC_ASSERT(document, return);
    document->executeTask(task, resultStorage);
}

void VcsBaseEditorWidget::setDefaultLineNumber(int line)
{
    VcsEditorDocument * const document = vcsDocument();
    QTC_ASSERT(document, return);
    document->setDefaultLineNumber(line);
}

void VcsBaseEditorWidget::gotoDefaultLine()
{
    if (VcsEditorDocument * const document = vcsDocument())
        document->gotoDefaultLine();
}

void VcsBaseEditorWidget::setPlainText(const QString &text)
{
    textDocument()->setPlainText(text);
}

// Find the complete file from a diff relative specification.
QString VcsBaseEditorWidget::findDiffFile(const QString &f) const
{
    const VcsEditorDocument * const document = vcsDocument();
    return document ? document->resolveDiffFile(f) : QString();
}

void VcsBaseEditorWidget::addDiffActions(QMenu *, const DiffChunk &)
{
}

void VcsBaseEditorWidget::slotAnnotateRevision(const QString &change)
{
    const int currentLine = textCursor().blockNumber() + 1;
    const FilePath fileName = fileNameForLine(currentLine).canonicalPath();
    const FilePath ownWorkingDirectory = this->workingDirectory();
    const FilePath workingDirectory = ownWorkingDirectory.isEmpty()
            ? VcsManager::findTopLevelForDirectory(fileName.parentDir())
            : ownWorkingDirectory;
    const FilePath relativePath = fileName.isRelativePath()
            ? fileName
            : fileName.relativeChildPath(workingDirectory);
    emit annotateRevisionRequested(workingDirectory, relativePath.toUrlishString(), change, currentLine);
}

QStringList VcsBaseEditorWidget::annotationPreviousVersions(const QString &) const
{
    return {};
}

void VcsBaseEditorWidget::slotPaste()
{
    // Retrieve service by soft dependency.
    auto pasteService = ExtensionSystem::PluginManager::getObject<CodePaster::Service>();
    QTC_ASSERT(pasteService, return);
    pasteService->postCurrentEditor();
}

bool VcsBaseEditorWidget::canApplyDiffChunk(const DiffChunk &dc) const
{
    const VcsEditorDocument * const document = vcsDocument();
    return document && document->canApplyDiffChunk(dc);
}

bool VcsBaseEditorWidget::applyDiffChunk(const DiffChunk &dc, PatchAction patchAction) const
{
    const VcsEditorDocument * const document = vcsDocument();
    return document && document->applyDiffChunk(dc, patchAction);
}

QString VcsBaseEditorWidget::fileNameFromDiffSpecification(const QTextBlock &inBlock, QString *header) const
{
    const VcsEditorDocument * const document = vcsDocument();
    return document ? document->fileNameFromDiffSpecification(inBlock, header) : QString();
}

void VcsBaseEditorWidget::addChangeActions(QMenu *, const QString &, int line)
{
    Q_UNUSED(line);
}


QString VcsBaseEditorWidget::decorateVersion(const QString &revision) const
{
    return revision;
}

bool VcsBaseEditorWidget::isValidRevision(const QString &revision) const
{
    Q_UNUSED(revision)
    return true;
}

QString VcsBaseEditorWidget::revisionSubject(const QTextBlock &inBlock) const
{
    Q_UNUSED(inBlock)
    return {};
}

bool VcsBaseEditorWidget::hasDiff() const
{
    switch (contentType()) {
    case DiffOutput:
    case LogOutput:
        return true;
    default:
        return false;
    }
}

void VcsBaseEditorWidget::slotApplyDiffChunk(const DiffChunk &chunk, PatchAction patchAction)
{
    // The document asks, saves, applies and says so; diffChunkReverted() is
    // forwarded from it.
    if (VcsEditorDocument * const document = vcsDocument())
        document->applyChunk(chunk, patchAction);
}

// Tagging of editors for re-use.
QString VcsBaseEditor::editorTag(EditorContentType t, const FilePath &workingDirectory,
                                 const QStringList &files, const QString &revision)
{
    const QChar colon = ':';
    QString rc = QString::number(t);
    rc += colon;
    if (!revision.isEmpty()) {
        rc += revision;
        rc += colon;
    }
    rc += workingDirectory.toUrlishString();
    if (!files.isEmpty()) {
        rc += colon;
        rc += files.join(colon);
    }
    return rc;
}

static const char tagPropertyC[] = "_q_VcsBaseEditorTag";

void VcsBaseEditor::tagEditor(IEditor *e, const QString &tag)
{
    e->document()->setProperty(tagPropertyC, QVariant(tag));
}

IEditor *VcsBaseEditor::locateEditorByTag(const QString &tag)
{
    const QList<IDocument *> documents = DocumentModel::openedDocuments();
    for (IDocument *document : documents) {
        const QVariant tagPropertyValue = document->property(tagPropertyC);
        if (tagPropertyValue.typeId() == QMetaType::QString && tagPropertyValue.toString() == tag)
            return DocumentModel::editorsForDocument(document).constFirst();
    }
    return nullptr;
}

/*!
    \class VcsBase::VcsEditorFactory

    \brief The VcsEditorFactory class is the base class for editor
    factories creating instances of VcsBaseEditor subclasses.

    \sa VcsBase::VcsBaseEditorWidget
*/

VcsEditorFactory::VcsEditorFactory(const VcsBaseEditorParameters &parameters)
{
    setId(parameters.id);
    setDisplayName(parameters.displayName);
    if (parameters.mimeType != DiffEditor::Constants::DIFF_EDITOR_MIMETYPE)
        addMimeType(parameters.mimeType);

    setOptionalActionMask(OptionalActions::None);
    setDuplicatedSupported(false);

    setDocumentCreator([parameters] { return new VcsEditorDocument(parameters); });

    // A VCS that still has a widget subclass gets it; one that has moved
    // everything of it into the parameters gets the Qt Quick editor.
    if (parameters.editorWidgetCreator) {
        setEditorWidgetCreator([parameters] {
            auto widget = parameters.editorWidgetCreator();
            auto editorWidget = Aggregation::query<VcsBaseEditorWidget>(widget);
            editorWidget->setParameters(parameters);
            return widget;
        });
        setEditorCreator([] { return new VcsBaseEditor(); });
    } else {
        setUsesQuickEditor(true);
    }
    setMarksVisible(false);
    setRevisionsVisible(false);
    // Output, not a file: not to be typed into, and folded where it has
    // chunks - which is what the widget subclass asked for in init().
    setReadOnly(true);
    if (parameters.type == LogOutput || parameters.type == DiffOutput)
        setCodeFoldingSupported(true);
}

VcsEditorFactory::~VcsEditorFactory() = default;

} // namespace VcsBase

#ifdef WITH_TESTS
#include <QTest>

namespace VcsBase {

// On the document, whichever editor the factory builds around it: the widget
// one for a VCS that still has a subclass, the Qt Quick one for a VCS that
// has moved everything into its parameters.
void VcsBaseEditorWidget::testDiffFileResolving(const VcsEditorFactory &factory)
{
    const std::unique_ptr<IEditor> editor(factory.createEditor());
    QVERIFY(editor);
    auto * const document = qobject_cast<VcsEditorDocument *>(editor->document());
    QVERIFY(document);

    QFETCH(QByteArray, header);
    QFETCH(QByteArray, fileName);
    QTextDocument doc(QString::fromLatin1(header));
    QTextBlock block = doc.lastBlock();
    // set source root for shadow builds
    VcsBase::setSource(document, FilePath::fromString(QString::fromLatin1(SRC_DIR)));
    QVERIFY(document->fileNameFromDiffSpecification(block).endsWith(QString::fromLatin1(fileName)));
}

void VcsBaseEditorWidget::testLogResolving(const VcsEditorFactory &factory,
                                           const QByteArray &data,
                                           const QByteArray &entry1,
                                           const QByteArray &entry2)
{
    const std::unique_ptr<IEditor> editor(factory.createEditor());
    QVERIFY(editor);
    auto * const document = qobject_cast<VcsEditorDocument *>(editor->document());
    QVERIFY(document);

    document->setPlainText(QLatin1String(data));
    // The document finds the entries.
    QCOMPARE(document->sections()->entries().value(0), QString::fromLatin1(entry1));
    QCOMPARE(document->sections()->entries().value(1), QString::fromLatin1(entry2));
    // And the widget editor's browser in the tool bar shows what it found;
    // the Qt Quick editor's is TextEditor's to test.
    if (VcsBaseEditorWidget * const widget = VcsBaseEditor::getVcsBaseEditor(editor.get())) {
        QCOMPARE(widget->d->entriesComboBox()->itemText(0), QString::fromLatin1(entry1));
        QCOMPARE(widget->d->entriesComboBox()->itemText(1), QString::fromLatin1(entry2));
    }
}

} // VcsBase

#endif

#include "vcsbaseeditor.moc"
