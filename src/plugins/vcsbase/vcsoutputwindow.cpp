// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "vcsoutputwindow.h"

#include "vcsbasetr.h"
#include "vcsoutputformatter.h"

#include <coreplugin/editormanager/editormanager.h>
#include <coreplugin/outputpaneview.h>
#include <coreplugin/outputview.h>

#include <texteditor/behaviorsettings.h>
#include <texteditor/fontsettings.h>

#include <utils/filepath.h>
#include <utils/qtcprocess.h>
#include <utils/theme/theme.h>

#include <QAction>
#include <QContextMenuEvent>
#ifdef WITH_TESTS
#include <QTest>
#endif

#include <QMenu>
#include <QPoint>
#include <QPointer>
#include <QRegularExpression>
#include <QTextBlock>
#include <QTextBlockUserData>
#include <QTextCharFormat>
#include <QTextStream>
#include <QTime>

using namespace Core;
using namespace Utils;

/*!
    \class VcsBase::VcsBaseOutputWindow

    \brief The VcsBaseOutputWindow class is an output window for Version Control
    System commands and other output (Singleton).

    Installed by the base plugin and accessible for the other plugins
    via static instance()-accessor. Provides slots to append output with
    special formatting.

    It is possible to associate a repository with plain log text, enabling
    an "Open" context menu action over relative file name tokens in the text
    (absolute paths will also work). This can be used for "status" logs,
    showing modified file names, allowing the user to open them.
*/

namespace VcsBase {
namespace Internal {

const char C_VCS_OUTPUT_PANE[] = "Vcs.OutputPane";

const char zoomSettingsKey[] = "Vcs/OutputPane/Zoom";

// Store repository along with text blocks
class RepositoryUserData : public QTextBlockUserData
{
public:
    explicit RepositoryUserData(const FilePath &repository) : m_repository(repository) {}
    const FilePath &repository() const { return m_repository; }

private:
    const FilePath m_repository;
};

// A plain text edit with a special context menu containing "Clear"
// and functions to append specially formatted entries.
class VcsOutputView : public Core::OutputPaneView
{
public:
    explicit VcsOutputView(QWidget *parent = nullptr);

    void appendLines(const QString &text, VcsOutputWindow::MessageStyle style,
                     const FilePath &repository);

private:
    QString anchorAt(int position);
    void adaptContextMenu(int position);
    void handleLink(const QString &href, int position);


    VcsOutputLineParser *m_parser = nullptr;
    QAction m_openAction;
    QList<QAction *> m_linkActions;
};

VcsOutputView::VcsOutputView(QWidget *parent)
    : Core::OutputPaneView(Context(C_VCS_OUTPUT_PANE), zoomSettingsKey, parent)
    , m_parser(new VcsOutputLineParser)
{
    formatter()->setBoldFontEnabled(false);
    formatter()->setLineParsers({m_parser});

    connect(this, &Core::OutputPaneView::contextMenuAboutToShow, this, [this](qreal x, qreal y) {
        adaptContextMenu(documentPositionAt(x, y));
    });
    connect(this, &Core::OutputPaneView::linkActivated, this,
            [this](const QString &href, qreal x, qreal y) {
                handleLink(href, documentPositionAt(x, y));
            });
}

// Search back for beginning of word
static inline int firstWordCharacter(const QString &s, int startPos)
{
    for ( ; startPos >= 0 ; startPos--) {
        if (s.at(startPos).isSpace())
            return startPos + 1;
    }
    return 0;
}

QString identifierAt(const QTextDocument *document, int position, FilePath *repository)
{
    if (repository)
        repository->clear();
    if (!document || position < 0)
        return {};

    const QTextBlock block = document->findBlock(position);
    if (!block.isValid())
        return {};

    // The blank-delimited word, not what QTextCursor::WordUnderCursor would
    // give: that breaks at delimiters like '/', and these are file names.
    const QString line = block.text();
    const int positionInLine = position - block.position();
    if (positionInLine < 0 || positionInLine >= line.size() || line.at(positionInLine).isSpace())
        return {};

    // Which repository the line came from, remembered on the block when it was
    // written. It is what a relative file name is resolved against.
    if (repository) {
        if (QTextBlockUserData * const data = block.userData())
            *repository = static_cast<const RepositoryUserData *>(data)->repository();
    }

    const int startPos = firstWordCharacter(line, positionInLine);
    int endPos = positionInLine;
    for ( ; endPos < line.size() && !line.at(endPos).isSpace(); endPos++) ;
    return endPos > startPos ? line.mid(startPos, endPos - startPos) : QString();
}

QString VcsOutputView::anchorAt(int position)
{
    if (position < 0)
        return {};
    QTextCursor cursor(sourceDocument());
    // One past the position: a character's format is the format of the text
    // that follows it, so asking exactly at a link's first character finds
    // whatever came before it instead.
    cursor.setPosition(std::min(position + 1, sourceDocument()->characterCount() - 1));
    return cursor.charFormat().anchorHref();
}

void VcsOutputView::adaptContextMenu(int position)
{
    qDeleteAll(m_linkActions);
    m_linkActions.clear();

    const QString href = anchorAt(position);
    FilePath repo;
    const QString token = identifierAt(sourceDocument(), position, &repo);

    QList<QAction *> actions;
    if (!repo.isEmpty() && !href.isEmpty()) {
        // The parser knows what a link of its own means; it fills a menu, so
        // it is given one and the entries are taken out of it.
        const std::unique_ptr<QMenu> scratch(new QMenu);
        m_parser->fillLinkContextMenu(scratch.get(), repo, href);
        for (QAction * const action : scratch->actions()) {
            action->setParent(this);
            m_linkActions << action;
        }
        actions += m_linkActions;
    }

    if (!token.isEmpty()) {
        // A file, expanded through the repository if the name is relative.
        if (!repo.isEmpty() && !repo.isFile() && repo.isRelativePath())
            repo = repo.pathAppended(token);
        if (repo.isFile()) {
            m_openAction.setText(Tr::tr("Open \"%1\"").arg(repo.nativePath()));
            m_openAction.disconnect();
            connect(&m_openAction, &QAction::triggered, this, [fp = repo.absoluteFilePath()] {
                EditorManager::openEditor(fp);
            });
            actions << &m_openAction;
        }
    }

    // Over one of its own links these are the whole menu: the standard entries
    // are about the output as text, and this is about what the link points to.
    setContextMenuActions(actions, !href.isEmpty());
}

void VcsOutputView::handleLink(const QString &href, int position)
{
    if (href.isEmpty())
        return;

    FilePath repository;
    identifierAt(sourceDocument(), position, &repository);
    if (repository.isEmpty()) {
        formatter()->handleLink(href);
        return;
    }
    if (formatter()->handleFileLink(href))
        return;
    m_parser->handleVcsLink(repository, href);
}

static OutputFormat styleToFormat(VcsOutputWindow::MessageStyle style)
{
    switch (style) {
    case VcsOutputWindow::Warning:
        return LogMessageFormat;
    case VcsOutputWindow::Error:
        return StdErrFormat;
    case VcsOutputWindow::Message:
        return StdOutFormat;
    case VcsOutputWindow::Command:
        return NormalMessageFormat;
    case VcsOutputWindow::None:
        return OutputFormat::StdOutFormat;
    }
    return OutputFormat::StdOutFormat;
}

void VcsOutputView::appendLines(const QString &text, VcsOutputWindow::MessageStyle style,
                                const FilePath &repository)
{
    if (text.isEmpty())
        return;

    const QString textToAdd = style == VcsOutputWindow::Command
                            ? QTime::currentTime().toString("\nHH:mm:ss ") + text : text;
    const int previousLineCount = sourceDocument()->lineCount();

    formatter()->setBoldFontEnabled(style == VcsOutputWindow::Command);
    appendMessage(textToAdd, styleToFormat(style));

    // Written now rather than when the queue gets to it, because what follows
    // marks up the lines this call added and has to be able to find them.
    flush();
    scrollToBottom();

    if (!repository.isEmpty()) {
        // Which repository these lines came from, remembered on the lines
        // themselves: it is what a click on one of them is resolved against.
        QTextBlock block = sourceDocument()->findBlockByLineNumber(previousLineCount);
        for ( ; block.isValid(); block = block.next())
            block.setUserData(new RepositoryUserData(repository));
    }
}

#ifdef WITH_TESTS

class VcsOutputViewTest final : public QObject
{
    Q_OBJECT

private slots:
    void testTheWordAndRepositoryUnderAPoint()
    {
        // What a right-click or a link click in the VCS output resolves to.
        // Inside the widget this took a point in a QPlainTextEdit, so the only
        // way to ask it anything was to click in a running Qt Creator.
        QTextDocument document;
        document.setPlainText("modified: src/main.cpp\nnothing here\n");

        const FilePath repository = FilePath::fromString("/tmp/some/repo");
        document.findBlockByNumber(0).setUserData(new RepositoryUserData(repository));

        FilePath found;
        // Anywhere in the word gives the whole word, delimiters and all: these
        // are file names, and stopping at '/' would give half a path.
        for (int i = 10; i < 22; ++i) {
            QCOMPARE(identifierAt(&document, i, &found), QString("src/main.cpp"));
            QCOMPARE(found, repository);
        }

        // The first word of the line, and its start.
        QCOMPARE(identifierAt(&document, 0, nullptr), QString("modified:"));
        QCOMPARE(identifierAt(&document, 8, nullptr), QString("modified:"));

        // On a space there is no word, so a click between two of them offers
        // nothing rather than the one on either side.
        QVERIFY(identifierAt(&document, 9, nullptr).isEmpty());

        // A line nobody attached a repository to has none, and says so rather
        // than inheriting the previous line's.
        QVERIFY(!identifierAt(&document, 25, &found).isEmpty());
        QVERIFY2(found.isEmpty(), "a line with no repository was given another line's");

        // And nothing at all outside the document.
        QVERIFY(identifierAt(&document, -1, nullptr).isEmpty());
        QVERIFY(identifierAt(nullptr, 0, nullptr).isEmpty());
    }
};

QObject *createVcsOutputViewTest()
{
    return new VcsOutputViewTest;
}

#endif // WITH_TESTS

} // namespace Internal

// ------------------- VcsBaseOutputWindowPrivate
class VcsOutputWindowPrivate
{
public:
    Internal::VcsOutputView widget;
    const QRegularExpression passwordRegExp = QRegularExpression("://([^@:]+):([^@]+)@");
};

static VcsOutputWindow *m_instance = nullptr;
static VcsOutputWindowPrivate *d = nullptr;

VcsOutputWindow::VcsOutputWindow()
{
    setId("VersionControl");
    setDisplayName(Tr::tr("Version Control"));
    setPriorityInStatusBar(-20);

    d = new VcsOutputWindowPrivate;
    Q_ASSERT(d->passwordRegExp.isValid());
    m_instance = this;

    auto updateBehaviorSettings = [] {
        d->widget.setWheelZoomEnabled(TextEditor::globalBehaviorSettings().scrollWheelZooming());
    };

    auto updateFontSettings = [] {
        d->widget.setBaseFont(TextEditor::globalFontSettings().data().font());
    };

    updateBehaviorSettings();
    updateFontSettings();
    setupContext(Internal::C_VCS_OUTPUT_PANE, &d->widget);

    connect(this, &IOutputPane::zoomInRequested, &d->widget, &Core::OutputPaneView::zoomIn);
    connect(this, &IOutputPane::zoomOutRequested, &d->widget, &Core::OutputPaneView::zoomOut);
    connect(this, &IOutputPane::resetZoomRequested, &d->widget, &Core::OutputPaneView::resetZoom);
    connect(&TextEditor::globalBehaviorSettings(), &Utils::AspectContainer::changed,
            this, updateBehaviorSettings);
    connect(&TextEditor::globalFontSettings(), &TextEditor::FontSettings::changed,
            this, updateFontSettings);
}

static QString filterPasswordFromUrls(QString input)
{
    return input.replace(d->passwordRegExp, "://\\1:***@");
}

VcsOutputWindow::~VcsOutputWindow()
{
    m_instance = nullptr;
    delete d;
}

QWidget *VcsOutputWindow::outputWidget(QWidget *parent)
{
    if (parent != d->widget.parent())
        d->widget.setParent(parent);
    return &d->widget;
}

QStringList VcsOutputWindow::outputTexts() const
{
    return {d->widget.toPlainText()};
}

void VcsOutputWindow::clearContents()
{
    d->widget.clear();
}

void VcsOutputWindow::setFocus()
{
    d->widget.setFocus();
}

bool VcsOutputWindow::hasFocus() const
{
    return d->widget.hasFocus();
}

bool VcsOutputWindow::canFocus() const
{
    return true;
}

bool VcsOutputWindow::canNavigate() const
{
    return false;
}

bool VcsOutputWindow::canNext() const
{
    return false;
}

bool VcsOutputWindow::canPrevious() const
{
    return false;
}

void VcsOutputWindow::goToNext()
{
}

void VcsOutputWindow::goToPrev()
{
}

void VcsOutputWindow::setText(const QString &text)
{
    d->widget.setPlainText(text);
}

void VcsOutputWindow::setData(const QByteArray &data)
{
    setText(TextEncoding::encodingForLocale().decode(data));
}

void VcsOutputWindow::append(const Utils::FilePath &workingDirectory, const QString &text,
                             MessageStyle style, bool silently)
{
    const QString lines = (text.endsWith('\n') || text.endsWith('\r')) ? text : text + '\n';
    d->widget.appendLines(lines, style, workingDirectory);

    if (!silently && !d->widget.isVisible())
        m_instance->popup(IOutputPane::NoModeSwitch);
}

void VcsOutputWindow::appendSilently(const FilePath &workingDirectory, const QString &text)
{
    append(workingDirectory, text, None, true);
}

void VcsOutputWindow::appendText(const Utils::FilePath &workingDirectory, const QString &text)
{
    append(workingDirectory, text, None, false);
}

void VcsOutputWindow::appendMessage(const FilePath &workingDirectory, const QString &text)
{
    append(workingDirectory, text, Message, true);
}

void VcsOutputWindow::appendWarning(const FilePath &workingDirectory, const QString &text)
{
    append(workingDirectory, text, Warning, false);
}

void VcsOutputWindow::appendError(const FilePath &workingDirectory, const QString &text)
{
    append(workingDirectory, text, Error, false);
}

// Helper to format arguments for log windows hiding common password options.
static inline QString formatArguments(const QStringList &args)
{
    const char passwordOptionC[] = "--password";
    QString rc;
    QTextStream str(&rc);
    const int size = args.size();
    // Skip authentication options
    for (int i = 0; i < size; i++) {
        const QString arg = filterPasswordFromUrls(args.at(i));
        if (i)
            str << ' ';
        if (arg.startsWith(QString::fromLatin1(passwordOptionC) + '=')) {
            str << ProcessArgs::quoteArg("--password=********");
            continue;
        }
        str << ProcessArgs::quoteArg(arg);
        if (arg == passwordOptionC) {
            str << ' ' << ProcessArgs::quoteArg("********");
            i++;
        }
    }
    return rc;
}

QString VcsOutputWindow::msgExecutionLogEntry(const FilePath &workingDir, const CommandLine &command)
{
    const QString maskedCmdline = ProcessArgs::quoteArg(command.executable().toUserOutput())
            + ' ' + formatArguments(command.splitArguments());
    if (workingDir.isEmpty())
        return Tr::tr("Running: %1").arg(maskedCmdline) + '\n';
    return Tr::tr("Running in \"%1\": %2").arg(workingDir.toUserOutput(), maskedCmdline) + '\n';
}

void VcsOutputWindow::appendShellCommandLine(const FilePath &workingDirectory, const QString &text)
{
    append(workingDirectory, filterPasswordFromUrls(text), Command, true);
}

void VcsOutputWindow::appendCommand(const FilePath &workingDirectory, const CommandLine &command)
{
    appendShellCommandLine(workingDirectory, msgExecutionLogEntry(workingDirectory, command));
}

void VcsOutputWindow::destroy()
{
    delete m_instance;
    m_instance = nullptr;
}

VcsOutputWindow *VcsOutputWindow::instance()
{
    if (!m_instance)
        (void) new VcsOutputWindow;
    return m_instance;
}

} // namespace VcsBase

#ifdef WITH_TESTS
#include "vcsoutputwindow.moc"
#endif
