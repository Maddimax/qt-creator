// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "fakevimactions.h"
#include "fakevimhandler.h"
#include "fakevimtr.h"
#include "mcpsupport.h"

#include <coreplugin/actionmanager/actioncontainer.h>
#include <coreplugin/actionmanager/actionmanager.h>
#include <coreplugin/actionmanager/command.h>
#include <coreplugin/actionmanager/command.h>
#include <coreplugin/coreconstants.h>
#include <coreplugin/dialogs/ioptionspage.h>
#include <coreplugin/editormanager/editormanager.h>
#include <coreplugin/session.h>
#include <coreplugin/editormanager/documentmodel.h>
#include <coreplugin/find/findplugin.h>
#include <coreplugin/find/textfindconstants.h>
#include <coreplugin/find/ifindsupport.h>
#include <coreplugin/documentmanager.h>
#include <coreplugin/modemanager.h>
#include <coreplugin/icore.h>
#include <coreplugin/idocument.h>
#include <utils/guitest.h>
#include <utils/mimeutils.h>
#include <coreplugin/messagemanager.h>
#include <coreplugin/statusbarmanager.h>

#include <extensionsystem/iplugin.h>

#include <projectexplorer/projectexplorerconstants.h>

#include <texteditor/autocompleter.h>
#include <texteditor/codeassist/assistinterface.h>
#include <texteditor/codeassist/assistproposalitem.h>
#include <texteditor/codeassist/asyncprocessor.h>
#include <texteditor/codeassist/completionassistprovider.h>
#include <texteditor/codeassist/genericproposal.h>
#include <texteditor/codeassist/genericproposalmodel.h>
#include <texteditor/codeassist/iassistprocessor.h>
#include <texteditor/displaysettings.h>
#include <texteditor/marginsettings.h>
#include <texteditor/fontsettings.h>
#include <texteditor/icodestylepreferences.h>
#include <texteditor/indenter.h>
#include <texteditor/fontsettings.h>
#include <texteditor/tabsettings.h>
#include <texteditor/textdocumentlayout.h>
#include <texteditor/texteditor.h>
#include <texteditor/textsuggestion.h>
#include <texteditor/textviewport.h>
#include <texteditor/texteditorconstants.h>

#include <texteditor/textmark.h>
#include <texteditor/typingsettings.h>

#include <utils/aggregate.h>
#include <utils/algorithm.h>
#include <utils/aspects.h>
#include <utils/shutdownguard.h>
#include <utils/treemodel.h>

#ifdef WITH_TESTS
#include <utils/temporarydirectory.h>

#include <memory>
#include <QTest>
#endif
#include <utils/environment.h>
#include <utils/fancylineedit.h>
#include <utils/guiutils.h>
#include <utils/hostosinfo.h>
#include <utils/infolabel.h>
#include <utils/layoutbuilder.h>
#include <utils/pathchooser.h>
#include <utils/qtcprocess.h>
#include <utils/qtcassert.h>

#include <QScopeGuard>
#include <utils/stylehelper.h>

#include <cppeditor/cppeditorconstants.h>

#include <extensionsystem/pluginmanager.h>

#include <QAction>
#include <QAbstractTableModel>
#include <QDebug>
#include <QFile>
#include <QGridLayout>
#include <QGroupBox>
#include <QGuiApplication>
#include <QInputDialog>
#include <QMessageBox>
#include <QItemDelegate>
#include <QLineEdit>
#include <QMainWindow>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollBar>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QStyleHints>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextEdit>
#include <QTimer>
#include <QTreeWidgetItem>

#include <functional>

#ifdef WITH_TESTS
#include "fakevim_test.h"
#endif

using namespace TextEditor;
using namespace Core;
using namespace Utils;

namespace FakeVim::Internal {

const char INSTALL_HANDLER[]                = "TextEditor.FakeVimHandler";
const char SETTINGS_CATEGORY[]              = "D.FakeVim";
const char SETTINGS_ID[]                    = "A.FakeVim.General";
const char SETTINGS_EX_CMDS_ID[]            = "B.FakeVim.ExCommands";
const char SETTINGS_USER_CMDS_ID[]          = "C.FakeVim.UserCommands";

static class FakeVimPlugin *dd = nullptr;

class MiniBuffer : public QStackedWidget
{
    Q_OBJECT

public:
    MiniBuffer()
        : m_label(new QLabel(this))
        , m_edit(new QLineEdit(this))
    {
        connect(m_edit, &QLineEdit::textEdited, this, &MiniBuffer::changed);
        connect(m_edit, &QLineEdit::cursorPositionChanged, this, &MiniBuffer::changed);
        connect(m_edit, &QLineEdit::selectionChanged, this, &MiniBuffer::changed);
        m_label->setTextInteractionFlags(Qt::TextSelectableByMouse);

        addWidget(m_label);
        addWidget(m_edit);

        m_hideTimer.setSingleShot(true);
        m_hideTimer.setInterval(8000);
        connect(&m_hideTimer, &QTimer::timeout, this, &QWidget::hide);
    }

    void setContents(const QString &contents, int cursorPos, int anchorPos,
                     int messageLevel, FakeVimHandler *eventFilter)
    {
        if (cursorPos != -1) {
            {
                QSignalBlocker blocker(m_edit);
                m_label->clear();
                m_edit->setText(contents);
                if (anchorPos != -1 && anchorPos != cursorPos)
                    m_edit->setSelection(anchorPos, cursorPos - anchorPos);
                else
                    m_edit->setCursorPosition(cursorPos);
            }
            setCurrentWidget(m_edit);
            m_edit->setFocus();
        } else {
            if (contents.isEmpty()) {
                if (m_alwaysVisible) {
                    m_hideTimer.stop();
                    m_label->clear();
                    m_label->setStyleSheet(QString());
                    show();
                } else if (m_lastMessageLevel == MessageMode) {
                    hide();
                } else {
                    m_hideTimer.start();
                }
            } else {
                m_hideTimer.stop();
                show();

                m_label->setText(contents);

                QString css;
                if (messageLevel == MessageError) {
                    css = "border:1px solid rgba(255,255,255,150);"
                          "background-color:rgba(255,0,0,100);";
                } else if (messageLevel == MessageWarning) {
                    css = "border:1px solid rgba(255,255,255,120);"
                            "background-color:rgba(255,255,0,20);";
                } else if (messageLevel == MessageShowCmd) {
                    css = "border:1px solid rgba(255,255,255,120);"
                            "background-color:rgba(100,255,100,30);";
                }
                m_label->setStyleSheet(QString::fromLatin1(
                    "*{border-radius:2px;padding-left:4px;padding-right:4px;%1}").arg(css));
            }

            if (m_edit->hasFocus())
                emit edited(QString(), -1, -1);

            setCurrentWidget(m_label);
        }

        if (m_eventFilter != eventFilter) {
            if (m_eventFilter != nullptr) {
                m_edit->removeEventFilter(m_eventFilter);
                disconnect(this, &MiniBuffer::edited, nullptr, nullptr);
            }
            if (eventFilter != nullptr) {
                m_edit->installEventFilter(eventFilter);
                connect(this, &MiniBuffer::edited,
                        eventFilter, &FakeVimHandler::miniBufferTextEdited);
            }
            m_eventFilter = eventFilter;
        }

        m_lastMessageLevel = messageLevel;
    }

    QSize sizeHint() const override
    {
        QSize size = QWidget::sizeHint();
        // reserve maximal width for line edit widget
        return currentWidget() == m_edit ? QSize(maximumWidth(), size.height()) : size;
    }

    // When set, the buffer stays shown (empty) instead of hiding while idle,
    // for the reserved in-editor command line (QTCREATORBUG-21005).
    void setAlwaysVisible(bool v) { m_alwaysVisible = v; }

signals:
    void edited(const QString &text, int cursorPos, int anchorPos);

private:
    void changed()
    {
        const int cursorPos = m_edit->cursorPosition();
        int anchorPos = m_edit->selectionStart();
        if (anchorPos == cursorPos)
            anchorPos = cursorPos + m_edit->selectedText().size();
        emit edited(m_edit->text(), cursorPos, anchorPos);
    }

    QLabel *m_label;
    QLineEdit *m_edit;
    QObject *m_eventFilter = nullptr;
    QTimer m_hideTimer;
    int m_lastMessageLevel = MessageMode;
    bool m_alwaysVisible = false;
};

class RelativeNumbersColumn : public QWidget
{
public:
    RelativeNumbersColumn(TextEditorWidget *baseTextEditor)
        : QWidget(baseTextEditor)
        , m_editor(baseTextEditor)
        , m_documentLayout(m_editor->document()->documentLayout())
    {
        setAttribute(Qt::WA_TransparentForMouseEvents, true);

        m_timerUpdate.setSingleShot(true);
        m_timerUpdate.setInterval(0);
        connect(&m_timerUpdate, &QTimer::timeout,
                this, &RelativeNumbersColumn::followEditorLayout);

        auto start = QOverload<>::of(&QTimer::start);
        connect(m_editor, &PlainTextEdit::cursorPositionChanged,
                &m_timerUpdate, start);
        connect(m_editor->verticalScrollBar(), &QAbstractSlider::valueChanged,
                &m_timerUpdate, start);
        connect(m_editor->document(), &QTextDocument::contentsChanged,
                &m_timerUpdate, start);
        connect(&TextEditor::displaySettings(), &DisplaySettings::changed, &m_timerUpdate, start);
        connect(&globalFontSettings(), &FontSettings::changed,
                this, &RelativeNumbersColumn::followEditorLayout);
        connect(m_documentLayout, &QAbstractTextDocumentLayout::documentSizeChanged,
                this, &RelativeNumbersColumn::followEditorLayout);
        connect(m_editor, &TextEditorWidget::resized,
                this, &RelativeNumbersColumn::followEditorLayout);

        m_editor->installEventFilter(this);

        followEditorLayout();
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        QTextCursor firstVisibleCursor = m_editor->cursorForPosition(QPoint(0, 0));
        QTextBlock firstVisibleBlock = firstVisibleCursor.block();
        if (firstVisibleCursor.positionInBlock() > 0) {
            firstVisibleBlock = firstVisibleBlock.next();
            firstVisibleCursor.setPosition(firstVisibleBlock.position());
        }

        // Find relative number for the first visible line.
        QTextBlock block = m_editor->textCursor().block();
        bool forward = firstVisibleBlock.blockNumber() > block.blockNumber();
        int n = 0;
        while (block.isValid() && block != firstVisibleBlock) {
            block = forward ? block.next() : block.previous();
            if (block.isVisible())
                n += forward ? 1 : -1;
        }

        // Copy colors from extra area palette.
        QPainter p(this);
        QPalette pal = m_editor->extraArea()->palette();
        const QColor fg = pal.color(QPalette::Dark);
        const QColor bg = pal.color(QPalette::Window);
        p.setPen(fg);

        // Draw relative line numbers. Position each number at the block's
        // actual location as laid out by the editor, so they stay aligned with
        // lines of differing height (e.g. CJK glyphs) or that wrap
        // (QTCREATORBUG-26802).
        const QRect eventRect = event->rect();
        const bool hideLineNumbers = m_editor->lineNumbersVisible();
        while (block.isValid()) {
            if (block.isVisible()) {
                QTextCursor blockCursor(m_editor->document());
                blockCursor.setPosition(block.position());
                const QRect cursorRect = m_editor->cursorRect(blockCursor);
                const QRect rect(0, cursorRect.y(), width(), cursorRect.height());
                if (n != 0 && rect.intersects(eventRect)) {
                    const int line = qAbs(n);
                    const QString number = QString::number(line);
                    if (hideLineNumbers)
                        p.fillRect(rect, bg);
                    if (hideLineNumbers || line < 100)
                        p.drawText(rect, Qt::AlignRight, number);
                }

                if (cursorRect.y() > height())
                    break;

                ++n;
            }

            block = block.next();
        }
    }

    bool eventFilter(QObject *, QEvent *event) override
    {
        if (event->type() == QEvent::Resize || event->type() == QEvent::Move)
            m_timerUpdate.start();
        return false;
    }

private:
    void followEditorLayout()
    {
        QFont font = m_editor->font();
        if (m_font != font ) {
            m_font = font;
            setFont(m_font);
        }

        // Follow geometry of normal line numbers if visible,
        // otherwise follow geometry of marks (breakpoints etc.).
        // See TextEditorWidget::extraAreaWidth() how to calculate space.
        QRect rect = m_editor->extraArea()->geometry().adjusted(0, 0, -4, 0);
        bool marksVisible = m_editor->marksVisible();
        bool lineNumbersVisible = m_editor->lineNumbersVisible();
        bool foldMarksVisible = m_editor->codeFoldingVisible();

        if (marksVisible && lineNumbersVisible) {
            const TextEditor::FontSettingsData &fs = m_editor->textDocument()->fontSettings();
            int lineSpacing = (fs.relativeLineSpacing() == 100)
                                ? m_editor->fontMetrics().lineSpacing()
                                : fs.lineSpacing();
            rect.setLeft(lineSpacing + 2);
        }

        if (foldMarksVisible && (marksVisible || lineNumbersVisible)) {
            int lineSpacing = m_editor->fontMetrics().lineSpacing();
            rect.setRight(rect.right() - lineSpacing + lineSpacing % 2 + 1);
        }

        setGeometry(rect);

        update();
    }

    TextEditorWidget *m_editor;
    QAbstractTextDocumentLayout *m_documentLayout;
    QFont m_font;
    QTimer m_timerUpdate;
};

///////////////////////////////////////////////////////////////////////
//
// FakeVimPlugin
//
///////////////////////////////////////////////////////////////////////

using ExCommandMap = QMap<QString, QString>;
using UserCommandMap = QMap<int, QString>;

class FakeVimPlugin final : public ExtensionSystem::IPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.qt-project.Qt.QtCreatorPlugin" FILE "FakeVim.json")

public:
    FakeVimPlugin();

    ~FakeVimPlugin() final { dd = nullptr; }

    void initialize() final;

    void extensionsInitialized() final
    {
        m_miniBuffer = new MiniBuffer;
        StatusBarManager::addStatusBarWidget(m_miniBuffer, StatusBarManager::LastLeftAligned);
    }

    ExtensionSystem::IPlugin::ShutdownFlag aboutToShutdown() final
    {
        StatusBarManager::destroyStatusBarWidget(m_miniBuffer);
        m_miniBuffer = nullptr;
        return SynchronousShutdown;
    }

    void editorOpened(Core::IEditor *);
    void enterBuffer(Core::IEditor *editor, FakeVimHandler *handler);
    void triggerAutocmdInAnyBuffer(const QString &event, const QString &target);
    bool enterBufferOnce(Core::IEditor *editor);
    void editorAboutToClose(Core::IEditor *);
    void currentEditorAboutToChange(Core::IEditor *);

    void allDocumentsRenamed(const FilePath &oldPath, const FilePath &newPath);
    void documentRenamed(Core::IDocument *document, const FilePath &oldPath, const FilePath &newPath);
    void renameFileNameInEditors(const FilePath &oldPath, const FilePath &newPath);

    void setUseFakeVim(bool on);
    void setUseFakeVimInternal(bool on);
    void quitFakeVim();
    void fold(FakeVimHandler *handler, int depth, bool fold);
    void maybeReadVimRc();
    void setShowRelativeLineNumbers(bool on);
    void setCursorBlinking(bool on);

    void resetCommandBuffer();
    void showCommandBuffer(FakeVimHandler *handler, const QString &contents,
                           int cursorPos, int anchorPos, int messageLevel);
    void handleExCommand(FakeVimHandler *handler, bool *handled, const ExCommand &cmd);

    void readSettings();

    void handleDelayedQuitAll(bool forced);
    void handleDelayedQuit(bool forced, Core::IEditor *editor);
    void handleBufferDelete(bool forced, Core::IEditor *editor);
    void userActionTriggered(int key);

    void updateAllHightLights();

    void switchToFile(int n);
    int currentFile() const;

    void createRelativeNumberWidget(IEditor *editor);

signals:
    void delayedQuitRequested(bool forced, Core::IEditor *editor);
    void delayedQuitAllRequested(bool forced);
    void delayedBufferDeleteRequested(bool forced, Core::IEditor *editor);

public:
    struct HandlerAndData
    {
#ifdef Q_OS_WIN
        // We need to declare a constructor here, otherwise the MSVC 17.5.x compiler fails to parse
        // the "{nullptr}" initializer in the definition below.
        // This seems to be a compiler bug,
        // see: https://developercommunity.visualstudio.com/t/10351118
        HandlerAndData()
            : handler(nullptr)
        {}
#endif
        FakeVimHandler *handler{nullptr};
        // Whichever view is showing the file hands one of these out; the
        // type is the same either way.
        std::shared_ptr<void> suggestionBlocker;
        bool entered = false; // whether the buffer has been given to FakeVim
    };

    QHash<IEditor *, HandlerAndData> m_editorToHandler;

    void setActionChecked(Id id, bool check);

    using DistFunction = int (*)(const QRect &, const QRect &);
    void moveSomewhere(FakeVimHandler *handler, DistFunction f, int count);

    void keepOnlyWindow(); // :only

    ExCommandMap m_exCommandMap;
    ExCommandMap m_defaultExCommandMap;

    // Vim-like tag stack for :tag/CTRL-] and :pop/CTRL-T (QTCREATORBUG-11754).
    // m_tagStack holds the "from" location of each tag jump and the symbol
    // that was followed, which ":tags" lists; m_tagIndex is the current level
    // in it (m_tagStack.size() means we are at the newest jump).
    void tagJump(FakeVimHandler *handler, const QString &tag);
    void tagStackMove(FakeVimHandler *handler, int distance);
    struct TagJump
    {
        Utils::Link from;
        QString tag;
    };
    QList<TagJump> m_tagStack;
    int m_tagIndex = 0;

    UserCommandMap m_userCommandMap;
    UserCommandMap m_defaultUserCommandMap;

    MiniBuffer *m_miniBuffer = nullptr;
    // Optional command line shown at the bottom of the editor instead of the
    // status bar (QTCREATORBUG-21005). Child of the editor, so QPointer.
    void updateEditorCommandLinePlacement();
    void attachEditorMiniBuffer(FakeVimHandler *handler, IEditor *editor);
    void positionEditorMiniBuffer();
    void releaseEditorMiniBuffer();
    QPointer<MiniBuffer> m_editorMiniBuffer;
    // Whichever view the command line is sitting over, and the two ways of
    // asking it for room: a widget has a text margin, a Qt Quick view a text
    // inset. Exactly one of the two is set.
    QPointer<QWidget> m_miniBufferHost;
    QPointer<TextEditorWidget> m_miniBufferEditor;
    QPointer<TextEditor::TextViewport> m_miniBufferView;

    QString m_lastHighlight;

    int m_savedCursorFlashTime = 0;

    // Vim alternate file ("#"): the editor left when the current one changed.
    QPointer<IEditor> m_alternateFileEditor;
    bool m_didVimEnter = false; // VimEnter is fired once per session
    // Whether the application had focus when it was last looked at, so that
    // only a real change of it is announced.
    bool m_appActive = true;
    // The splits whose size changed since WinResized was last announced.
    // Vim announces them together, once the display has been brought up to
    // date, so these are gathered until the event loop has drained.
    QSet<int> m_resizedViews;
    QTimer m_winResizedTimer;
};

///////////////////////////////////////////////////////////////////////
//
// FakeVimExCommandsPage
//
///////////////////////////////////////////////////////////////////////

enum { CommandRole = Qt::UserRole };

const char exCommandMapGroup[] = "FakeVimExCommand";
const char userCommandMapGroup[] = "FakeVimUserCommand";
const char reKey[] = "RegEx";
const char cmdKey[] = "Cmd";
const char idKey[] = "Command";

// The commands there are, in the sections their ids name, and the expression
// each is reached by. A QTreeWidget carried all of this on its items, so the
// page's state was the widget's.
class ExCommandItem final : public TreeItem
{
public:
    ExCommandItem() = default; // The root, which is never drawn.

    ExCommandItem(const QString &title) // A section.
        : m_title(title)
    {}

    ExCommandItem(const QString &id, const QString &shortId, const QString &description)
        : m_id(id)
        , m_title(shortId)
        , m_description(description)
    {}

    QVariant data(int column, int role) const override
    {
        if (role == Qt::DisplayRole) {
            switch (column) {
            case 0: return m_title;
            case 1: return m_description;
            case 2: return m_regex;
            }
            return {};
        }
        // A section, and anything the user has changed, is shown in bold - the
        // widget page called that "modified".
        if (role == Qt::FontRole && (m_id.isEmpty() || isModified())) {
            QFont font;
            font.setBold(true);
            return font;
        }
        return {};
    }

    bool isModified() const
    {
        return !m_id.isEmpty() && m_regex != dd->m_defaultExCommandMap.value(m_id);
    }

    QString id() const { return m_id; }
    QString regex() const { return m_regex; }
    void setRegex(const QString &regex) { m_regex = regex; }
    QString defaultRegex() const { return dd->m_defaultExCommandMap.value(m_id); }

private:
    QString m_id;
    QString m_title;
    QString m_description;
    QString m_regex;
};

class ExCommandsAspect final : public BaseAspect
{
    Q_OBJECT

public:
    explicit ExCommandsAspect(AspectContainer *container)
        : BaseAspect(container)
        , m_model(this)
    {
        m_model.setHeader({Tr::tr("Command"), Tr::tr("Description"),
                           Tr::tr("Ex Trigger Expression")});
        reload();
    }

    AspectPresentation presentation() const override
    {
        AspectPresentation p = BaseAspect::presentation();
        p.control = AspectControls::Tree;
        p.filterPlaceholderText = Tr::tr("Filter");
        return p;
    }

    QAbstractItemModel *tableModel() override { return &m_model; }

    void reload()
    {
        m_current = nullptr;
        m_model.clear();
        QHash<QString, ExCommandItem *> sections;
        for (Command *c : ActionManager::commands()) {
            if (c->action() && c->action()->isSeparator())
                continue;
            const QString name = c->id().toString();
            const int pos = name.indexOf('.');
            const QString section = name.left(pos);
            auto item = new ExCommandItem(name, name.mid(pos + 1), c->description());
            item->setRegex(dd->m_exCommandMap.value(name));
            ExCommandItem *&parent = sections[section];
            if (!parent) {
                parent = new ExCommandItem(section);
                m_model.rootItem()->appendChild(parent);
            }
            parent->appendChild(item);
        }
    }

    // Which command the expression below is about. The view says so; the page
    // reads it. Null when a section is picked, which has no expression.
    Q_INVOKABLE void setCurrentIndex(const QModelIndex &index)
    {
        ExCommandItem *item = m_model.itemForIndex(index);
        ExCommandItem *command = item && !item->id().isEmpty() ? item : nullptr;
        if (command == m_current)
            return;
        m_current = command;
        emit currentChanged();
    }

    ExCommandItem *current() const { return m_current; }

    void setCurrentRegex(const QString &regex)
    {
        if (!m_current)
            return;
        m_current->setRegex(regex);
        m_current->update();
    }

    void resetAll()
    {
        m_model.forAllItems([](ExCommandItem *item) {
            if (!item->id().isEmpty()) {
                item->setRegex(item->defaultRegex());
                item->update();
            }
        });
        emit currentChanged();
    }

    ExCommandMap mapping() const
    {
        ExCommandMap map;
        m_model.forAllItems([&map](ExCommandItem *item) {
            if (item->id().isEmpty())
                return;
            const QString regex = item->regex();
            const QString pattern = item->defaultRegex();
            if ((regex.isEmpty() && pattern.isEmpty()) || (!regex.isEmpty() && pattern == regex))
                return;
            const QRegularExpression expression(regex);
            if (expression.isValid())
                map[item->id()] = expression.pattern();
        });
        return map;
    }

signals:
    void currentChanged();

private:
    // One item type all the way down: the sections and the commands under them
    // differ by what they hold, not by what they are.
    TreeModel<ExCommandItem> m_model;
    ExCommandItem *m_current = nullptr;
};

// What the Ex Command Mapping page edits: which expression reaches which
// command. The mapping itself lives in the plugin, so these aspects have no
// settings keys of their own.
class ExCommandsPageAspects final : public AspectContainer
{
public:
    ExCommandsPageAspects()
    {
        setAutoApply(false);
        setQmlSource(QUrl("qrc:/qt/qml/QtCreator/FakeVim/FakeVimExCommandsPage.qml"));

        commands.setQmlName("Commands");

        regex.setQmlName("Regex");
        regex.setLabelText(Tr::tr("Regular expression:"));
        regex.setDisplayStyle(StringAspect::LineEditDisplay);
        regex.setValidationFunction([](const QString &text) -> Result<> {
            if (QRegularExpression(text).isValid())
                return ResultOk;
            return ResultError(
                Tr::tr("The pattern \"%1\" is no valid regular expression.").arg(text));
        });

        reset.setQmlName("Reset");
        reset.setActionText(Tr::tr("Reset"));
        reset.setToolTip(Tr::tr("Reset to default."));
        reset.setAction([this] {
            if (ExCommandItem *item = commands.current())
                regex.setValue(item->defaultRegex());
        });

        resetAll.setQmlName("ResetAll");
        resetAll.setActionText(Tr::tr("Reset All"));
        resetAll.setAction([this] {
            commands.resetAll();
            showCurrent();
        });

        // Behaviour, not layout.
        connect(&commands, &ExCommandsAspect::currentChanged, this, [this] { showCurrent(); });
        regex.addOnVolatileValueChanged(this, [this] { store(); });
        showCurrent();
        m_original = commands.mapping();
    }

    void apply() override
    {
        AspectContainer::apply();
        const ExCommandMap mapping = commands.mapping();
        ExCommandMap &global = dd->m_exCommandMap;
        if (mapping == global)
            return;

        const ExCommandMap &defaults = dd->m_defaultExCommandMap;
        QtcSettings *settings = ICore::settings();
        settings->beginWriteArray(exCommandMapGroup);
        int count = 0;
        for (auto it = mapping.constBegin(), end = mapping.constEnd(); it != end; ++it) {
            const QString id = it.key();
            const QString re = it.value();
            const bool changed = defaults.contains(id) ? defaults[id] != re : !re.isEmpty();
            if (changed) {
                settings->setArrayIndex(count);
                settings->setValue(idKey, id);
                settings->setValue(reKey, re);
                ++count;
            }
        }
        settings->endArray();
        global.clear();
        global.insert(defaults);
        global.insert(mapping);
        m_original = mapping;
    }

    void cancel() override
    {
        AspectContainer::cancel();
        commands.reload();
        showCurrent();
        m_original = commands.mapping();
    }

    bool isDirty() const override
    {
        return AspectContainer::isDirty() || commands.mapping() != m_original;
    }

    ExCommandsAspect commands{this};
    StringAspect regex{this};
    ActionAspect reset{this};
    ActionAspect resetAll{this};

private:
    // Filling the field in writes the command's own expression straight back
    // into it, so there is nothing to guard against here.
    void showCurrent()
    {
        ExCommandItem *item = commands.current();
        regex.setEnabled(item);
        reset.setEnabled(item);
        regex.setValue(item ? item->regex() : QString());
    }

    void store() { commands.setCurrentRegex(regex.volatileValue()); }

    ExCommandMap m_original;
};

class FakeVimExCommandsPage : public IOptionsPage
{
public:
    FakeVimExCommandsPage()
    {
        setId(SETTINGS_EX_CMDS_ID);
        setDisplayName(Tr::tr("Ex Command Mapping"));
        setCategory(SETTINGS_CATEGORY);
        setSettingsProvider([] {
            static GuardedObject<ExCommandsPageAspects> theAspects;
            return theAspects.get();
        });
    }
};

const FakeVimExCommandsPage exCommandPage;

///////////////////////////////////////////////////////////////////////
//
// FakeVimUserCommandsPage
//
///////////////////////////////////////////////////////////////////////

class FakeVimUserCommandsModel : public QAbstractTableModel
{
public:
    explicit FakeVimUserCommandsModel(QObject *parent = nullptr)
        : QAbstractTableModel(parent)
    {
        m_commandMap = dd->m_userCommandMap;
    }

    UserCommandMap commandMap() const { return m_commandMap; }
    int rowCount(const QModelIndex &parent) const final { return parent.isValid() ? 0 : 9; }
    int columnCount(const QModelIndex &parent) const final { return parent.isValid() ? 0 : 2; }
    QVariant data(const QModelIndex &index, int role) const final;
    bool setData(const QModelIndex &index, const QVariant &data, int role) final;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const final;
    Qt::ItemFlags flags(const QModelIndex &index) const final;
    QHash<int, QByteArray> roleNames() const final
    {
        return AspectTable::withRoleNames(QAbstractTableModel::roleNames());
    }

private:
    UserCommandMap m_commandMap;
};

QVariant FakeVimUserCommandsModel::headerData(int section,
    Qt::Orientation orient, int role) const
{
    if (orient == Qt::Horizontal && role == Qt::DisplayRole) {
        switch (section) {
            case 0: return Tr::tr("Action");
            case 1: return Tr::tr("Command");
        };
    }
    return QVariant();
}

Qt::ItemFlags FakeVimUserCommandsModel::flags(const QModelIndex &index) const
{
    if (index.column() == 1)
        return QAbstractTableModel::flags(index) | Qt::ItemIsEditable;
    return QAbstractTableModel::flags(index);
}

// What the User Command Mapping page edits: nine commands, numbered, each a
// line of Vim. The model is the page's, so it outlives the form.
class FakeVimUserCommandsAspect final : public AspectContainer
{
public:
    FakeVimUserCommandsAspect()
    {
        setAutoApply(false);
        setQmlSource(QUrl("qrc:/qt/qml/QtCreator/FakeVim/FakeVimUserCommandsPage.qml"));
        commands.setQmlName("Commands");
    }

    void apply() override
    {
        AspectContainer::apply();
        const UserCommandMap &current = commands.commandMap();
        UserCommandMap &userMap = dd->m_userCommandMap;
        if (current == userMap)
            return;

        QtcSettings *settings = ICore::settings();
        settings->beginWriteArray(userCommandMapGroup);
        int count = 0;
        for (auto it = current.constBegin(), end = current.constEnd(); it != end; ++it) {
            const int key = it.key();
            const QString cmd = it.value();
            const bool changed = dd->m_defaultUserCommandMap.contains(key)
                                     ? dd->m_defaultUserCommandMap[key] != cmd
                                     : !cmd.isEmpty();
            if (changed) {
                settings->setArrayIndex(count);
                settings->setValue(idKey, key);
                settings->setValue(cmdKey, cmd);
                ++count;
            }
        }
        settings->endArray();
        userMap.clear();
        userMap.insert(dd->m_defaultUserCommandMap);
        userMap.insert(current);
    }

    bool isDirty() const override
    {
        return AspectContainer::isDirty() || commands.commandMap() != dd->m_userCommandMap;
    }

    // The rows, handed to whichever renderer is drawing the page.
    class CommandsAspect final : public BaseAspect
    {
    public:
        using BaseAspect::BaseAspect;

        AspectPresentation presentation() const override
        {
            AspectPresentation p = BaseAspect::presentation();
            p.control = AspectControls::Table;
            // Nine of them, always: they are numbered slots, not a list.
            p.allowAdding = false;
            p.allowRemoving = false;
            return p;
        }

        QAbstractItemModel *tableModel() override { return &m_model; }
        UserCommandMap commandMap() const { return m_model.commandMap(); }

    private:
        // Parented: a model handed to QML with no parent belongs to the engine.
        FakeVimUserCommandsModel m_model{this};
    };

    CommandsAspect commands{this};
};

#ifdef WITH_TESTS

// The commands lived in a QTreeView with an item delegate, so what the page
// held could only be read back out of widgets.
class FakeVimUserCommandsTest : public QObject
{
    Q_OBJECT

private slots:
    void testTheCommandsAreNumberedSlots();
    void testEditingACommandIsSomethingToApply();

private:
    static FakeVimUserCommandsAspect *page()
    {
        Core::IOptionsPage *found = Utils::findOrDefault(
            Core::IOptionsPage::allOptionsPages(),
            [](Core::IOptionsPage *p) { return p->id() == SETTINGS_USER_CMDS_ID; });
        if (!found)
            return nullptr;
        const std::optional<AspectContainer *> aspects = found->aspects();
        return aspects ? static_cast<FakeVimUserCommandsAspect *>(*aspects) : nullptr;
    }
};

// The commands lived on QTreeWidgetItems and the expression in a line edit
// beside them, so the mapping could only be read back out of widgets.
class FakeVimExCommandsTest : public QObject
{
    Q_OBJECT

private slots:
    void cleanup()
    {
        if (ExCommandsPageAspects *p = page())
            static_cast<BaseAspect *>(p)->cancel();
    }

    void testTheCommandsAreGroupedByTheirSection();
    void testNoExpressionIsOfferedUntilACommandIsPicked();
    void testChangingAnExpressionIsSomethingToApply();
    void testAnExpressionThatIsNoRegularExpressionIsRefused();
    void testResetPutsTheDefaultBack();

private:
    static ExCommandsPageAspects *page()
    {
        Core::IOptionsPage *found = Utils::findOrDefault(
            Core::IOptionsPage::allOptionsPages(),
            [](Core::IOptionsPage *p) { return p->id() == SETTINGS_EX_CMDS_ID; });
        if (!found)
            return nullptr;
        const std::optional<AspectContainer *> aspects = found->aspects();
        return aspects ? static_cast<ExCommandsPageAspects *>(*aspects) : nullptr;
    }

    // The first command under the first section, which is what a user would
    // click on.
    static QModelIndex firstCommand(ExCommandsPageAspects *p)
    {
        QAbstractItemModel *model = p->commands.tableModel();
        for (int section = 0; section < model->rowCount({}); ++section) {
            const QModelIndex parent = model->index(section, 0);
            if (model->rowCount(parent) > 0)
                return model->index(0, 0, parent);
        }
        return {};
    }
};

void FakeVimExCommandsTest::testTheCommandsAreGroupedByTheirSection()
{
    ExCommandsPageAspects *p = page();
    QVERIFY(p);
    QAbstractItemModel *model = p->commands.tableModel();
    QVERIFY(model);
    QCOMPARE(model->columnCount({}), 3);
    QVERIFY(model->rowCount({}) > 0);

    // Sections hold commands; a section with nothing under it would be a
    // command drawn as a heading.
    int commands = 0;
    for (int section = 0; section < model->rowCount({}); ++section) {
        const QModelIndex parent = model->index(section, 0);
        QVERIFY2(model->rowCount(parent) > 0,
                 qPrintable(parent.data().toString() + " has nothing under it"));
        commands += model->rowCount(parent);
    }
    QVERIFY(commands > model->rowCount({}));
}

void FakeVimExCommandsTest::testNoExpressionIsOfferedUntilACommandIsPicked()
{
    ExCommandsPageAspects *p = page();
    QVERIFY(p);
    QAbstractItemModel *model = p->commands.tableModel();

    p->commands.setCurrentIndex({});
    QVERIFY(!p->regex.isEnabled());
    QVERIFY(!p->reset.isEnabled());

    // A section is not a command and has no expression either.
    p->commands.setCurrentIndex(model->index(0, 0));
    QVERIFY(!p->regex.isEnabled());

    const QModelIndex command = firstCommand(p);
    QVERIFY(command.isValid());
    p->commands.setCurrentIndex(command);
    QVERIFY(p->regex.isEnabled());
    QVERIFY(p->reset.isEnabled());
}

void FakeVimExCommandsTest::testChangingAnExpressionIsSomethingToApply()
{
    ExCommandsPageAspects *p = page();
    QVERIFY(p);
    const QModelIndex command = firstCommand(p);
    QVERIFY(command.isValid());
    p->commands.setCurrentIndex(command);

    QVERIFY(!static_cast<BaseAspect *>(p)->isDirty());
    p->regex.setVolatileValue(QString("^zz$"));

    // The expression is in the tree as well as in the field, and the mapping
    // is not an aspect's value, so nothing else notices it.
    const QModelIndex shown = p->commands.tableModel()->index(command.row(), 2, command.parent());
    QCOMPARE(shown.data().toString(), QString("^zz$"));
    QVERIFY(static_cast<BaseAspect *>(p)->isDirty());
}

void FakeVimExCommandsTest::testAnExpressionThatIsNoRegularExpressionIsRefused()
{
    ExCommandsPageAspects *p = page();
    QVERIFY(p);
    const BaseAspect &regex = p->regex;

    QCOMPARE(regex.validationMessage("^foo$"), QString());
    // The widget page put a red label under the field; the aspect says it.
    QVERIFY(!regex.validationMessage("([unclosed").isEmpty());
}

void FakeVimExCommandsTest::testResetPutsTheDefaultBack()
{
    ExCommandsPageAspects *p = page();
    QVERIFY(p);
    const QModelIndex command = firstCommand(p);
    QVERIFY(command.isValid());
    p->commands.setCurrentIndex(command);
    const QString original = p->regex.volatileValue();

    p->regex.setVolatileValue(QString("^zz$"));
    QVERIFY(static_cast<BaseAspect *>(p)->isDirty());

    p->reset.triggerAction();
    QCOMPARE(p->regex.volatileValue(), original);
    QVERIFY(!static_cast<BaseAspect *>(p)->isDirty());

    // And all of them at once.
    p->regex.setVolatileValue(QString("^zz$"));
    p->resetAll.triggerAction();
    QVERIFY(!static_cast<BaseAspect *>(p)->isDirty());
}

QObject *createFakeVimExCommandsTest()
{
    return new FakeVimExCommandsTest;
}

void FakeVimUserCommandsTest::testTheCommandsAreNumberedSlots()
{
    FakeVimUserCommandsAspect *p = page();
    QVERIFY(p);
    QAbstractItemModel *model = p->commands.tableModel();
    QVERIFY(model);

    // Nine of them, always: they are slots to fill in, not a list to add to.
    QCOMPARE(model->rowCount({}), 9);
    QCOMPARE(model->columnCount({}), 2);
    QVERIFY(!p->commands.presentation().allowAdding);
    QVERIFY(!p->commands.presentation().allowRemoving);

    // The action names them and cannot be changed; the command is the user's.
    QCOMPARE(model->index(0, 0).data().toString(), Tr::tr("User command #%1").arg(1));
    QVERIFY(!model->index(0, 0).data(AspectTable::EditableRole).toBool());
    QVERIFY(model->index(0, 1).data(AspectTable::EditableRole).toBool());
}

void FakeVimUserCommandsTest::testEditingACommandIsSomethingToApply()
{
    FakeVimUserCommandsAspect *p = page();
    QVERIFY(p);
    QAbstractItemModel *model = p->commands.tableModel();
    const QString before = model->index(2, 1).data().toString();

    QVERIFY(!static_cast<BaseAspect *>(p)->isDirty());
    QVERIFY(model->setData(model->index(2, 1), QString(":wq")));
    QCOMPARE(model->index(2, 1).data().toString(), QString(":wq"));
    // The commands are not an aspect's value, so nothing else notices them.
    QVERIFY(static_cast<BaseAspect *>(p)->isDirty());

    QVERIFY(model->setData(model->index(2, 1), before));
    QVERIFY(!static_cast<BaseAspect *>(p)->isDirty());
}

QObject *createFakeVimUserCommandsTest()
{
    return new FakeVimUserCommandsTest;
}

// A handler is made for every text editor whether or not FakeVim is switched
// on, so what it does while switched off has to be nothing.
class FakeVimSuggestionsTest : public QObject
{
    Q_OBJECT

private slots:
    // Inline suggestions are held back outside insert mode. That hold was
    // being taken as the editor opened, with FakeVim switched off and nothing
    // that would ever release it, so Copilot could not show one at all.
    void testSuggestionsAreNotHeldBackWhileFakeVimIsOff()
    {
        QVERIFY(!settings().useFakeVim());

        Utils::TemporaryDirectory dir("fakevim-suggestions");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("plain.txt");
        QVERIFY(file.writeFileContents("one\ntwo\n"));

        IEditor * const editor
            = EditorManager::openEditor(file, Core::Constants::K_DEFAULT_TEXT_EDITOR_ID);
        QVERIFY(editor);
        const QScopeGuard closeIt([editor] { EditorManager::closeEditors({editor}, false); });
        TextEditorWidget * const widget = TextEditorWidget::fromEditor(editor);
        QVERIFY(widget);
        QVERIFY2(dd->m_editorToHandler.contains(editor),
                 "no handler was made, so this proves nothing");

        QVERIFY2(!widget->suggestionsBlocked(),
                 "suggestions are held back in an editor FakeVim is not driving");
    }
};

QObject *createFakeVimSuggestionsTest()
{
    return new FakeVimSuggestionsTest;
}

#endif // WITH_TESTS

class FakeVimUserCommandsPage : public IOptionsPage
{
public:
    FakeVimUserCommandsPage()
    {
        setId(SETTINGS_USER_CMDS_ID);
        setDisplayName(Tr::tr("User Command Mapping"));
        setCategory(SETTINGS_CATEGORY);
        setSettingsProvider([] {
            static GuardedObject<FakeVimUserCommandsAspect> theAspects;
            return theAspects.get();
        });
    }
};

const FakeVimUserCommandsPage userCommandsPage;

///////////////////////////////////////////////////////////////////////
//
// WordCompletion
//
///////////////////////////////////////////////////////////////////////

class FakeVimCompletionAssistProvider : public CompletionAssistProvider
{
public:
    IAssistProcessor *createProcessor(const AssistInterface *) const override;

    void setActive(const QString &needle, bool forward, FakeVimHandler *handler)
    {
        Q_UNUSED(forward)
        m_handler = handler;
        if (!m_handler)
            return;

        auto editor = qobject_cast<TextEditorWidget *>(handler->widget());
        if (!editor)
            return;

        //qDebug() << "ACTIVATE: " << needle << forward;
        m_needle = needle;
        editor->invokeAssist(Completion, this);
    }

    void setInactive()
    {
        m_needle.clear();
        m_handler = nullptr;
    }

    const QString &needle() const
    {
        return m_needle;
    }

    void appendNeedle(const QChar &c)
    {
        m_needle.append(c);
    }

    FakeVimHandler *handler() const
    {
        return m_handler;
    }

private:
    FakeVimHandler *m_handler = nullptr;
    QString m_needle;
};

static FakeVimCompletionAssistProvider theFakeVimCompletionAssistProvider;

class FakeVimAssistProposalItem final : public AssistProposalItem
{
public:
    FakeVimAssistProposalItem(const FakeVimCompletionAssistProvider *provider)
        : m_provider(const_cast<FakeVimCompletionAssistProvider *>(provider))
    {}

    bool implicitlyApplies() const override
    {
        return false;
    }

    bool prematurelyApplies(const QChar &c) const override
    {
        m_provider->appendNeedle(c);
        return text() == m_provider->needle();
    }

    void applyContextualContent(TextEditor::AssistTarget &, int) const override
    {
        QTC_ASSERT(m_provider->handler(), return);
        m_provider->handler()->handleReplay(text().mid(m_provider->needle().size()));
        // Announced after the word is in, as Vim has it. Giving up on a
        // completion instead is not announced: Qt Creator closes the proposal
        // without telling the provider, so there is no such moment here.
        m_provider->handler()->triggerCompleteDone(text());
        const_cast<FakeVimCompletionAssistProvider *>(m_provider)->setInactive();
    }

private:
    FakeVimCompletionAssistProvider *m_provider;
};


class FakeVimAssistProposalModel : public GenericProposalModel
{
public:
    FakeVimAssistProposalModel(const QList<AssistProposalItemInterface *> &items)
    {
        loadContent(items);
    }

    bool supportsPrefixExpansion() const override
    {
        return false;
    }
};

class FakeVimCompletionAssistProcessor : public AsyncProcessor
{
public:
    FakeVimCompletionAssistProcessor(const IAssistProvider *provider)
        : m_provider(static_cast<const FakeVimCompletionAssistProvider *>(provider))
    {}

    IAssistProposal *performAsync() override
    {
        const QString &needle = m_provider->needle();

        const int basePosition = interface()->position() - needle.size();

        QTextCursor tc(interface()->textDocument());
        tc.setPosition(interface()->position());
        tc.movePosition(QTextCursor::Start, QTextCursor::MoveAnchor);

        QList<AssistProposalItemInterface *> items;
        QSet<QString> seen;
        QTextDocument::FindFlags flags = QTextDocument::FindCaseSensitively;
        while (1) {
            tc = tc.document()->find(needle, tc.position(), flags);
            if (tc.isNull())
                break;
            QTextCursor sel = tc;
            sel.select(QTextCursor::WordUnderCursor);
            QString found = sel.selectedText();
            // Only add "real" completions.
            if (found.startsWith(needle)
                    && sel.anchor() != basePosition
                    && Utils::insert(seen, found)) {
                auto item = new FakeVimAssistProposalItem(m_provider);
                item->setText(found);
                items.append(item);
            }
            tc.movePosition(QTextCursor::Right, QTextCursor::MoveAnchor);
        }
        //qDebug() << "COMPLETIONS" << completions->size();

        return new GenericProposal(basePosition,
                                   GenericProposalModelPtr(new FakeVimAssistProposalModel(items)));
    }

private:
    const FakeVimCompletionAssistProvider *m_provider;
};

IAssistProcessor *FakeVimCompletionAssistProvider::createProcessor(const AssistInterface *) const
{
    return new FakeVimCompletionAssistProcessor(this);
}

// FakeVimUserCommandsModel

QVariant FakeVimUserCommandsModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid())
        return QVariant();

    if (role == Qt::DisplayRole || role == Qt::EditRole) {
        switch (index.column()) {
        case 0: // Action
            return Tr::tr("User command #%1").arg(index.row() + 1);
        case 1: // Command
            return m_commandMap.value(index.row() + 1);
        }
    }

    // A Qt Quick view cannot read flags(), so the model answers both.
    if (role == AspectTable::EditableRole)
        return AspectTable::isWritable(flags(index));

    return QVariant();
}

bool FakeVimUserCommandsModel::setData(const QModelIndex &index, const QVariant &data, int role)
{
    if (role == Qt::DisplayRole || role == Qt::EditRole) {
        if (index.column() == 1) {
            m_commandMap[index.row() + 1] = data.toString();
            emit dataChanged(index, index);
            return true;
        }
    }
    return false;
}

#ifdef WITH_TESTS
static void setupTest(QString *title, FakeVimHandler **handler, QWidget **edit)
{
    *title = QString::fromLatin1("test.cpp");

    // These tests take iedit->widget() and drive a FakeVimHandler over it
    // directly - setupWidget() and handleCommand() below - so the fixture
    // needs an actual QPlainTextEdit to hand the handler. The widget editor
    // is asked for on those grounds.
    //
    // Not because FakeVim stops at the Qt Quick editor: editorOpened() casts
    // to TextViewport *first* and wraps it in a ViewportAdapter, and
    // FakeVimInQuickEditorTest drives Vim over that view in six tests. This
    // comment used to say the answer was no, which was true before the
    // adapter existed and has not been since.
    TextEditorFactory * const factory
        = TextEditorFactory::preferredFactoryFor(FilePath::fromString(*title));
    const bool wasQuick = factory && factory->usesQuickEditor();
    const QScopeGuard restoreFactory([factory, wasQuick] {
        if (factory)
            factory->setUsesQuickEditor(wasQuick);
    });
    if (factory)
        factory->setUsesQuickEditor(false);

    IEditor *iedit = EditorManager::openEditorWithContents(Id(), title);
    EditorManager::activateEditor(iedit);
    *edit = iedit->widget();
    *handler = dd->m_editorToHandler.value(iedit, {}).handler;
    // Rather than dereferencing null: without a handler there is nothing to
    // test, and a crash here takes every later test in the plugin with it.
    QTC_ASSERT(*handler, return);
    (*handler)->setupWidget();
    (*handler)->handleCommand("set startofline");

//    *handler = 0;
//    m_statusMessage.clear();
//    m_statusData.clear();
//    m_infoMessage.clear();
//    if (m_textedit) {
//        m_textedit->setPlainText(lines);
//        QTextCursor tc = m_textedit->textCursor();
//        tc.movePosition(QTextCursor::Start, QTextCursor::MoveAnchor);
//        m_textedit->setTextCursor(tc);
//        m_textedit->setPlainText(lines);
//        *handler = new FakeVimHandler(m_textedit);
//    } else {
//        m_plaintextedit->setPlainText(lines);
//        QTextCursor tc = m_plaintextedit->textCursor();
//        tc.movePosition(QTextCursor::Start, QTextCursor::MoveAnchor);
//        m_plaintextedit->setTextCursor(tc);
//        m_plaintextedit->setPlainText(lines);
//        *handler = new FakeVimHandler(m_plaintextedit);
//    }

//    connect(*handler, &FakeVimHandler::commandBufferChanged,
//            this, &FakeVimPlugin::changeStatusMessage);
//    connect(*handler, &FakeVimHandler::extraInformationChanged,
//            this, &FakeVimPlugin::changeExtraInformation);
//    connect(*handler, &FakeVimHandler::statusDataChanged,
//            this, &FakeVimPlugin::changeStatusData);

//    QCOMPARE(EDITOR(toPlainText()), lines);
    (*handler)->handleCommand("set iskeyword=@,48-57,_,192-255,a-z,A-Z");
}
#endif

#ifdef WITH_TESTS
QObject *createFakeVimInQuickEditorTest();
#endif

FakeVimPlugin::FakeVimPlugin()
{
    dd = this;

#ifdef WITH_TESTS
    // Defined further down, where the plugin's own types are in scope.
    addTestCreator(createFakeVimInQuickEditorTest);
    addTestCreator([] { return createFakeVimTester(&setupTest); });
    addTestCreator(createFakeVimUserCommandsTest);
    addTestCreator(createFakeVimExCommandsTest);
    addTestCreator(createFakeVimSuggestionsTest);
#endif

    m_defaultExCommandMap[CppEditor::Constants::SWITCH_HEADER_SOURCE] = "^A$";
    m_defaultExCommandMap["Coreplugin.OutputPane.previtem"] = "^(cN(ext)?|cp(revious)?)!?( (.*))?$";
    m_defaultExCommandMap["Coreplugin.OutputPane.nextitem"] = "^cn(ext)?!?( (.*))?$";
    // :tag/CTRL-] and :pop/CTRL-T are handled directly via a Vim-like tag
    // stack (see handleExCommand), not through action mappings.
    m_defaultExCommandMap["QtCreator.Locate"] = "^e$";

    for (int i = 1; i < 10; ++i) {
        QString cmd = QString::fromLatin1(":echo User command %1 executed.<CR>");
        m_defaultUserCommandMap.insert(i, cmd.arg(i));
    }
}

void FakeVimPlugin::initialize()
{
    IOptionsPage::registerCategory(
        "D.FakeVim", Tr::tr("FakeVim"), ":/fakevim/images/settingscategory_fakevim.png");

/*
    // Set completion settings and keep them up to date.
    TextEditorSettings *textEditorSettings = TextEditorSettings::instance();
    completion->setCompletionSettings(textEditorSettings->completionSettings());
    connect(textEditorSettings, &TextEditorSettings::completionSettingsChanged,
            completion, &TextEditorWidget::setCompletionSettings);
*/
    readSettings();

    registerMcpTools();

    // Vimrc can break test so don't source it if running tests.
    if (!ExtensionSystem::PluginManager::testRunRequested())
        maybeReadVimRc();

    Command *cmd = nullptr;

    cmd = ActionManager::registerAction(settings().useFakeVim.action(),
        INSTALL_HANDLER, Context(Core::Constants::C_GLOBAL), true);
    cmd->setDefaultKeySequence(QKeySequence(useMacShortcuts ? Tr::tr("Meta+Shift+Y,Meta+Shift+Y")
                                                            : Tr::tr("Alt+Y,Alt+Y")));
    connect(cmd->action(), &QAction::triggered, [] { settings().writeSettings(); });

    ActionContainer *advancedMenu =
        ActionManager::actionContainer(Core::Constants::M_EDIT_ADVANCED);
    advancedMenu->addAction(cmd, Core::Constants::G_EDIT_EDITOR);

    const Id base = "FakeVim.UserAction";
    for (int i = 1; i < 10; ++i) {
        auto act = new QAction(this);
        act->setText(Tr::tr("Execute User Action #%1").arg(i));
        cmd = ActionManager::registerAction(act, base.withSuffix(i));
        cmd->setDefaultKeySequence(QKeySequence((useMacShortcuts ? Tr::tr("Meta+Shift+Y,%1")
                                                                 : Tr::tr("Alt+Y,%1")).arg(i)));
        connect(act, &QAction::triggered, this, [this, i] { userActionTriggered(i); });
    }

    connect(ICore::instance(), &ICore::coreAboutToClose, this, [] {
        // Don't attach to editors anymore.
        disconnect(EditorManager::instance(), &EditorManager::editorOpened,
                   dd, &FakeVimPlugin::editorOpened);
    });

    // EditorManager
    connect(EditorManager::instance(), &EditorManager::editorAboutToClose,
            this, &FakeVimPlugin::editorAboutToClose);
    connect(EditorManager::instance(), &EditorManager::editorOpened,
            this, &FakeVimPlugin::editorOpened);
    // A split is Vim's window. WinNew carries nothing; WinClosed names the
    // window that went, which Vim reports as its id (measured), so the id
    // Qt Creator hands out is passed straight through as the target.
    connect(EditorManager::instance(), &EditorManager::editorViewCreated,
            this, [this](int) { triggerAutocmdInAnyBuffer("WinNew", {}); });
    connect(EditorManager::instance(), &EditorManager::editorViewClosed,
            this, [this](int viewId) {
        triggerAutocmdInAnyBuffer("WinClosed", QString::number(viewId));
    });
    // A split changing size. Vim announces the windows that changed TOGETHER,
    // after the display has been brought up to date, so one layout change is
    // one event naming several - hence the gathering and the zero timer rather
    // than an announcement per view.
    m_winResizedTimer.setSingleShot(true);
    m_winResizedTimer.setInterval(0);
    connect(&m_winResizedTimer, &QTimer::timeout, this, [this] {
        if (m_resizedViews.isEmpty() || !settings().useFakeVim()) {
            m_resizedViews.clear();
            return;
        }
        QList<int> viewIds = Utils::toList(m_resizedViews);
        std::sort(viewIds.begin(), viewIds.end());
        m_resizedViews.clear();
        FakeVimHandler *handler = nullptr;
        if (IEditor *current = EditorManager::currentEditor())
            handler = m_editorToHandler.value(current, {}).handler;
        if (!handler && !m_editorToHandler.isEmpty())
            handler = m_editorToHandler.constBegin()->handler;
        if (handler)
            handler->triggerWinResized(viewIds);
    });
    connect(EditorManager::instance(), &EditorManager::editorViewResized,
            this, [this](int viewId) {
        m_resizedViews.insert(viewId);
        m_winResizedTimer.start();
    });
    // The whole window changing size, which is Vim resizing rather than one of
    // its windows. It carries no target and no v:event.
    connect(ICore::instance(), &ICore::mainWindowResized,
            this, [this] { triggerAutocmdInAnyBuffer("VimResized", {}); });
    // Vim announces the APPLICATION gaining and losing focus, which is what
    // an ":autocmd FocusLost * wa" hangs off. Moving between splits is not
    // that - it is WinEnter/WinLeave above - so this watches the application
    // state rather than the focus of any widget. Neither event carries a
    // target: both "<afile>" and "<amatch>" are the current file (measured).
    m_appActive = QGuiApplication::applicationState() == Qt::ApplicationActive;
    connect(qGuiApp, &QGuiApplication::applicationStateChanged,
            this, [this](Qt::ApplicationState state) {
        const bool active = state == Qt::ApplicationActive;
        if (active == m_appActive)
            return;
        m_appActive = active;
        triggerAutocmdInAnyBuffer(active ? QLatin1String("FocusGained")
                                        : QLatin1String("FocusLost"), {});
    });
    // A context menu about to be shown. The pattern is the mode, which the
    // handler works out for itself.
    connect(EditorManager::instance(), &EditorManager::aboutToShowContextMenu,
            this, [this](QMenu *, const Utils::FilePath &,
                         const QHash<Utils::Id, QAction *> &) {
        if (!settings().useFakeVim())
            return;
        if (IEditor *current = EditorManager::currentEditor()) {
            if (FakeVimHandler *handler = m_editorToHandler.value(current, {}).handler)
                handler->triggerMenuPopup();
        }
    });
    // Qt Creator has real sessions. Neither the event nor Vim's own carries a
    // target: <afile> and <amatch> are the current file (measured).
    connect(SessionManager::instance(), &SessionManager::sessionLoaded,
            this, [this](const QString &) {
        triggerAutocmdInAnyBuffer("SessionLoadPost", {});
    });
    connect(EditorManager::instance(), &EditorManager::currentEditorAboutToChange,
            this, &FakeVimPlugin::currentEditorAboutToChange);
    connect(EditorManager::instance(), &EditorManager::currentEditorChanged,
            this, [this](IEditor *editor) {
        updateEditorCommandLinePlacement();
        if (!settings().useFakeVim())
            return;
        if (!enterBufferOnce(editor)) {
            // Already entered once before: enterBuffer() fired WinEnter/BufEnter
            // for the first visit, but every later visit needs them again.
            if (FakeVimHandler *handler = m_editorToHandler.value(editor, {}).handler) {
                handler->triggerAutocmd("WinEnter");
                handler->triggerAutocmd("BufEnter");
            }
        }
    });

    connect(DocumentManager::instance(), &DocumentManager::allDocumentsRenamed,
            this, &FakeVimPlugin::allDocumentsRenamed);
    connect(DocumentManager::instance(), &DocumentManager::documentRenamed,
            this, &FakeVimPlugin::documentRenamed);

    FakeVimSettings &s = settings();
    connect(&s.useFakeVim, &FvBoolAspect::changed,
            this, [this, &s] { setUseFakeVim(s.useFakeVim()); });
    connect(&s.readVimRc, &FvBaseAspect::changed,
            this, &FakeVimPlugin::maybeReadVimRc);
    connect(&s.vimRcPath, &FvBaseAspect::changed,
            this, &FakeVimPlugin::maybeReadVimRc);
    connect(&s.relativeNumber, &FvBoolAspect::changed,
            this, [this, &s] { setShowRelativeLineNumbers(s.relativeNumber()); });
    connect(&s.blinkingCursor, &FvBoolAspect::changed,
            this, [this, &s] { setCursorBlinking(s.blinkingCursor()); });
    connect(&s.cursorFlashTime, &FvIntegerAspect::changed,
            this, [this, &s] { setCursorBlinking(s.blinkingCursor()); });
    connect(&s.commandLineInEditor, &FvBoolAspect::changed, this,
            [this] { updateEditorCommandLinePlacement(); });

    // Delayed operations.
    connect(this, &FakeVimPlugin::delayedQuitRequested,
            this, &FakeVimPlugin::handleDelayedQuit, Qt::QueuedConnection);
    connect(this, &FakeVimPlugin::delayedQuitAllRequested,
            this, &FakeVimPlugin::handleDelayedQuitAll, Qt::QueuedConnection);
    connect(this, &FakeVimPlugin::delayedBufferDeleteRequested,
            this, &FakeVimPlugin::handleBufferDelete, Qt::QueuedConnection);

    setCursorBlinking(s.blinkingCursor());
}

void FakeVimPlugin::userActionTriggered(int key)
{
    IEditor *editor = EditorManager::currentEditor();
    FakeVimHandler *handler = m_editorToHandler[editor].handler;
    if (handler) {
        // If disabled, enable FakeVim mode just for single user command.
        bool enableFakeVim = !settings().useFakeVim();
        if (enableFakeVim)
            setUseFakeVimInternal(true);

        const QString cmd = m_userCommandMap.value(key);
        handler->handleInput(cmd);

        if (enableFakeVim)
            setUseFakeVimInternal(false);
    }
}

FakeVimHandler *handlerForEditor(IEditor *editor)
{
    return dd ? dd->m_editorToHandler.value(editor).handler : nullptr;
}

void FakeVimPlugin::updateAllHightLights()
{
    const QList<IEditor *> editors = EditorManager::visibleEditors();
    for (IEditor *editor : editors) {
        QWidget *w = editor->widget();
        if (auto find = Aggregation::query<IFindSupport>(w))
            find->highlightAll(m_lastHighlight, FindRegularExpression | FindCaseSensitively);
    }
}

void FakeVimPlugin::createRelativeNumberWidget(IEditor *editor)
{
    if (auto textEditor = TextEditorWidget::fromEditor(editor)) {
        auto relativeNumbers = new RelativeNumbersColumn(textEditor);
        connect(&settings().relativeNumber, &FvBaseAspect::changed,
                relativeNumbers, &QObject::deleteLater);
        connect(&settings().useFakeVim, &FvBaseAspect::changed,
                relativeNumbers, &QObject::deleteLater);
        relativeNumbers->show();
        return;
    }

    // A view that numbers its own gutter is told to count from the caret,
    // rather than having a column of numbers laid over the top of it.
    if (const auto view = qobject_cast<TextEditor::TextViewport *>(
            TextEditor::keyTargetOf(editor))) {
        view->setRelativeLineNumbers(true);
        const auto follow = [view] {
            view->setRelativeLineNumbers(settings().useFakeVim()
                                         && settings().relativeNumber());
        };
        connect(&settings().relativeNumber, &FvBaseAspect::changed, view, follow);
        connect(&settings().useFakeVim, &FvBaseAspect::changed, view, follow);
    }
}

void FakeVimPlugin::readSettings()
{
    QtcSettings *settings = ICore::settings();

    m_exCommandMap = m_defaultExCommandMap;
    int size = settings->beginReadArray(exCommandMapGroup);
    for (int i = 0; i < size; ++i) {
        settings->setArrayIndex(i);
        const QString re = settings->value(reKey).toString();
        if (QRegularExpression(re).isValid()) {
            const QString id = settings->value(idKey).toString();
            m_exCommandMap[id] = re;
        }
    }
    settings->endArray();

    m_userCommandMap = m_defaultUserCommandMap;
    size = settings->beginReadArray(userCommandMapGroup);
    for (int i = 0; i < size; ++i) {
        settings->setArrayIndex(i);
        const int id = settings->value(idKey).toInt();
        const QString cmd = settings->value(cmdKey).toString();
        m_userCommandMap[id] = cmd;
    }
    settings->endArray();
}

void FakeVimPlugin::maybeReadVimRc()
{
    //qDebug() << theFakeVimSetting(ConfigReadVimRc)
    //    << theFakeVimSetting(ConfigReadVimRc)->value();
    //qDebug() << theFakeVimSetting(ConfigShiftWidth)->value();
    if (!settings().readVimRc())
        return;
    QString fileName = settings().vimRcPath().path();
    if (fileName.isEmpty()) {
        fileName = QStandardPaths::writableLocation(QStandardPaths::HomeLocation)
            + QLatin1String(HostOsInfo::isWindowsHost() ? "/_vimrc" : "/.vimrc");
    } else {
        // path may contain environment variables such as $HOME or
        // %USERPROFILE% (as advertised by the setting's placeholder text)
        fileName = Environment::systemEnvironment().expandVariables(fileName);
    }
    //qDebug() << "READING VIMRC: " << fileName;
    // Read it into a temporary handler for effects modifying global state.
    QPlainTextEdit editor;
    FakeVimHandler handler(&editor);
    handler.handleCommand("source " + fileName);
    //qDebug() << theFakeVimSetting(ConfigShiftWidth)->value();
}

static void triggerAction(Id id)
{
    Command *cmd = ActionManager::command(id);
    QTC_ASSERT(cmd, qDebug() << "UNKNOWN CODE: " << id.name(); return);
    QAction *action = cmd->action();
    QTC_ASSERT(action, return);
    action->trigger();
}

void FakeVimPlugin::setActionChecked(Id id, bool check)
{
    Command *cmd = ActionManager::command(id);
    QTC_ASSERT(cmd, return);
    QAction *action = cmd->action();
    QTC_ASSERT(action, return);
    QTC_ASSERT(action->isCheckable(), return);
    action->setChecked(!check); // trigger negates the action's state
    action->trigger();
}

static int moveRightWeight(const QRect &cursor, const QRect &other)
{
    if (!cursor.adjusted(999999, 0, 0, 0).intersects(other))
        return -1;
    const int dx = other.left() - cursor.right();
    const int dy = qAbs(cursor.center().y() - other.center().y());
    const int w = 10000 * dx + dy;
    return w;
}

static int moveLeftWeight(const QRect &cursor, const QRect &other)
{
    if (!cursor.adjusted(-999999, 0, 0, 0).intersects(other))
        return -1;
    const int dx = cursor.left() - other.right();
    const int dy = qAbs(cursor.center().y() -other.center().y());
    const int w = 10000 * dx + dy;
    return w;
}

static int moveUpWeight(const QRect &cursor, const QRect &other)
{
    if (!cursor.adjusted(0, 0, 0, -999999).intersects(other))
        return -1;
    const int dy = cursor.top() - other.bottom();
    const int dx = qAbs(cursor.center().x() - other.center().x());
    const int w = 10000 * dy + dx;
    return w;
}

static int moveDownWeight(const QRect &cursor, const QRect &other)
{
    if (!cursor.adjusted(0, 0, 0, 999999).intersects(other))
        return -1;
    const int dy = other.top() - cursor.bottom();
    const int dx = qAbs(cursor.center().x() - other.center().x());
    const int w = 10000 * dy + dx;
    return w;
}

void FakeVimPlugin::moveSomewhere(FakeVimHandler *handler, DistFunction f, int count)
{
    QTC_ASSERT(handler, return);
    QWidget *w = handler->widget();
    QRect rc;
    if (auto pe = qobject_cast<QPlainTextEdit *>(w)) {
        rc = pe->cursorRect();
    } else if (auto pe = qobject_cast<PlainTextEdit *>(w)) {
        rc = pe->cursorRect();
    } else {
        QTC_ASSERT(false, return);
    }
    QRect cursorRect(w->mapToGlobal(rc.topLeft()), w->mapToGlobal(rc.bottomRight()));
    //qDebug() << "\nCURSOR: " << cursorRect;

    IEditor *bestEditor = nullptr;
    int repeat = count;

    IEditor *currentEditor = EditorManager::currentEditor();
    QList<IEditor *> editors = EditorManager::visibleEditors();
    while (repeat < 0 || repeat-- > 0) {
        editors.removeOne(currentEditor);
        int bestValue = -1;
        for (IEditor *editor : std::as_const(editors)) {
            QWidget *w = editor->widget();
            QRect editorRect(w->mapToGlobal(w->geometry().topLeft()),
                    w->mapToGlobal(w->geometry().bottomRight()));
            //qDebug() << "   EDITOR: " << editorRect << editor;

            int value = f(cursorRect, editorRect);
            if (value != -1 && (bestValue == -1 || value < bestValue)) {
                bestValue = value;
                bestEditor = editor;
                //qDebug() << "          BEST SO FAR: " << bestValue << bestEditor;
            }
        }
        if (bestValue == -1)
            break;

        currentEditor = bestEditor;
        //qDebug() << "     BEST: " << bestValue << bestEditor;
    }

    // FIME: This is know to fail as the EditorManager will fall back to
    // the current editor's view. Needs additional public API there.
    if (bestEditor)
        EditorManager::activateEditor(bestEditor);
}

void FakeVimPlugin::keepOnlyWindow()
{
    IEditor *currentEditor = EditorManager::currentEditor();
    QList<IEditor *> editors = EditorManager::visibleEditors();
    editors.removeOne(currentEditor);

    for (IEditor *editor : std::as_const(editors)) {
        EditorManager::activateEditor(editor);
        triggerAction(Core::Constants::REMOVE_CURRENT_SPLIT);
    }
}

void FakeVimPlugin::fold(FakeVimHandler *handler, int depth, bool fold)
{
    QTC_ASSERT(handler, return);
    QTextDocument *doc = handler->textCursor().document();
    QTC_ASSERT(doc, return);
    auto documentLayout = qobject_cast<TextDocumentLayout*>(doc->documentLayout());
    QTC_ASSERT(documentLayout, return);

    QTextBlock block = handler->textCursor().block();
    int indent = TextBlockUserData::foldingIndent(block);
    if (fold) {
        if (TextBlockUserData::isFolded(block)) {
            while (block.isValid() && (TextBlockUserData::foldingIndent(block) >= indent
                || !block.isVisible())) {
                block = block.previous();
            }
        }
        if (TextBlockUserData::canFold(block))
            ++indent;
        while (depth != 0 && block.isValid()) {
            const int indent2 = TextBlockUserData::foldingIndent(block);
            if (TextBlockUserData::canFold(block) && indent2 < indent) {
                TextBlockUserData::doFoldOrUnfold(block, false);
                if (depth > 0)
                    --depth;
                indent = indent2;
            }
            block = block.previous();
        }
    } else {
        if (TextBlockUserData::isFolded(block)) {
            if (depth < 0) {
                // recursively open fold
                while (block.isValid()
                    && TextBlockUserData::foldingIndent(block) >= indent) {
                    if (TextBlockUserData::canFold(block))
                        TextBlockUserData::doFoldOrUnfold(block, true);
                    block = block.next();
                }
            } else {
                if (TextBlockUserData::canFold(block)) {
                    TextBlockUserData::doFoldOrUnfold(block, true);
                    if (depth > 0)
                        --depth;
                }
            }
        }
    }

    documentLayout->requestUpdate();
    documentLayout->emitDocumentSizeChanged();
}

// This class defers deletion of a child FakeVimHandler using deleteLater().
class DeferredDeleter : public QObject
{
    Q_OBJECT

    FakeVimHandler *m_handler;

public:
    DeferredDeleter(QObject *parent, FakeVimHandler *handler)
        : QObject(parent), m_handler(handler)
    {}

    ~DeferredDeleter() override
    {
        if (m_handler) {
            m_handler->disconnectFromEditor();
            m_handler->deleteLater();
            m_handler = nullptr;
        }
    }
};

// Vim detects the file type by matching file names and, where that is not
// enough, by looking into the file. Qt Creator already did all of that to pick
// an editor and a highlighter, so translate its MIME type instead of carrying a
// copy of Vim's filetype.vim. Names follow Vim's, since scripts match on them.
static QString vimFileType(const IDocument *document)
{
    static const QHash<QString, QString> mimeToFileType = {
        {"text/x-c++src", "cpp"}, {"text/x-c++hdr", "cpp"},
        {"text/x-csrc", "c"}, {"text/x-chdr", "c"},
        {"text/x-objcsrc", "objc"}, {"text/x-objc++src", "objcpp"},
        {"text/x-csharp", "cs"}, {"text/x-java", "java"},
        {"text/x-python", "python"}, {"text/x-python3", "python"},
        {"text/x-ruby", "ruby"}, {"text/x-perl", "perl"},
        {"application/x-shellscript", "sh"}, {"text/x-shellscript", "sh"},
        {"application/x-perl", "perl"}, {"application/x-ruby", "ruby"},
        {"text/x-go", "go"}, {"text/rust", "rust"}, {"text/x-rust", "rust"},
        {"text/x-lua", "lua"}, {"text/x-sql", "sql"}, {"text/x-tex", "tex"},
        {"text/x-haskell", "haskell"},
        {"application/javascript", "javascript"},
        {"text/javascript", "javascript"}, {"application/json", "json"},
        {"application/x-typescript", "typescript"},
        {"text/x-qml", "qml"}, {"text/x-qt.qml", "qml"},
        {"text/x-cmake", "cmake"}, {"text/x-cmake-project", "cmake"},
        {"text/x-makefile", "make"}, {"text/x-qmake-project", "qmake"},
        {"text/x-qbs-project", "qbs"},
        {"application/x-yaml", "yaml"}, {"text/x-yaml", "yaml"},
        {"application/toml", "toml"},
        {"text/html", "html"}, {"application/xhtml+xml", "html"},
        {"text/css", "css"}, {"text/markdown", "markdown"},
        {"application/xml", "xml"}, {"text/xml", "xml"},
        {"image/svg+xml", "svg"}, {"text/x-qt.ui", "xml"},
        {"application/vnd.qt.xml.resource", "xml"},
        {"text/x-vim", "vim"}, {"application/x-desktop", "desktop"},
        {"text/x-diff", "diff"}, {"text/x-patch", "diff"},
        {"text/x-ini", "dosini"}
    };

    QString mimeName = document->mimeType();
    if (mimeName.isEmpty() && !document->filePath().isEmpty())
        mimeName = Utils::mimeTypeForFile(document->filePath()).name();
    return mimeToFileType.value(mimeName);
}

// A key bound to a command has to be claimed before the shortcut system runs,
// and the only offer of that comes as a QEvent::ShortcutOverride to the focus
// *widget*. An item never sees it, so FakeVim's own filter cannot answer for
// the Qt Quick editor. This carries the same answer to the view, which is
// asked at the right moment.
class QuickEditorKeyClaim final : public TextEditor::EditHandler
{
public:
    QuickEditorKeyClaim(Core::IEditor *editor, FakeVimHandler *handler)
        : TextEditor::EditHandler(editor), m_handler(handler)
    {}

    // Only the claim: the keys themselves still arrive through the event
    // filter FakeVim installs, which runs before the view sees them.
    bool handleKeyPress(QKeyEvent *, const std::function<void()> &) override { return false; }

    bool wantsKeyBeforeShortcuts(QKeyEvent *event) override
    {
        return m_handler && settings().useFakeVim() && m_handler->wantsKeyBeforeShortcuts(event);
    }

private:
    const QPointer<FakeVimHandler> m_handler;
};

// Driving the Qt Quick editor. Everything below is what a TextViewport
// answers of FakeVimEditorAdapter; the interesting one is keyTarget(), which
// is the item rather than the widget, because that is where key events arrive.
class ViewportAdapter final : public FakeVimEditorAdapter
{
public:
    ViewportAdapter(TextEditor::TextViewport *view, QWidget *host)
        : m_view(view), m_host(host)
    {}

    // The widget the item is drawn in. The item is not one, and callers of
    // FakeVimHandler::widget() want something to parent and place against.
    QWidget *widget() const override { return m_host; }
    QObject *keyTarget() const override { return m_view; }

    // This view keeps both notions itself. Asking its host by name - which is
    // what the default does - reaches a QuickWidget that has never heard of
    // either, so both used to answer false and Vim took the keys.
    bool inSnippetMode() const override
    {
        return m_view && m_view->hasSnippetPlaceholders();
    }
    bool inInlineRename() const override
    {
        return m_view && m_view->hasActiveEditHandler();
    }

    QTextDocument *document() const override
    {
        TextEditor::TextDocument * const doc = m_view ? m_view->textDocument() : nullptr;
        return doc ? doc->document() : nullptr;
    }
    QTextCursor textCursor() const override { return m_view ? m_view->textCursor() : QTextCursor(); }
    void setTextCursor(const QTextCursor &cursor) override
    {
        if (m_view)
            m_view->setTextCursor(cursor);
    }
    QRect cursorRect(const QTextCursor &cursor) const override
    {
        if (!m_view)
            return {};
        return m_view->rectangleAt(cursor.position()).toAlignedRect();
    }
    QTextCursor cursorForPosition(const QPoint &point) const override
    {
        return m_view ? m_view->cursorForPosition(point) : QTextCursor();
    }
    void ensureCursorVisible() override
    {
        if (m_view)
            m_view->ensureCursorVisible();
    }
    int viewportWidth() const override { return m_view ? int(m_view->width()) : 0; }
    int viewportHeight() const override { return m_view ? int(m_view->height()) : 0; }
    int height() const override { return viewportHeight(); }
    QFont font() const override
    {
        TextEditor::TextDocument * const doc = m_view ? m_view->textDocument() : nullptr;
        return doc ? doc->fontSettings().font() : QFont();
    }
    QPalette palette() const override { return QGuiApplication::palette(); }
    bool isReadOnly() const override { return !m_view || m_view->isReadOnly(); }
    void setTabStopDistance(qreal distance) override
    {
        if (m_view)
            m_view->setTabStopDistance(distance);
    }
    bool overwriteMode() const override { return m_view && m_view->overwriteMode(); }
    void setOverwriteMode(bool overwrite) override
    {
        if (m_view)
            m_view->setOverwriteMode(overwrite);
    }
    void undo() override { if (m_view) m_view->undo(); }
    void redo() override { if (m_view) m_view->redo(); }
    bool centerOnScroll() const override { return m_view && m_view->centerOnScroll(); }
    void setCenterOnScroll(bool on) override
    {
        if (m_view)
            m_view->setCenterOnScroll(on);
    }

    QMetaObject::Connection connectCursorPositionChanged(
        QObject *receiver, const std::function<void()> &slot) override
    {
        return QObject::connect(m_view, &TextEditor::TextViewport::cursorPositionChanged,
                                receiver, slot);
    }

private:
    const QPointer<TextEditor::TextViewport> m_view;
    const QPointer<QWidget> m_host;
};

#ifdef WITH_TESTS

// FakeVim installs its handler only on an editor it recognises, and until now
// that meant one whose widget aggregates a QPlainTextEdit. The Qt Quick editor
// draws its text with an item, so FakeVim was inert there - silently, which is
// the worst way for an editing mode to be off.
class FakeVimInQuickEditorTest final : public QObject
{
    Q_OBJECT

private slots:
    // What this class must not print. It printed "No such method" fourteen
    // times a run for as long as there was a Qt Quick editor to drive, and
    // nothing failed: FakeVim was asking the view's host a question only a
    // widget could answer, so a snippet and a rename both lost their keys.
    //
    // After every test rather than at the end: FollowSymbolTest learnt that a
    // check which runs when the class finishes is no use against a defect that
    // stops the class finishing.
    void init()
    {
        m_mustNotSay = std::make_unique<Utils::GuiTest::CollectedWarnings>(
            QStringList{"updatePolish", "polish() loop", "No such method"});

        // Every test here opens a C++ file and drives the Qt Quick view, so
        // the view is asked for rather than assumed. It is the default, but
        // QTC_WIDGET_CPP_EDITOR is the documented way to turn that round -
        // and under it these tests were failing with "the C++ file opened in
        // a widget editor", which is true and not their subject.
        m_cppFactory = TextEditor::TextEditorFactory::preferredFactoryFor(
            Utils::FilePath::fromString("a.cpp"));
        if (m_cppFactory) {
            m_factoryWasQuick = m_cppFactory->usesQuickEditor();
            m_cppFactory->setUsesQuickEditor(true);
        }
    }

    void cleanup()
    {
        if (m_cppFactory)
            m_cppFactory->setUsesQuickEditor(m_factoryWasQuick);
        m_cppFactory = nullptr;

        const QStringList said = m_mustNotSay ? m_mustNotSay->hits() : QStringList();
        m_mustNotSay.reset();
        QVERIFY2(said.isEmpty(),
                 qPrintable(QString("printed %1 of them, first: %2")
                                .arg(said.size()).arg(said.value(0))));
    }

    // Vim leaves the keys alone while the view is in the middle of something
    // that wants them: a snippet being filled in, or an in-place rename. It
    // used to ask the editor's widget for both by name, which for a view that
    // is not a widget reaches the Qt Quick host - an object that has never
    // heard of either, so both answered false and Vim took Escape and Enter
    // from a snippet and a rename alike. QMetaObject said so in the log, 14
    // times a run, for as long as this editor has existed.
    void testVimLeavesASnippetItsOwnKeys()
    {
        const bool wasOn = settings().useFakeVim();
        const QScopeGuard restore([wasOn] { settings().useFakeVim.setValue(wasOn); });
        settings().useFakeVim.setValue(true);

        Utils::TemporaryDirectory dir("fakevim-snippet-keys");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("main.cpp");
        QVERIFY(file.writeFileContents("class name {};\n"));

        IEditor * const editor = EditorManager::openEditor(file);
        QVERIFY2(editor, "the editor manager opened nothing");
        const QScopeGuard closeIt([editor] { EditorManager::closeEditors({editor}, false); });
        QVERIFY2(!TextEditor::TextEditorWidget::fromEditor(editor),
                 "the C++ file opened in a widget editor, so this tests nothing");

        auto * const view = qobject_cast<TextEditor::TextViewport *>(
            TextEditor::keyTargetOf(editor));
        QVERIFY2(view, "FakeVim is not driving this editor at all");

        QVERIFY2(dd->m_editorToHandler.value(editor, {}).handler,
                 "no FakeVim handler was installed on the Quick editor");

        // A snippet's holes, put there the way the assist target does.
        view->setSnippetPlaceholders({{6, 10, 0, false, nullptr}});
        QVERIFY2(view->hasSnippetPlaceholders(), "the view kept none of the holes");

        // Escape, which means "done with this snippet" to the view and "leave
        // insert mode" to Vim. The view has to get it.
        QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
        QCoreApplication::sendEvent(view, &escape);
        QVERIFY2(!view->hasSnippetPlaceholders(),
                 "Vim took Escape from the snippet, so it could not be given up");
    }

    // The other half of the comment above: a rename being typed into. Vim
    // has to leave Escape alone for it too, and the answer comes a different
    // way from the snippet's - through the edit handler CppEditor parents to
    // the editor, rather than from anything the view keeps itself.
    void testVimLeavesAnInPlaceRenameItsOwnKeys()
    {
        const bool wasOn = settings().useFakeVim();
        const QScopeGuard restore([wasOn] { settings().useFakeVim.setValue(wasOn); });
        settings().useFakeVim.setValue(true);

        Utils::TemporaryDirectory dir("fakevim-rename-keys");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("main.cpp");
        QVERIFY(file.writeFileContents(
            "int main()\n{\n    int alpha = 1;\n    return alpha + alpha;\n}\n"));

        IEditor * const editor = EditorManager::openEditor(file);
        QVERIFY2(editor, "the editor manager opened nothing");
        const QScopeGuard closeIt([editor] { EditorManager::closeEditors({editor}, false); });
        QVERIFY2(!TextEditor::TextEditorWidget::fromEditor(editor),
                 "the C++ file opened in a widget editor, so this tests nothing");

        auto * const view = qobject_cast<TextEditor::TextViewport *>(
            TextEditor::keyTargetOf(editor));
        QVERIFY2(view, "FakeVim is not driving this editor at all");
        QVERIFY2(dd->m_editorToHandler.value(editor, {}).handler,
                 "no FakeVim handler was installed on the Quick editor");

        // Nothing is being typed into yet, so the answer has to be no before
        // it can mean anything when it is yes.
        QVERIFY2(!view->hasActiveEditHandler(),
                 "something claimed to be mid-edit in a file just opened");

        // The rename over the three uses of alpha. Asked until it takes: the
        // uses come from the code model, which reads the file on its own
        // thread, and until it has there is nothing local to rename.
        editor->gotoLine(3, 10);
        QTRY_VERIFY_WITH_TIMEOUT(
            [&] {
                TextEditor::renameSymbolUnderCursorIn(editor);
                return view->hasActiveEditHandler();
            }(),
            30000);

        // Vim draws its block cursor by putting the editor in overwrite mode,
        // and while the editor is taking the keys the caret has to be the thin
        // insert one instead. Asked after a cursor move, which is what
        // recomputes the shape - and moved within the name so the rename is
        // still the thing being typed into.
        view->setCursorPosition(view->cursorPosition() + 1);
        QTRY_VERIFY2(!view->overwriteMode(),
                     "the caret stayed a Vim block while the rename was being typed into");

        // Escape, which means "done renaming" to the view and "leave insert
        // mode" to Vim. The view has to get it, or the rename cannot be
        // finished at all.
        QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
        QCoreApplication::sendEvent(view, &escape);
        QVERIFY2(!view->hasActiveEditHandler(),
                 "Vim took Escape from the rename, so it could not be given up");
    }

    // The in-editor command line. It parented a MiniBuffer to a
    // TextEditorWidget and reserved a strip at the bottom so it never covers
    // text, so with a C++ file - which opens in the Qt Quick editor - the
    // setting was silently ignored and the reader got the global one.
    void testTheCommandLineSitsInTheQuickEditor()
    {
        const bool wasOn = settings().useFakeVim();
        const bool wasInEditor = settings().commandLineInEditor();
        const QScopeGuard restore([wasOn, wasInEditor] {
            settings().useFakeVim.setValue(wasOn);
            settings().commandLineInEditor.setValue(wasInEditor);
        });
        settings().useFakeVim.setValue(true);
        settings().commandLineInEditor.setValue(true);

        Utils::TemporaryDirectory dir("fakevim-command-line-in-the-quick-editor");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("main.cpp");
        QVERIFY(file.writeFileContents("hello\nworld\n"));

        IEditor * const editor = EditorManager::openEditor(file);
        QVERIFY2(editor, "the editor manager opened nothing");
        const QScopeGuard closeIt([editor] { EditorManager::closeEditors({editor}, false); });
        QVERIFY2(!TextEditor::TextEditorWidget::fromEditor(editor),
                 "the C++ file opened in a widget editor, so this tests nothing");

        auto * const view = qobject_cast<TextEditor::TextViewport *>(
            TextEditor::keyTargetOf(editor));
        QVERIFY2(view, "FakeVim is not driving this editor at all");

        // The command line is put over the editor's own widget, and the view
        // is asked for room so it never covers the text.
        QTRY_VERIFY2(dd->m_editorMiniBuffer, "no command line was made for this editor");
        QCOMPARE(dd->m_editorMiniBuffer->parentWidget(), editor->widget());
        QTRY_VERIFY2(view->textAreaHeight() < view->height(),
                     "the view kept all its room, so the command line covers text");

        // And it is given back when the setting goes off.
        settings().commandLineInEditor.setValue(false);
        QTRY_COMPARE(view->textAreaHeight(), view->height());
    }

    void testFakeVimDrivesTheQuickEditor()
    {
        const bool wasOn = settings().useFakeVim();
        const QScopeGuard restore([wasOn] { settings().useFakeVim.setValue(wasOn); });
        settings().useFakeVim.setValue(true);

        Utils::TemporaryDirectory dir("fakevim-in-the-quick-editor");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("main.cpp");
        QVERIFY(file.writeFileContents("hello\nworld\n"));

        IEditor * const editor = EditorManager::openEditor(file);
        QVERIFY2(editor, "the editor manager opened nothing");
        const QScopeGuard closeIt([editor] { EditorManager::closeEditors({editor}, false); });

        // The premise: no widget to attach to, which is what used to end this.
        QVERIFY2(!TextEditor::TextEditorWidget::fromEditor(editor),
                 "the C++ file opened in a widget editor, so this tests nothing");
        QObject * const keyTarget = TextEditor::keyTargetOf(editor);
        QVERIFY2(qobject_cast<TextEditor::TextViewport *>(keyTarget),
                 "the Quick editor has no viewport to drive");

        const auto document = qobject_cast<TextEditor::TextDocument *>(editor->document());
        QVERIFY(document);
        QTextCursor start(document->document());
        TextEditor::setTextCursorOf(editor, start);

        // Sent to the object FakeVim filters, which is what its event filter
        // is installed on. That a keystroke reaches that object at all is the
        // Quick editor's own business and is tested there.
        const auto send = [keyTarget](int key, const QString &text) {
            QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier, text);
            QCoreApplication::sendEvent(keyTarget, &press);
        };

        // Command mode, then "x" - delete the character under the cursor.
        send(Qt::Key_Escape, {});
        send(Qt::Key_X, "x");
        QCOMPARE(document->document()->toPlainText(), QString("ello\nworld\n"));

        // And "dd" takes the line, so this is vim and not a stray keystroke.
        send(Qt::Key_D, "d");
        send(Qt::Key_D, "d");
        QCOMPARE(document->document()->toPlainText(), QString("world\n"));
    }

    // Suggestions are held off outside insert mode. That took a widget, so in
    // the Qt Quick editor it was skipped and a suggestion could sit on top of
    // vim's command mode.
    void testSuggestionsAreHeldOffOutsideInsertMode()
    {
        const bool wasOn = settings().useFakeVim();
        const QScopeGuard restore([wasOn] { settings().useFakeVim.setValue(wasOn); });
        settings().useFakeVim.setValue(true);

        Utils::TemporaryDirectory dir("fakevim-suggestions-in-the-quick-editor");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("main.cpp");
        QVERIFY(file.writeFileContents("int a;\nint b;\n"));

        IEditor * const editor = EditorManager::openEditor(file);
        QVERIFY2(editor, "the editor manager opened nothing");
        const QScopeGuard closeIt([editor] { EditorManager::closeEditors({editor}, false); });
        QVERIFY2(!TextEditor::TextEditorWidget::fromEditor(editor),
                 "the C++ file opened in a widget editor, so this tests nothing");
        const auto view = qobject_cast<TextEditor::TextViewport *>(
            TextEditor::keyTargetOf(editor));
        QVERIFY(view);
        QObject * const keyTarget = view;

        const auto send = [keyTarget](int key, const QString &text) {
            QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier, text);
            QCoreApplication::sendEvent(keyTarget, &press);
        };

        // Command mode: nothing should be offered.
        send(Qt::Key_Escape, {});
        QTRY_VERIFY2(view->suggestionsBlocked(),
                     "suggestions were left on offer in command mode");

        // Insert mode releases them.
        send(Qt::Key_I, "i");
        QTRY_VERIFY2(!view->suggestionsBlocked(),
                     "suggestions were still held off in insert mode");

        // And leaving it takes them away again.
        send(Qt::Key_Escape, {});
        QTRY_VERIFY2(view->suggestionsBlocked(),
                     "suggestions were left on offer after leaving insert mode");
    }

    // "relativenumber". The widget editor lays a column of numbers over its
    // own gutter, which needs a widget to lay it on; a view that draws its own
    // gutter is told to number it from the caret instead.
    void testRelativeNumbersReachTheQuickEditorsGutter()
    {
        const bool wasOn = settings().useFakeVim();
        const bool wasRelative = settings().relativeNumber();
        const QScopeGuard restore([wasOn, wasRelative] {
            settings().useFakeVim.setValue(wasOn);
            settings().relativeNumber.setValue(wasRelative);
        });
        settings().useFakeVim.setValue(true);
        settings().relativeNumber.setValue(true);

        Utils::TemporaryDirectory dir("fakevim-relative-numbers");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("main.cpp");
        QVERIFY(file.writeFileContents("int a;\nint b;\nint c;\n"));

        IEditor * const editor = EditorManager::openEditor(file);
        QVERIFY2(editor, "the editor manager opened nothing");
        const QScopeGuard closeIt([editor] { EditorManager::closeEditors({editor}, false); });
        QVERIFY2(!TextEditor::TextEditorWidget::fromEditor(editor),
                 "the C++ file opened in a widget editor, so this tests nothing");

        const auto view = qobject_cast<TextEditor::TextViewport *>(
            TextEditor::keyTargetOf(editor));
        QVERIFY(view);
        QVERIFY2(view->relativeLineNumbers(),
                 "the gutter was left numbering from the top of the file");

        // Turning the setting off puts it back.
        settings().relativeNumber.setValue(false);
        QVERIFY2(!view->relativeLineNumbers(),
                 "the gutter kept counting from the caret after the setting went off");
        settings().relativeNumber.setValue(true);
        QVERIFY(view->relativeLineNumbers());

        // As does switching FakeVim off altogether.
        settings().useFakeVim.setValue(false);
        QVERIFY2(!view->relativeLineNumbers(),
                 "the gutter kept counting from the caret after FakeVim went off");
    }

    // A key Creator binds elsewhere has to reach vim instead of firing the
    // shortcut, and the only moment to say so is the ShortcutOverride Qt
    // offers the focus widget. An item never sees that, so this is carried to
    // the view instead - which is asked at the right moment.
    void testAKeyBoundToACommandIsClaimedForVim()
    {
        const bool wasOn = settings().useFakeVim();
        const QScopeGuard restore([wasOn] { settings().useFakeVim.setValue(wasOn); });
        settings().useFakeVim.setValue(true);

        Utils::TemporaryDirectory dir("fakevim-claims-a-key");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("main.cpp");
        QVERIFY(file.writeFileContents("hello\n"));

        IEditor * const editor = EditorManager::openEditor(file);
        QVERIFY2(editor, "the editor manager opened nothing");
        const QScopeGuard closeIt([editor] { EditorManager::closeEditors({editor}, false); });
        QVERIFY2(!TextEditor::TextEditorWidget::fromEditor(editor),
                 "the C++ file opened in a widget editor, so this tests nothing");
        // By name rather than by type: QQuickWidget lives in a module this
        // plugin has no reason to link, and all this needs is the widget Qt
        // offers the override to.
        QWidget *host = nullptr;
        for (QWidget * const candidate : editor->widget()->findChildren<QWidget *>()) {
            if (candidate->inherits("QQuickWidget")) {
                host = candidate;
                break;
            }
        }
        QVERIFY2(host, "the Quick editor has no widget to be offered a shortcut override");

        // The offer as Qt makes it, to the widget that has the focus.
        // With the text a real keystroke carries: vim matches a mapping on the
        // input, and Ctrl+W arrives as the control character.
        const auto offer = [host](int key, Qt::KeyboardModifiers mods, const QString &written) {
            QKeyEvent event(QEvent::ShortcutOverride, key, mods, written);
            event.ignore();
            QCoreApplication::sendEvent(host, &event);
            return event.isAccepted();
        };

        // A C++ editor already carries an EditHandler of its own for renaming.
        // Both have to be asked, or whichever was built first would answer for
        // the other.
        const QList<TextEditor::EditHandler *> handlers
            = editor->findChildren<TextEditor::EditHandler *>();
        QVERIFY2(handlers.size() >= 2,
                 qPrintable(QString("expected the language's handler and vim's, found %1")
                                .arg(handlers.size())));

        QVERIFY2(dd->m_editorToHandler.value(editor, {}).handler,
                 "no FakeVim handler was installed on the Quick editor");

        // Vim's Ctrl, which on a Mac is the key Qt calls Meta.
        const Qt::KeyboardModifiers vimControl = Utils::HostOsInfo::controlModifier();
        const QString written(QChar(0x17));

        // Vim wants nearly every Ctrl combination, and Creator binds plenty of
        // them - this is the whole reason the claim exists.
        const bool wasPassing = settings().passControlKey();
        const QScopeGuard restorePassing(
            [wasPassing] { settings().passControlKey.setValue(wasPassing); });
        settings().passControlKey.setValue(false);
        QVERIFY2(offer(Qt::Key_W, vimControl, written),
                 "Ctrl+W was left to the shortcut system instead of reaching vim");

        // Unless the reader has asked for control keys to be passed through,
        // which is what that setting is for.
        settings().passControlKey.setValue(true);
        QVERIFY2(!offer(Qt::Key_W, vimControl, written),
                 "vim claimed Ctrl+W although control keys are set to pass through");
        settings().passControlKey.setValue(false);

        // And with FakeVim switched off it is Creator's again.
        settings().useFakeVim.setValue(false);
        QVERIFY2(!offer(Qt::Key_W, vimControl, written),
                 "vim claimed a key while switched off");
    }
private:
    // Declared last on purpose. A "private:" section in the middle of a
    // QObject takes every function below it out of "private slots:", and moc
    // then registers none of them - which quietly stopped five of this
    // class's six tests from running when this member was put in the middle.
    std::unique_ptr<Utils::GuiTest::CollectedWarnings> m_mustNotSay;
    TextEditor::TextEditorFactory *m_cppFactory = nullptr;
    bool m_factoryWasQuick = false;

};

QObject *createFakeVimInQuickEditorTest()
{
    return new FakeVimInQuickEditorTest;
}

#endif // WITH_TESTS

void FakeVimPlugin::editorOpened(IEditor *editor)
{
    if (!editor)
        return;

    if (m_editorToHandler.contains(editor)) {
        // We get here via the call from the duplicated handler in case
        // it was triggered by triggerAction(Core::Constants::SPLIT).
        // On the other hand, we need the path from there to support the
        // case of manual calls to IEditor::duplicate().
        return;
    }

    QWidget *widget = editor->widget();
    if (!widget)
        return;

    // The Qt Quick editor draws its text with an item rather than a widget, so
    // none of the three below finds anything in it. It is driven through the
    // same handler over an adapter of its own.
    std::unique_ptr<FakeVimEditorAdapter> adapter;
    // keyTargetOf() answers the item for a Quick editor and the widget for a
    // widget one, which is exactly the question being asked here.
    if (const auto view = qobject_cast<TextEditor::TextViewport *>(
            TextEditor::keyTargetOf(editor))) {
        adapter = std::make_unique<ViewportAdapter>(view, widget);
    } else if (auto edit = Aggregation::query<QTextEdit>(widget)) {
        widget = edit;
    } else if (auto edit = Aggregation::query<QPlainTextEdit>(widget)) {
        widget = edit;
    } else if (auto edit = Aggregation::query<Utils::PlainTextEdit>(widget)) {
        widget = edit;
    } else {
        return;
    }

    // Duplicated editors are not signalled by the EditorManager. Track them nevertheless.
    connect(editor, &IEditor::editorDuplicated, this, [this](IEditor *duplicate) {
        editorOpened(duplicate);
        connect(duplicate, &QObject::destroyed, this, [this, duplicate] {
            m_editorToHandler.remove(duplicate);
        });
    });

    auto tew = TextEditorWidget::fromEditor(editor);

    //qDebug() << "OPENING: " << editor << editor->widget()
    //    << "MODE: " << theFakeVimSetting(ConfigUseFakeVim)->value();

    const bool drivesAnItem = bool(adapter);
    auto handler = adapter ? new FakeVimHandler(std::move(adapter), nullptr)
                           : new FakeVimHandler(widget, nullptr);
    if (drivesAnItem) {
        // Parented to the editor, which is where the view looks for it.
        new QuickEditorKeyClaim(editor, handler);
    }
    // the handler might have triggered the deletion of the editor:
    // make sure that it can return before being deleted itself
    new DeferredDeleter(widget, handler);
    m_editorToHandler[editor].handler = handler;

    handler->extraInformationChanged.set([this](const QString &text) {
        EditorManager::splitSideBySide();
        QString title = "stdout.txt";
        IEditor *iedit = EditorManager::openEditorWithContents(Id(), &title, text.toUtf8());
        EditorManager::activateEditor(iedit);
        FakeVimHandler *handler = m_editorToHandler.value(iedit, {}).handler;
        QTC_ASSERT(handler, return);
        handler->handleCommand("0");
    });

    handler->commandBufferChanged.set(
        [this, handler](const QString &contents, int cursorPos, int anchorPos, int messageLevel) {
            showCommandBuffer(handler, contents, cursorPos, anchorPos, messageLevel);
        });

    handler->highlightFormatRequested.set([alive = QPointer<IEditor>(editor)](
                                              const QString &group) {
        // What matchadd() paints with: the group names Vim uses, drawn the way
        // this editor draws the same thing.
        static const QHash<QString, TextStyle> styles = {
            {"Search", C_SEARCH_RESULT},
            {"IncSearch", C_SEARCH_RESULT},
            {"Visual", C_SELECTION},
            {"ErrorMsg", C_ERROR},
            {"Error", C_ERROR},
            {"WarningMsg", C_WARNING},
            {"Todo", C_WARNING},
            {"Comment", C_COMMENT},
            {"String", C_STRING},
            {"Number", C_NUMBER},
            {"Type", C_TYPE},
            {"Statement", C_KEYWORD},
            {"Keyword", C_KEYWORD},
            {"Function", C_FUNCTION},
            {"Underlined", C_LINK}
        };
        const auto style = styles.constFind(group);
        if (style == styles.constEnd() || !alive)
            return QTextCharFormat();
        const TextDocumentPtr document = TextEditor::textDocumentPtr(alive);
        if (!document)
            return QTextCharFormat();
        return document->fontSettings().toTextCharFormat(*style);
    });

    // Guarded: the handler outlives the editor during teardown, and this
    // callback is reached from there.
    handler->selectionChanged.set([alive = QPointer<IEditor>(editor)](
                                      const QList<QTextEdit::ExtraSelection> &selection) {
        if (!alive)
            return;
        QList<TextDocument::ExtraSelection> forView;
        forView.reserve(selection.size());
        for (const QTextEdit::ExtraSelection &one : selection)
            forView.append({one.cursor, one.format});
        TextEditor::setViewSelections(alive, TextEditorWidget::FakeVimSelection, forView);
    });

    handler->tabPressedInInsertMode.set([alive = QPointer<IEditor>(editor)]() {
        if (TextSuggestion * const suggestion = TextEditor::currentSuggestionIn(alive)) {
            suggestion->apply();
            return false;
        }

        return true;
    });

    handler->modeChanged.set([tew, this, editor](bool insertMode) {
        HandlerAndData &handlerAndData = m_editorToHandler[editor];
        if (!handlerAndData.handler || !handlerAndData.handler->inFakeVimMode())
            return;

        // inFakeVimMode() says a key is being processed, which happens while
        // FakeVim is switched off too. Without this the block below is taken
        // once as the editor opens and never released, because a mode change
        // is the only thing that releases it and none is coming.
        if (!settings().useFakeVim())
            return;

        // We don't want to show suggestions unless we are in insert mode.
        if (insertMode != (handlerAndData.suggestionBlocker == nullptr)) {
            handlerAndData.suggestionBlocker
                = insertMode ? nullptr : TextEditor::blockSuggestionsIn(editor);
        }

        TextEditor::clearSuggestionIn(editor);
    });

    handler->highlightMatches.set([this](const QString &needle) {
        m_lastHighlight = needle;
        for (IEditor *editor : EditorManager::visibleEditors()) {
            QWidget *w = editor->widget();
            if (auto find = Aggregation::query<IFindSupport>(w))
                find->highlightAll(needle, FindRegularExpression | FindCaseSensitively);
        }
    });

    handler->moveToMatchingParenthesis.set([](bool *moved, bool *forward, QTextCursor *cursor) {
        *moved = false;

        bool undoFakeEOL = false;
        if (cursor->atBlockEnd() && cursor->block().length() > 1) {
            cursor->movePosition(QTextCursor::Left, QTextCursor::KeepAnchor, 1);
            undoFakeEOL = true;
        }
        TextBlockUserData::MatchType match = TextBlockUserData::matchCursorForward(cursor);
        if (match == TextBlockUserData::Match) {
            *moved = true;
            *forward = true;
        } else {
            if (undoFakeEOL)
                cursor->movePosition(QTextCursor::Right, QTextCursor::KeepAnchor, 1);
            if (match == TextBlockUserData::NoMatch) {
                // Backward matching is according to the character before the cursor.
                bool undoMove = false;
                if (!cursor->atBlockEnd()) {
                    cursor->movePosition(QTextCursor::Right, QTextCursor::KeepAnchor, 1);
                    undoMove = true;
                }
                match = TextBlockUserData::matchCursorBackward(cursor);
                if (match == TextBlockUserData::Match) {
                    *moved = true;
                    *forward = false;
                } else if (undoMove) {
                    cursor->movePosition(QTextCursor::Left, QTextCursor::KeepAnchor, 1);
                }
            }
        }
    });

    handler->indentRegion.set([tew](int beginBlock, int endBlock, QChar typedChar) {
        if (!tew)
            return;

        TabSettingsData tabSettings;
        if (settings().useEditorTabSettings()) {
            // Follow the editor's (project) tab settings (QTCREATORBUG-14273).
            tabSettings = tew->textDocument()->tabSettings();
        } else {
            tabSettings.m_indentSize = settings().shiftWidth();
            tabSettings.m_tabSize = settings().tabStop();
            tabSettings.m_tabPolicy = settings().expandTab()
                    ? TabSettingsData::SpacesOnlyTabPolicy : TabSettingsData::TabsOnlyTabPolicy;
            tabSettings.m_continuationAlignBehavior =
                    tew->textDocument()->tabSettings().m_continuationAlignBehavior;
        }

        QTextDocument *doc = tew->document();
        QTextBlock startBlock = doc->findBlockByNumber(beginBlock);

        // Record line lenghts for mark adjustments
        QVector<int> lineLengths(endBlock - beginBlock + 1);
        QTextBlock block = startBlock;

        for (int i = beginBlock; i <= endBlock; ++i) {
            lineLengths[i - beginBlock] = block.text().size();
            if (typedChar.unicode() == 0 && block.text().simplified().isEmpty()) {
                // clear empty lines
                QTextCursor cursor(block);
                while (!cursor.atBlockEnd())
                    cursor.deleteChar();
            } else {
                tew->textDocument()->indenter()->indentBlock(block, typedChar, tabSettings);
            }
            block = block.next();
        }
    });

    handler->tabSettingsRequested.set([tew](int *tabSize, int *indentSize, bool *spacesForTabs) {
        if (!tew)
            return;
        const TabSettingsData ts = tew->textDocument()->tabSettings();
        *tabSize = ts.m_tabSize;
        *indentSize = ts.m_indentSize;
        *spacesForTabs = ts.m_tabPolicy == TabSettingsData::SpacesOnlyTabPolicy;
    });

    // Vim keeps these per window; Qt Creator keeps them per editor, which is
    // near enough the same thing - setDisplaySettings() is per widget. So they
    // are read and written on the editor rather than stored, the way
    // "modified" and "readonly" are: what the user sees IS the answer.
    const auto displayFlag = [](DisplaySettingsData &settings, const QString &option)
                                 -> bool * {
        if (option == "number")
            return &settings.m_displayLineNumbers;
        if (option == "wrap")
            return &settings.m_textWrapping;
        if (option == "list")
            return &settings.m_visualizeWhitespace;
        if (option == "cursorline")
            return &settings.m_highlightCurrentLine;
        if (option == "breakindent")
            return &settings.m_breakindent;
        return nullptr;
    };
    // The file's own shape. Qt Creator has no CR-only line ending and keeps no
    // note of a missing final newline, so "mac" is refused and 'endofline' is
    // not answered here at all.
    handler->documentOptionRequested.set([tew](const QString &option, QString *value) {
        if (!tew)
            return;
        TextDocument *document = tew->textDocument();
        if (option == "fileformat") {
            *value = document->lineTerminationMode()
                             == Utils::TextFileFormat::CRLFLineTerminator
                         ? QLatin1String("dos") : QLatin1String("unix");
        } else if (option == "bomb") {
            *value = document->format().hasUtf8Bom ? QLatin1String("1")
                                                   : QLatin1String("0");
        }
    });
    // How it is shown. Qt Creator has ONE margin column where Vim takes a
    // whole list, and a switch for the folding markers where Vim takes a
    // width, so the handler keeps what was asked for and hands over what can
    // be had.
    handler->marginOptionChanged.set([tew](const QString &option, int column) {
        if (!tew)
            return;
        if (option == "colorcolumn") {
            MarginSettingsData margin = tew->marginSettings();
            margin.m_showMargin = column > 0;
            if (column > 0)
                margin.m_marginColumn = column;
            tew->setMarginSettings(margin);
        } else if (option == "foldcolumn") {
            DisplaySettingsData settings = tew->displaySettings();
            settings.m_displayFoldingMarkers = column > 0;
            tew->setDisplaySettings(settings);
        }
    });
    handler->documentOptionChanged.set([tew](const QString &option,
                                            const QString &value, bool *accepted) {
        if (!tew)
            return;
        TextDocument *document = tew->textDocument();
        if (option == "fileformat") {
            if (value == "unix") {
                document->setLineTerminationMode(
                    Utils::TextFileFormat::LFLineTerminator);
            } else if (value == "dos") {
                document->setLineTerminationMode(
                    Utils::TextFileFormat::CRLFLineTerminator);
            } else {
                *accepted = false; // "mac" among them: there is no such mode
            }
        } else if (option == "bomb") {
            const bool wanted = value != "0";
            if (wanted == document->format().hasUtf8Bom)
                return;
            if (document->supportsUtf8Bom())
                document->switchUtf8Bom();
            else
                *accepted = false;
        }
    });
    handler->displayOptionRequested.set([tew, displayFlag](const QString &option,
                                                           bool *on) {
        if (!tew)
            return;
        DisplaySettingsData settings = tew->displaySettings();
        if (const bool *flag = displayFlag(settings, option))
            *on = *flag;
    });
    handler->displayOptionChanged.set([tew, displayFlag](const QString &option,
                                                          bool on) {
        if (!tew)
            return;
        DisplaySettingsData settings = tew->displaySettings();
        if (bool *flag = displayFlag(settings, option)) {
            *flag = on;
            tew->setDisplaySettings(settings);
        }
    });

    handler->checkForElectricCharacter.set([tew](bool *result, QChar c) {
        if (tew)
            *result = tew->textDocument()->indenter()->isElectricCharacter(c);
    });

    handler->requestDisableBlockSelection.set([tew] {
        if (tew)
            tew->setTextCursor(tew->textCursor());
    });

    handler->requestSetBlockSelection.set([tew](const QTextCursor &cursor, bool toEndOfLine) {
        if (tew) {
            const TabSettingsData &tabs = tew->textDocument()->tabSettings();
            MultiTextCursor mtc;
            const bool forwardSelection = cursor.anchor() < cursor.position();
            QTextBlock block = cursor.document()->findBlock(cursor.anchor());
            const QTextBlock end = forwardSelection ? cursor.block().next() : cursor.block().previous();
            const int anchor = tabs.columnAt(block.text(), cursor.anchor() - block.position());
            const int pos = tabs.columnAt(cursor.block().text(), cursor.positionInBlock());
            while (block.isValid() && block != end) {
                const int columns = tabs.columnCountForText(block.text());
                if (columns >= anchor || (!toEndOfLine && columns >= pos)) {
                    QTextCursor c(block);
                    c.setPosition(block.position() + tabs.positionAtColumn(block.text(), anchor));
                    // After '$' the selection extends to the end of each line
                    // rather than to a fixed column (QTCREATORBUG-22192).
                    const int endPosition = toEndOfLine
                            ? block.position() + block.length() - 1
                            : block.position() + tabs.positionAtColumn(block.text(), pos);
                    c.setPosition(endPosition, QTextCursor::KeepAnchor);
                    mtc.addCursor(c);
                }
                block = forwardSelection ? block.next() : block.previous();
            }
            tew->setMultiTextCursor(mtc);
        }
    });

    handler->requestBlockSelection.set([tew](QTextCursor *cursor) {
        if (tew && cursor) {
            MultiTextCursor mtc = tew->multiTextCursor();
            *cursor = mtc.cursors().first();
            cursor->setPosition(mtc.mainCursor().position(), QTextCursor::KeepAnchor);
        }
    });

    handler->requestHasBlockSelection.set([tew](bool *on) {
        if (tew && on)
            *on = tew->multiTextCursor().hasMultipleCursors();
    });

    handler->simpleCompletionRequested.set([handler](const QString &needle, bool forward) {
        theFakeVimCompletionAssistProvider.setActive(needle, forward, handler);
    });

    handler->windowCommandRequested.set([this, handler](const QString &map, int count) {
        // normalize mapping
        const QString key = map.toUpper();

        if (key == "C" || key == "<C-C>")
            triggerAction(Core::Constants::REMOVE_CURRENT_SPLIT);
        else if (key == "N" || key == "<C-N>")
            triggerAction(Core::Constants::GOTO_NEXT_SPLIT);
        else if (key == "O" || key == "<C-O>")
            keepOnlyWindow();
        else if (key == "P" || key == "<C-P>")
            triggerAction(Core::Constants::GOTO_PREV_SPLIT);
        else if (key == "S" || key == "<C-S>") {
            triggerAction(Core::Constants::SPLIT);
            updateAllHightLights();
        } else if (key == "V" || key == "<C-V>") {
            triggerAction(Core::Constants::SPLIT_SIDE_BY_SIDE);
            updateAllHightLights();
        } else if (key == "W" || key == "<C-W>")
            triggerAction(Core::Constants::GOTO_NEXT_SPLIT);
        else if (key.contains("RIGHT") || key == "L" || key == "<S-L>" || key == "<C-L>")
            moveSomewhere(handler, &moveRightWeight, key == "<S-L>" ? -1 : count);
        else if (key.contains("LEFT")  || key == "H" || key == "<S-H>" || key == "<C-H>")
            moveSomewhere(handler, &moveLeftWeight, key == "<S-H>" ? -1 : count);
        else if (key.contains("UP")    || key == "K" || key == "<S-K>" || key == "<C-K>")
            moveSomewhere(handler, &moveUpWeight, key == "<S-K>" ? -1 : count);
        else if (key.contains("DOWN")  || key == "J" || key == "<S-J>" || key == "<C-J>")
            moveSomewhere(handler, &moveDownWeight, key == "<S-J>" ? -1 : count);
        else
            qDebug() << "UNKNOWN WINDOW COMMAND: <C-W>" << map;
    });

    // input() and its relatives block in Vim until the user answers. A modal
    // dialog is what blocks here, so the engine needs nothing suspended.
    handler->inputRequested.set(
        [](const QString &prompt, const QString &preset, bool secret,
           QString *answer, bool *cancelled) {
            bool accepted = false;
            const QString text = QInputDialog::getText(
                ICore::dialogParent(), Tr::tr("FakeVim"), prompt,
                secret ? QLineEdit::Password : QLineEdit::Normal, preset, &accepted);
            *cancelled = !accepted;
            if (accepted)
                *answer = text;
        });

    handler->inputListRequested.set([](const QStringList &lines, int *chosen) {
        // The first entry is the prompt and the rest are the choices, and the
        // answer is the number of one of them - which is its place in the
        // list, the numbering a Vim script writes into the entries itself.
        if (lines.size() < 2)
            return;
        bool accepted = false;
        const QString picked = QInputDialog::getItem(
            ICore::dialogParent(), Tr::tr("FakeVim"), lines.first(),
            lines.mid(1), 0, false, &accepted);
        if (accepted)
            *chosen = lines.mid(1).indexOf(picked) + 1;
    });

    // confirm() asks and waits, as input() does, and the same modal dialog is
    // what waits. The "&" Vim marks an accelerator with is the one QMessageBox
    // uses too, so the button text goes straight through.
    handler->confirmRequested.set(
        [](const QString &text, const QStringList &choices, int preferred, int *chosen) {
            QMessageBox box(QMessageBox::Question, Tr::tr("FakeVim"), text,
                            QMessageBox::NoButton, ICore::dialogParent());
            QList<QPushButton *> buttons;
            for (const QString &choice : choices)
                buttons.append(box.addButton(choice, QMessageBox::AcceptRole));
            if (preferred >= 1 && preferred <= buttons.size())
                box.setDefaultButton(buttons.at(preferred - 1));
            box.exec();
            const int taken = buttons.indexOf(qobject_cast<QPushButton *>(box.clickedButton()));
            // Vim counts the buttons from one, and answers 0 for a dialog
            // that was dismissed rather than answered.
            *chosen = taken < 0 ? 0 : taken + 1;
        });

    // ":winpos" is about the application window, which is the one window
    // Qt Creator really has a position for.
    handler->windowPositionRequested.set([](int *x, int *y) {
        if (QMainWindow *window = ICore::mainWindow()) {
            *x = window->pos().x();
            *y = window->pos().y();
        }
    });

    handler->windowMoveRequested.set([](int x, int y) {
        if (QMainWindow *window = ICore::mainWindow())
            window->move(x, y);
    });

    handler->findRequested.set([](bool reverse) {
        Find::setUseFakeVim(true);
        Find::openFindToolBar(reverse ? Find::FindBackwardDirection
                                      : Find::FindForwardDirection);
    });

    handler->findNextRequested.set([](bool reverse) {
        triggerAction(reverse ? Id(Core::Constants::FIND_PREVIOUS) : Id(Core::Constants::FIND_NEXT));
    });

    handler->findHideRequested.set([] { Find::hideFindToolBar(); });

    handler->foldToggle.set([this, handler](int depth) {
        QTextBlock block = handler->textCursor().block();
        fold(handler, depth, !TextBlockUserData::isFolded(block));
    });

    handler->foldAll.set([handler](bool fold) {
        QTextDocument *document = handler->textCursor().document();
        auto documentLayout = qobject_cast<TextDocumentLayout*>(document->documentLayout());
        QTC_ASSERT(documentLayout, return);

        QTextBlock block = document->firstBlock();
        while (block.isValid()) {
            TextBlockUserData::doFoldOrUnfold(block, !fold);
            block = block.next();
        }

        documentLayout->requestUpdate();
        documentLayout->emitDocumentSizeChanged();
    });

    // 'foldlevel': Qt Creator keeps the depth of a fold as its folding indent,
    // so the level maps onto it directly - a block whose indent reaches the
    // level is closed, and anything shallower is opened.
    handler->foldLevelRequested.set([handler](int level) {
        QTextDocument *doc = handler->textCursor().document();
        QTC_ASSERT(doc, return);
        auto documentLayout = qobject_cast<TextDocumentLayout *>(doc->documentLayout());
        QTC_ASSERT(documentLayout, return);
        for (QTextBlock block = doc->firstBlock(); block.isValid();
             block = block.next()) {
            if (!TextBlockUserData::canFold(block))
                continue;
            const bool open = TextBlockUserData::foldingIndent(block) < level;
            if (TextBlockUserData::isFolded(block) == open)
                TextBlockUserData::doFoldOrUnfold(block, open);
        }
        documentLayout->requestUpdate();
        documentLayout->emitDocumentSizeChanged();
    });

    // What the fold queries answer. A line hidden by a closed fold is not
    // VISIBLE, which is how the fold holding it is found: walk back to the
    // block that folded it, and forward to the last one it hides.
    handler->foldStateRequested.set(
        [handler](int line, int *closedStart, int *closedEnd, int *level) {
            QTextDocument *doc = handler->textCursor().document();
            QTC_ASSERT(doc, return);
            const QTextBlock block = doc->findBlockByNumber(line - 1);
            if (!block.isValid())
                return;
            *level = TextBlockUserData::foldingIndent(block);
            // The FIRST line of a closed fold is still shown, and Vim counts it
            // as part of the fold - measured: for a fold over 2..4, foldclosed()
            // answers 2 for line 2 as well as for the hidden 3 and 4. So a
            // visible block that has folded what follows it is a fold start,
            // and a hidden one belongs to the fold above it.
            QTextBlock start = block;
            if (block.isVisible()) {
                if (!TextBlockUserData::canFold(block)
                    || !TextBlockUserData::isFolded(block)) {
                    return;
                }
            } else {
                while (start.isValid() && !start.isVisible())
                    start = start.previous();
                if (!start.isValid())
                    return;
            }
            QTextBlock end = block;
            while (end.next().isValid() && !end.next().isVisible())
                end = end.next();
            *closedStart = start.blockNumber() + 1;
            *closedEnd = end.blockNumber() + 1;
        });

    handler->foldRangeRequested.set([handler](int firstLine, int lastLine, bool close) {
        QTextDocument *doc = handler->textCursor().document();
        QTC_ASSERT(doc, return);
        auto documentLayout = qobject_cast<TextDocumentLayout *>(doc->documentLayout());
        QTC_ASSERT(documentLayout, return);
        for (int line = firstLine; line <= lastLine; ++line) {
            const QTextBlock block = doc->findBlockByNumber(line - 1);
            if (block.isValid() && TextBlockUserData::canFold(block)
                && TextBlockUserData::isFolded(block) != close) {
                TextBlockUserData::doFoldOrUnfold(block, !close);
            }
        }
        documentLayout->requestUpdate();
        documentLayout->emitDocumentSizeChanged();
    });

    handler->foldToggleAll.set([handler] {
        const QTextDocument *document = handler->textCursor().document();
        bool anyFolded = false;
        for (QTextBlock block = document->firstBlock(); block.isValid(); block = block.next()) {
            if (TextBlockUserData::isFolded(block)) {
                anyFolded = true;
                break;
            }
        }
        handler->foldAll(!anyFolded); // open all if any fold is closed, else close all
    });

    handler->fold.set([this, handler](int depth, bool dofold) { fold(handler, depth, dofold); });

    handler->foldGoTo.set([handler](int count, bool current) {
        QTextCursor tc = handler->textCursor();
        QTextBlock block = tc.block();

        int pos = -1;
        if (count > 0) {
            int repeat = count;
            block = block.next();
            QTextBlock prevBlock = block;
            int indent = TextBlockUserData::foldingIndent(block);
            block = block.next();
            while (block.isValid()) {
                int newIndent = TextBlockUserData::foldingIndent(block);
                if (current ? indent > newIndent : indent < newIndent) {
                    if (prevBlock.isVisible()) {
                        pos = prevBlock.position();
                        if (--repeat <= 0)
                            break;
                    } else if (current) {
                        indent = newIndent;
                    }
                }
                if (!current)
                    indent = newIndent;
                prevBlock = block;
                block = block.next();
            }
        } else if (count < 0) {
            int repeat = -count;
            int indent = TextBlockUserData::foldingIndent(block);
            block = block.previous();
            while (block.isValid()) {
                int newIndent = TextBlockUserData::foldingIndent(block);
                if (current ? indent > newIndent : indent < newIndent) {
                    while (block.isValid() && !block.isVisible())
                        block = block.previous();
                    pos = block.position();
                    if (--repeat <= 0)
                        break;
                }
                if (!current)
                    indent = newIndent;
                block = block.previous();
            }
        }

        if (pos != -1) {
            tc.setPosition(pos, QTextCursor::KeepAnchor);
            handler->setTextCursor(tc);
        }
    });

    handler->requestJumpToGlobalMark.set(
        [this](QChar mark, bool backTickMode, const QString &fileName) {
            if (IEditor *iedit = EditorManager::openEditor(FilePath::fromString(fileName))) {
                if (FakeVimHandler *handler = m_editorToHandler.value(iedit, {}).handler)
                    handler->jumpToLocalMark(mark, backTickMode);
            }
        });

    handler->fileOpenRequested.set([](const QString &fileName, int line) {
        const FilePath path = FilePath::fromString(fileName);
        if (line > 0)
            EditorManager::openEditorAt(Link(path, line));
        else
            EditorManager::openEditor(path);
    });

    handler->handleExCommandRequested.set([this, handler](bool *handled, const ExCommand &cmd) {
        handleExCommand(handler, handled, cmd);
    });

    handler->tabNextRequested.set([] { triggerAction(Core::Constants::GOTONEXTINHISTORY); });

    handler->tabPreviousRequested.set([] { triggerAction(Core::Constants::GOTOPREVINHISTORY); });

    handler->tagJumpRequested.set([this, handler](const QString &tag) {
        tagJump(handler, tag);
    });
    // What ":tags" lists. The line each level was followed from is shown with
    // its text, which is read from the document where it is still open.
    handler->recentFilesRequested.set([](QStringList *files) {
        for (const DocumentManager::RecentFile &file : DocumentManager::recentFiles())
            files->append(file.first.toUserOutput());
    });
    handler->tagStackContents.set(
        [this](QList<FakeVimHandler::TagStackEntry> *entries, int *index) {
            for (const TagJump &jump : std::as_const(m_tagStack)) {
                FakeVimHandler::TagStackEntry entry;
                entry.tag = jump.tag;
                entry.fromLine = jump.from.target.line;
                if (TextDocument *document = TextDocument::textDocumentForFilePath(
                        jump.from.targetFilePath)) {
                    entry.fromText = document->document()
                                         ->findBlockByNumber(entry.fromLine - 1)
                                         .text();
                }
                entries->append(entry);
            }
            *index = m_tagIndex;
        });

    handler->tagStackRequested.set([this, handler](int distance) {
        tagStackMove(handler, distance);
    });

    handler->syntaxNamesRequested.set([tew](int line, int column, QStringList *names) {
        // Qt Creator keeps no name for what the highlighter produced, only
        // colors, so the only thing that can be answered is what the language
        // of the document knows how to be asked. That covers what a script
        // commenting or reformatting code looks for.
        if (!tew || !tew->autoCompleter())
            return;
        QTextCursor cursor(tew->document());
        const QTextBlock block = tew->document()->findBlockByNumber(line - 1);
        if (!block.isValid())
            return;
        cursor.setPosition(block.position() + qBound(0, column - 1,
                                                     qMax(0, block.length() - 1)));
        if (tew->autoCompleter()->isInComment(cursor))
            names->append("Comment");
        if (tew->autoCompleter()->isInString(cursor))
            names->append("String");
    });

    handler->contextHelpRequested.set([] {
        // Help::Constants::CONTEXT_HELP, by id to avoid a Help-plugin dependency.
        triggerAction("Help.Context");
    });

    handler->alternateFileRequested.set([this] {
        if (m_alternateFileEditor)
            EditorManager::activateEditor(m_alternateFileEditor);
    });

    handler->navigateHistoryRequested.set([](int distance) {
        for (int i = 0, n = qAbs(distance); i < n; ++i) {
            if (distance < 0)
                EditorManager::goBackInNavigationHistory();
            else
                EditorManager::goForwardInNavigationHistory();
        }
    });

    handler->completionRequested.set([tew] {
        if (tew)
            tew->invokeAssist(Completion, &theFakeVimCompletionAssistProvider);
    });

    handler->processOutput.set([](const QString &command, const QString &input, QString *output) {
        Process proc;
        proc.setCommand(Utils::CommandLine::fromUserInput(command));
        proc.setWriteData(input.toLocal8Bit());
        proc.start();

        // FIXME: Process should be interruptable by user.
        //        Solution is to create a QObject for each process and emit finished state.
        proc.waitForFinished();
        *output = proc.cleanedStdOut();
    });

    handler->setCurrentFileName(editor->document()->filePath().toUrlishString());
    handler->installEventFilter();

    // Nothing of the engine is asked anything while FakeVim is off - an editor
    // keeps its handler either way, so that switching FakeVim on later reaches
    // every buffer that is already open.
    if (settings().useFakeVim()) {
        enterBufferOnce(editor);

        // pop up the bar
        resetCommandBuffer();
        handler->setupWidget();

        if (settings().relativeNumber())
            createRelativeNumberWidget(editor);
    }
}

// An event that is about the WINDOW LAYOUT rather than about one buffer, so
// there is no buffer of its own to fire it through. The autocommand table is
// one static GlobalData shared by every handler, so any one of them reaches
// every registration exactly once - going through all of them would announce
// a single split once per buffer instead.
void FakeVimPlugin::triggerAutocmdInAnyBuffer(const QString &event, const QString &target)
{
    if (!settings().useFakeVim())
        return;
    FakeVimHandler *handler = nullptr;
    if (IEditor *current = EditorManager::currentEditor())
        handler = m_editorToHandler.value(current, {}).handler;
    if (!handler && !m_editorToHandler.isEmpty())
        handler = m_editorToHandler.constBegin()->handler;
    if (handler)
        handler->triggerAutocmd(event, target);
}

// What a buffer is given when FakeVim takes it over: the file type, whatever its
// modelines say, and the autocommands Vim fires on reading a file and entering it.
// Done when an editor is opened, and for every editor already open when FakeVim is
// switched on.
void FakeVimPlugin::enterBuffer(IEditor *editor, FakeVimHandler *handler)
{
    // Entering the window comes before entering the buffer, which comes before
    // the buffer is actually read; mirrors the WinLeave/BufLeave pair on the way out.
    handler->triggerAutocmd("WinEnter");
    handler->triggerAutocmd("BufEnter");

    // A buffer coming into existence, then joining the buffer list. Both
    // name the file, and both come before it is read.
    handler->triggerAutocmd("BufNew");
    handler->triggerAutocmd("BufAdd");


    // Vim detects the file type from the buffer read autocommands, so fire
    // those first and only fill in what they left unset. That way a rule in a
    // vimrc wins over what Qt Creator guessed.
    // A file that is there is read, which is announced before and after; one
    // that is not is a new file, which only announces itself once and gets no
    // BufReadPre at all (measured).
    const bool fileExists = editor->document()->filePath().exists();
    if (fileExists)
        handler->triggerAutocmd("BufReadPre");
    handler->triggerAutocmd(fileExists ? QLatin1String("BufReadPost")
                                       : QLatin1String("BufNewFile"));
    const QString fileType = vimFileType(editor->document());
    if (!fileType.isEmpty()) {
        // FALLBACK: this is what the MIME database guessed, so a ":setf" from a
        // script may still replace it.
        handler->handleCommand("if &ft == '' | setf FALLBACK " + fileType + " | endif");
    }
    // Highlighting is always on here, and scripts check this before asking what
    // is under the cursor.
    if (TextEditorWidget::fromEditor(editor))
        handler->handleCommand("let g:syntax_on = 1");
    // Last, so that what the file says about itself wins over both.
    handler->processModelines();
    handler->triggerAutocmd("BufWinEnter");

    // Vim fires VimEnter once, after the startup is complete. There is no
    // comparable moment here, so use the first buffer FakeVim gets to work on.
    if (!m_didVimEnter) {
        m_didVimEnter = true;
        handler->triggerAutocmd("VimEnter");
    }
}

// A buffer is given to FakeVim the first time FakeVim has it: as an editor is
// opened, or as it becomes the current one. Entering it runs autocommands and the
// modelines of the file, so doing it for every open editor at once would set the
// options from whichever buffer came last in the hash.
bool FakeVimPlugin::enterBufferOnce(IEditor *editor)
{
    if (!editor || !settings().useFakeVim())
        return false;
    auto it = m_editorToHandler.find(editor);
    if (it == m_editorToHandler.end() || it->entered || !it->handler)
        return false;
    it->entered = true;
    enterBuffer(editor, it->handler);
    return true;
}

void FakeVimPlugin::editorAboutToClose(IEditor *editor)
{
    //qDebug() << "CLOSING: " << editor << editor->widget();
    // The buffer leaving its window, then being unloaded, then dropped from
    // the buffer list - Vim's order on ":bdelete", all three naming the file.
    // Vim also has BufWipeout after these for ":bwipeout", which is a
    // distinction Qt Creator does not draw: closing an editor here is the
    // only way a buffer goes, so only the ":bdelete" three are fired and
    // BufWipeout is left alone rather than guessing which close is which.
    if (FakeVimHandler *handler = m_editorToHandler.value(editor, {}).handler) {
        handler->triggerAutocmd("BufWinLeave");
        handler->triggerAutocmd("BufUnload");
        handler->triggerAutocmd("BufDelete");
    }
    m_editorToHandler.remove(editor);
    if (m_alternateFileEditor == editor)
        m_alternateFileEditor.clear();
}

void FakeVimPlugin::currentEditorAboutToChange(IEditor *editor)
{
    if (!settings().useFakeVim()) {
        if (editor)
            m_alternateFileEditor = editor;
        return;
    }
    if (FakeVimHandler *handler = m_editorToHandler.value(editor, {}).handler) {
        handler->enterCommandMode();
        // Vim leaves the buffer before the window, and enters them the other way
        // round, so this pairs with the WinEnter/BufEnter on the way in.
        handler->triggerAutocmd("BufLeave");
        handler->triggerAutocmd("WinLeave");
        // The buffer is out of its window but still loaded, which is what
        // Vim calls hidden - every editor Qt Creator has open is loaded, so
        // switching away from one is exactly that. Vim fires this only for a
        // buffer that STAYS, never for one being closed, and that falls out
        // here rather than needing a flag: editorAboutToClose runs BEFORE
        // this on a close (measured) and has already taken the editor out of
        // m_editorToHandler, so the lookup above finds no handler and none of
        // this runs. Vim's order has BufLeave before BufHidden, which holds.
        handler->triggerAutocmd("BufHidden");
    }
    if (editor)
        m_alternateFileEditor = editor;
}

void FakeVimPlugin::allDocumentsRenamed(const FilePath &oldPath, const FilePath &newPath)
{
    renameFileNameInEditors(oldPath, newPath);
    FakeVimHandler::updateGlobalMarksFilenames(oldPath.toUrlishString(), newPath.toUrlishString());
}

void FakeVimPlugin::documentRenamed(
        IDocument *, const FilePath &oldPath, const FilePath &newPath)
{
    renameFileNameInEditors(oldPath, newPath);
}

void FakeVimPlugin::renameFileNameInEditors(const FilePath &oldPath, const FilePath &newPath)
{
    for (const HandlerAndData &handlerAndData : std::as_const(m_editorToHandler)) {
        if (handlerAndData.handler->currentFileName() == oldPath.toUrlishString())
            handlerAndData.handler->setCurrentFileName(newPath.toUrlishString());
    }
}

void FakeVimPlugin::setUseFakeVim(bool on)
{
    //qDebug() << "SET USE FAKEVIM" << on;
    Find::setUseFakeVim(on);
    setUseFakeVimInternal(on);
    setShowRelativeLineNumbers(settings().relativeNumber());
    setCursorBlinking(settings().blinkingCursor());
    updateEditorCommandLinePlacement();
}

void FakeVimPlugin::setUseFakeVimInternal(bool on)
{
    if (on) {
        //ICore *core = ICore::instance();
        //core->updateAdditionalContexts(Context(FAKEVIM_CONTEXT),
        // Context());
        const QList<IEditor *> editors = m_editorToHandler.keys();
        for (IEditor *editor : editors) {
            const HandlerAndData handlerAndData = m_editorToHandler.value(editor, {});
            if (handlerAndData.handler)
                handlerAndData.handler->setupWidget();
        }
        // The buffer in front is the one whose modelines have a say now; the rest
        // are entered as they are visited.
        IEditor *current = EditorManager::currentEditor();
        if (!enterBufferOnce(current)) {
            if (FakeVimHandler *handler = m_editorToHandler.value(current, {}).handler)
                handler->triggerAutocmd("WinEnter");
        }
    } else {
        //ICore *core = ICore::instance();
        //core->updateAdditionalContexts(Context(),
        // Context(FAKEVIM_CONTEXT));
        resetCommandBuffer();
        for (auto it = m_editorToHandler.begin(); it != m_editorToHandler.end(); ++it) {
            if (auto textDocument = qobject_cast<const TextDocument *>(it.key()->document())) {
                HandlerAndData &handlerAndData = it.value();
                handlerAndData.handler->restoreWidget(textDocument->tabSettings().m_tabSize);
                handlerAndData.suggestionBlocker.reset();
            }
            // Nothing is entered while FakeVim is off, so the next "on" re-enters
            // every buffer from scratch instead of enterBufferOnce() staying a no-op.
            it->entered = false;
        }
    }
}

void FakeVimPlugin::setShowRelativeLineNumbers(bool on)
{
    if (on && settings().useFakeVim()) {
        for (auto it = m_editorToHandler.constBegin(); it != m_editorToHandler.constEnd(); ++it)
            createRelativeNumberWidget(it.key());
    }
}

void FakeVimPlugin::setCursorBlinking(bool on)
{
    if (m_savedCursorFlashTime == 0)
        m_savedCursorFlashTime = QGuiApplication::styleHints()->cursorFlashTime();

    const bool blink = on || !settings().useFakeVim();
    int flashTime = m_savedCursorFlashTime;
    if (on && settings().useFakeVim() && settings().cursorFlashTime() > 0)
        flashTime = settings().cursorFlashTime();
    QGuiApplication::styleHints()->setCursorFlashTime(blink ? flashTime : 0);
}

// How many files ":next" and its kin should move by. It has to be read off
// hasRange: ExCommand::count is the line an address named, counted from zero,
// and it holds the CURRENT line where none was typed - so using it directly
// makes ":next" on line 3 move three files instead of one.
static int howManyFiles(const ExCommand &cmd)
{
    return cmd.hasRange ? cmd.count + 1 : 1;
}

void FakeVimPlugin::handleExCommand(FakeVimHandler *handler, bool *handled, const ExCommand &cmd)
{
    QTC_ASSERT(handler, return);
    using namespace Core;
    //qDebug() << "PLUGIN HANDLE: " << cmd.cmd << cmd.count;

    *handled = false;

    // Focus editor first so actions can be executed in correct context.
    QWidget *editor = handler->widget();
    if (editor)
        editor->setFocus();

    auto editorFromHandler = [this, handler]() -> Core::IEditor * {
        auto itEditor = std::find_if(m_editorToHandler.cbegin(),
                                     m_editorToHandler.cend(),
                                     [handler](const HandlerAndData &handlerAndData) {
                                         return handlerAndData.handler == handler;
                                     });
        if (itEditor != m_editorToHandler.cend())
            return itEditor.key();
        return nullptr;
    };

    *handled = true;
    if ((cmd.matches("w", "write") || cmd.cmd == "wq") && cmd.args.isEmpty()) {
        // :w[rite]
        bool saved = false;
        IEditor *editor = editorFromHandler();
        const QString fileName = handler->currentFileName();
        if (editor && editor->document()->filePath().toUrlishString() == fileName) {
            // An autocommand for the "Cmd" event saves the buffer itself, so
            // Qt Creator must not, and the "Pre"/"Post" pair does not fire.
            // Vim would also count the buffer as unmodified afterwards; that
            // is left alone here, because a document marked clean without
            // having been written closes without a word and loses the edits.
            if (handler->triggerAutocmd("BufWriteCmd") > 0)
                return;
            handler->triggerAutocmd("BufWritePre");
            saved = EditorManager::saveDocument(editor->document());
            if (saved) {
                handler->triggerAutocmd("BufWritePost");
                QFile file3(fileName);
                if (file3.open(QIODevice::ReadOnly)) {
                    const QByteArray ba = file3.readAll();
                    handler->showMessage(MessageInfo, Tr::tr("\"%1\" %2 %3L, %4C written")
                        .arg(fileName).arg(' ').arg(ba.count('\n')).arg(ba.size()));
                    if (cmd.cmd == "wq")
                        emit delayedQuitRequested(cmd.hasBang, editor);
                }
            }
        }

        if (!saved)
            handler->showMessage(MessageError, Tr::tr("File not saved"));
    } else if (cmd.matches("wa", "wall") || cmd.matches("wqa", "wqall")) {
        // :wa[ll] :wqa[ll]
        triggerAction(Core::Constants::SAVEALL);
        const QList<IDocument *> failed = DocumentManager::modifiedDocuments();
        if (failed.isEmpty())
            handler->showMessage(MessageInfo, Tr::tr("Saving succeeded"));
        else
            handler->showMessage(MessageError, Tr::tr("%n files not saved", nullptr, failed.size()));
        if (cmd.matches("wqa", "wqall"))
            emit delayedQuitAllRequested(cmd.hasBang);
    } else if (cmd.matches("e", "edit") || cmd.matches("ene", "enew")
               || cmd.matches("vie", "view") || cmd.matches("vi", "visual")) {
        // :e[dit] [file] - open one, or read the current one again. Measured:
        // a bare ":edit" on a changed buffer answers
        // "E37: No write since last change (add ! to override)" and needs the
        // bang to throw the changes away; ":edit #" goes to the alternate
        // file; a file that is not there is opened as a new one. ":view" is
        // ":edit" with 'readonly' set, and ":visual" is ":edit" by another
        // name.
        const QString target = cmd.args.trimmed();
        IEditor *editor = editorFromHandler();
        if (cmd.matches("ene", "enew")) {
            IEditor *fresh = EditorManager::openEditorWithContents(Id());
            if (fresh)
                EditorManager::activateEditor(fresh);
            return;
        }
        if (target == "#") {
            handler->handleInput("<C-^>");
            return;
        }
        if (target.isEmpty()) {
            if (!editor)
                return;
            IDocument *document = editor->document();
            if (document->isModified() && !cmd.hasBang) {
                handler->showMessage(MessageError,
                    Tr::tr("E37: No write since last change (add ! to override)"));
                return;
            }
            const Utils::Result<> reloaded =
                document->reload(IDocument::FlagReload, IDocument::TypeContents);
            if (!reloaded)
                handler->showMessage(MessageError, reloaded.error());
            return;
        }
        const FilePath path = FilePath::fromUserInput(target);
        if (!EditorManager::openEditor(path)) {
            handler->showMessage(MessageError,
                                 Tr::tr("E484: Can't open file %1").arg(target));
            return;
        }
        if (cmd.matches("vie", "view")) {
            // Vim opens it with 'readonly' set, which is this engine's own
            // flag on the handler the new editor gets.
            if (IEditor *opened = EditorManager::currentEditor()) {
                const auto it = m_editorToHandler.constFind(opened);
                if (it != m_editorToHandler.constEnd() && it->handler)
                    it->handler->handleInput(":set readonly<CR>");
            }
        }
    } else if (cmd.matches("sav", "saveas")) {
        if (cmd.args.trimmed().isEmpty()) {
            triggerAction(Core::Constants::SAVEAS);
            return;
        }
        IEditor *editor = editorFromHandler();
        if (!editor)
            return;
        const FilePath target = FilePath::fromUserInput(cmd.args.trimmed());
        if (target.exists() && !cmd.hasBang) {
            handler->showMessage(MessageError,
                Tr::tr("E13: File exists (add ! to override)"));
            return;
        }
        if (const Utils::Result<> saved = editor->document()->save(target); !saved)
            handler->showMessage(MessageError, saved.error());
    } else if (cmd.matches("h", "help") || cmd.matches("exu", "exusage")
               || cmd.matches("viu", "viusage")) {
        // By action id, to keep the Help plugin out of the dependencies -
        // contextHelpRequested does the same.
        triggerAction("Help.Home");
    } else if (cmd.matches("helpc", "helpclose")) {
        ModeManager::activateMode(Core::Constants::MODE_EDIT);
    } else if (cmd.matches("checkt", "checktime")) {
        IEditor *editor = editorFromHandler();
        if (!editor)
            return;
        IDocument *document = editor->document();
        if (document->isModified() || !document->filePath().exists())
            return;
        if (const Utils::Result<> reloaded
            = document->reload(IDocument::FlagReload, IDocument::TypeContents);
            !reloaded) {
            handler->showMessage(MessageError, reloaded.error());
        }
    } else if (cmd.matches("fin", "find")) {
        const QString name = cmd.args.trimmed();
        IEditor *editor = editorFromHandler();
        const FilePath here = editor ? editor->document()->filePath().parentDir() : FilePath{};
        const FilePath candidate = here.isEmpty() ? FilePath::fromUserInput(name)
                                                  : here.resolvePath(name);
        if (!candidate.exists() || !EditorManager::openEditor(candidate)) {
            handler->showMessage(MessageError,
                Tr::tr("E345: Can't find file \"%1\" in path").arg(name));
        }
    } else if (cmd.matches("bd", "bdelete")) {
        // :bd[elete]
        emit delayedBufferDeleteRequested(cmd.hasBang, editorFromHandler());
    } else if (cmd.matches("q", "quit")) {
        // :q[uit]
        emit delayedQuitRequested(cmd.hasBang, editorFromHandler());
    } else if (cmd.matches("qa", "qall")) {
        // :qa[ll]
        emit delayedQuitAllRequested(cmd.hasBang);
    } else if (cmd.matches("sp", "split")) {
        // :sp[lit]
        EditorManager::split();
        updateAllHightLights();
    } else if (cmd.matches("vs", "vsplit")) {
        // :vs[plit]
        EditorManager::splitSideBySide();
        updateAllHightLights();
    } else if (cmd.matches("ve", "version")) {
        // Vim prints pages of build flags here. Claiming a Vim version would
        // be a fiction; what this really is, is Qt Creator, so that is what it
        // says.
        handler->showMessage(MessageInfo, ICore::versionString());
    } else if (cmd.matches("mak", "make")) {
        // :mak[e][!] [arguments]
        // The pattern and "<amatch>" are the bare command name, with neither
        // the modifiers nor the arguments part of it (measured). Building here
        // is asynchronous, so the "Post" event marks the command having been
        // started rather than results being in - there is no quickfix list
        // here for it to have filled either way.
        handler->triggerAutocmd("QuickFixCmdPre", "make");
        triggerAction(ProjectExplorer::Constants::BUILD);
        handler->triggerAutocmd("QuickFixCmdPost", "make");
    } else if (cmd.matches("se", "set")) {
        if (cmd.args.isEmpty()) {
            // :se[t]
            ICore::showSettings(SETTINGS_ID);
        } else if (cmd.args == "ic" || cmd.args == "ignorecase") {
            // :set nc
            setActionChecked(Core::Constants::CASE_SENSITIVE, false);
        } else if (cmd.args == "noic" || cmd.args == "noignorecase") {
            // :set noic
            setActionChecked(Core::Constants::CASE_SENSITIVE, true);
        } else if (cmd.args == "breakindent" || cmd.args == "bri") {
            // :set breakindent
            TextEditor::displaySettings().breakindent.setValue(true);
        } else if (cmd.args == "nobreakindent" || cmd.args == "nobri") {
            // :set nobreakindent
            TextEditor::displaySettings().breakindent.setValue(false);
        }
        *handled = false; // Let the handler see it as well.
    } else if (cmd.matches("wn", "wnext") || cmd.matches("wN", "wNext")
               || cmd.matches("wp", "wprevious")) {
        // Write, then walk the argument list. Measured: the write happens even
        // where the walk cannot, so a single-entry list still saves and then
        // answers E163.
        IEditor *editor = editorFromHandler();
        if (editor && !EditorManager::saveDocument(editor->document())) {
            handler->showMessage(MessageError, Tr::tr("File not saved"));
            return;
        }
        const int distance = cmd.matches("wn", "wnext") ? howManyFiles(cmd)
                                                        : -howManyFiles(cmd);
        if (!handler->walkArgList(distance)) {
            handler->showMessage(MessageError,
                                 Tr::tr("E163: There is only one file to edit"));
        }
    } else if (cmd.matches("n", "next")) {
        // :n[ext] - the argument list where ":args" has set one, and otherwise
        // the documents Qt Creator has open, which is what this did before
        // there was a list to walk at all.
        if (!handler->walkArgList(howManyFiles(cmd)))
            switchToFile(currentFile() + howManyFiles(cmd));
    } else if (cmd.matches("prev", "previous") || cmd.matches("N", "Next")) {
        // :prev[ious], :N[ext]
        if (!handler->walkArgList(-howManyFiles(cmd)))
            switchToFile(currentFile() - howManyFiles(cmd));
    } else if (cmd.matches("bn", "bnext")) {
        // :bn[ext]
        switchToFile(currentFile() + howManyFiles(cmd));
    } else if (cmd.matches("bp", "bprevious") || cmd.matches("bN", "bNext")) {
        // :bp[revious], :bN[ext]
        switchToFile(currentFile() - howManyFiles(cmd));
    } else if (cmd.matches("on", "only")) {
        // :on[ly]
        keepOnlyWindow();
    } else if (cmd.cmd == "AS") {
        triggerAction(Core::Constants::SPLIT);
        triggerAction(CppEditor::Constants::SWITCH_HEADER_SOURCE);
    } else if (cmd.cmd == "AV") {
        triggerAction(Core::Constants::SPLIT_SIDE_BY_SIDE);
        triggerAction(CppEditor::Constants::SWITCH_HEADER_SOURCE);
    } else {
        // Check whether one of the configure commands matches.
        const auto end = m_exCommandMap.constEnd();
        for (auto it = m_exCommandMap.constBegin(); it != end; ++it) {
            const QString &id = it.key();
            const QString re = it.value();
            if (!re.isEmpty() && QRegularExpression(re).match(cmd.cmd).hasMatch()) {
                triggerAction(Id::fromString(id));
                return;
            }
        }
        *handled = false;
    }
}

static Link currentEditorLink()
{
    if (BaseTextEditor *editor = BaseTextEditor::currentTextEditor()) {
        // currentColumn() is 1-based; gotoLine()/Link expect a 0-based column.
        return Link(editor->document()->filePath(),
                    editor->currentLine(), editor->currentColumn() - 1);
    }
    return {};
}

void FakeVimPlugin::tagJump(FakeVimHandler *handler, const QString &tag)
{
    Q_UNUSED(handler)
    // A new tag jump discards any entries we had moved back past, records the
    // current location and the symbol being followed, and follows it.
    while (m_tagStack.size() > m_tagIndex)
        m_tagStack.removeLast();
    m_tagStack.append({currentEditorLink(), tag});
    m_tagIndex = m_tagStack.size();
    triggerAction(TextEditor::Constants::FOLLOW_SYMBOL_UNDER_CURSOR);
}

void FakeVimPlugin::tagStackMove(FakeVimHandler *handler, int distance)
{
    if (distance < 0) {
        // CTRL-T / :pop - go back towards where the tag jumps started.
        if (m_tagIndex == 0) {
            handler->showMessage(MessageError, Tr::tr("at bottom of tag stack"));
            return;
        }
        m_tagIndex = qMax(0, m_tagIndex + distance);
        EditorManager::openEditorAt(m_tagStack.at(m_tagIndex).from);
    } else if (distance > 0) {
        // bare :tag - re-follow towards the newest tag jump.
        if (m_tagIndex >= m_tagStack.size()) {
            handler->showMessage(MessageError, Tr::tr("at top of tag stack"));
            return;
        }
        m_tagIndex = qMin(m_tagStack.size(), m_tagIndex + distance);
        EditorManager::openEditorAt(m_tagStack.at(m_tagIndex - 1).from);
        triggerAction(TextEditor::Constants::FOLLOW_SYMBOL_UNDER_CURSOR);
    }
}

void FakeVimPlugin::handleDelayedQuit(bool forced, IEditor *editor)
{
    // This tries to simulate vim behaviour. But the models of vim and
    // Qt Creator core do not match well...
    if (EditorManager::hasSplitter()) {
        // Removing the split does not ask about unsaved changes. Like vim,
        // refuse to drop the last view of a modified document unless forced
        // (:q!), so the changes are not silently left behind
        // (QTCREATORBUG-32757). The no-splitter branch already prompts.
        IDocument *doc = editor ? editor->document() : nullptr;
        if (!forced && doc && doc->isModified()
                && DocumentModel::editorsForDocument(doc).size() <= 1) {
            if (FakeVimHandler *handler = m_editorToHandler.value(editor).handler) {
                handler->showMessage(MessageError,
                    Tr::tr("No write since last change (add ! to override)"));
            }
            return;
        }
        triggerAction(Core::Constants::REMOVE_CURRENT_SPLIT);
    } else {
        EditorManager::closeEditors({editor}, !forced);
    }
}

void FakeVimPlugin::handleDelayedQuitAll(bool forced)
{
    triggerAction(Core::Constants::REMOVE_ALL_SPLITS);
    EditorManager::closeAllEditors(!forced);
}

void FakeVimPlugin::handleBufferDelete(bool forced, IEditor *editor)
{
    EditorManager::closeEditors({editor}, !forced);
}

void FakeVimPlugin::quitFakeVim()
{
    settings().useFakeVim.setValue(false);
}

void FakeVimPlugin::resetCommandBuffer()
{
    showCommandBuffer(nullptr, QString(), -1, -1, 0);
}

void FakeVimPlugin::showCommandBuffer(FakeVimHandler *handler, const QString &contents, int cursorPos, int anchorPos,
                                             int messageLevel)
{
    //qDebug() << "SHOW COMMAND BUFFER" << contents;
    QTC_ASSERT(m_miniBuffer, return);

    if (!settings().useFakeVim()) {
        // The handler keeps running its buffer-local vim state (e.g. modelines,
        // autocommands) regardless of this setting, so that state is ready if
        // FakeVim gets turned on mid-session. Just don't show any of it.
        releaseEditorMiniBuffer();
        m_miniBuffer->setContents(QString(), -1, -1, MessageMode, nullptr);
        return;
    }

    if (settings().commandLineInEditor()) {
        if (!m_miniBufferEditor)
            updateEditorCommandLinePlacement();
        // Placement (which editor hosts the command line) is owned by
        // updateEditorCommandLinePlacement(); here we only update its content,
        // and only for the editor that currently hosts it. Buffers reported by
        // a non-current editor (e.g. one just left via Ctrl-W) are ignored, so
        // they cannot pull the command line back to the wrong split.
        auto tew = qobject_cast<TextEditorWidget *>(handler ? handler->widget() : nullptr);
        if (tew && tew == m_miniBufferEditor && m_editorMiniBuffer) {
            m_editorMiniBuffer->setContents(contents, cursorPos, anchorPos, messageLevel, handler);
            positionEditorMiniBuffer();
        }
        // Keep the status-bar buffer out of the way.
        m_miniBuffer->setContents(QString(), -1, -1, MessageMode, nullptr);
        return;
    }

    releaseEditorMiniBuffer();
    m_miniBuffer->setContents(contents, cursorPos, anchorPos, messageLevel, handler);
}

void FakeVimPlugin::updateEditorCommandLinePlacement()
{
    // Keep the in-editor command line on the current editor, so it follows
    // split switches immediately instead of only on the next keystroke
    // (QTCREATORBUG-21005).
    if (!settings().commandLineInEditor() || !settings().useFakeVim()) {
        releaseEditorMiniBuffer();
        return;
    }
    IEditor *editor = EditorManager::currentEditor();
    FakeVimHandler *handler = editor ? m_editorToHandler.value(editor).handler : nullptr;
    if (editor && handler)
        attachEditorMiniBuffer(handler, editor);
    else
        releaseEditorMiniBuffer();
}

void FakeVimPlugin::attachEditorMiniBuffer(FakeVimHandler *handler, IEditor *editor)
{
    // The editor's own widget, which is the text edit in one view and the
    // QQuickWidget the scene lives in for the other. Either is somewhere to
    // put an overlay; only what is inside them differs.
    QWidget * const host = editor->widget();
    auto * const document = qobject_cast<TextEditor::TextDocument *>(editor->document());
    if (!host || !document) {
        releaseEditorMiniBuffer();
        return;
    }

    if (!m_editorMiniBuffer) {
        m_editorMiniBuffer = new MiniBuffer;
        m_editorMiniBuffer->setAlwaysVisible(true);
    }
    if (m_miniBufferHost != host) {
        releaseEditorMiniBuffer();
        m_editorMiniBuffer->setParent(host);
        m_miniBufferHost = host;
        m_miniBufferEditor = TextEditorWidget::fromEditor(editor);
        m_miniBufferView = qobject_cast<TextEditor::TextViewport *>(
            TextEditor::keyTargetOf(editor));
        if (m_miniBufferEditor) {
            connect(m_miniBufferEditor, &TextEditorWidget::resized, this,
                    [this] { positionEditorMiniBuffer(); });
        } else if (m_miniBufferView) {
            // The item is the size of the scene, so its height changing is
            // the QQuickWidget having been resized.
            connect(m_miniBufferView, &QQuickItem::heightChanged, this,
                    [this] { positionEditorMiniBuffer(); });
            connect(m_miniBufferView, &QQuickItem::widthChanged, this,
                    [this] { positionEditorMiniBuffer(); });
        }
    }
    m_editorMiniBuffer->setFont(document->fontSettings().font());
    // Show an (empty) command line right away; also wires the handler as the
    // event filter for command-line editing.
    m_editorMiniBuffer->setContents(QString(), -1, -1, MessageMode, handler);
    positionEditorMiniBuffer();
}

void FakeVimPlugin::positionEditorMiniBuffer()
{
    if (!m_editorMiniBuffer || !m_miniBufferHost)
        return;
    // Reserve a strip at the editor bottom so the command line never covers
    // text; place the widget into it (QTCREATORBUG-21005).
    const int h = m_miniBufferHost->fontMetrics().height() + 4;
    if (m_miniBufferEditor) {
        m_miniBufferEditor->setEditorTextMargin("FakeVim.CommandLine", Qt::BottomEdge, h);
        const QRect vp = m_miniBufferEditor->viewport()->geometry();
        m_editorMiniBuffer->setGeometry(vp.x(), vp.y() + vp.height(), vp.width(), h);
    } else {
        if (m_miniBufferView)
            m_miniBufferView->setTextInset("FakeVim.CommandLine", Qt::BottomEdge, h);
        const QRect r = m_miniBufferHost->rect();
        m_editorMiniBuffer->setGeometry(r.x(), r.y() + r.height() - h, r.width(), h);
    }
    m_editorMiniBuffer->raise();
    m_editorMiniBuffer->show();
}

void FakeVimPlugin::releaseEditorMiniBuffer()
{
    if (!m_miniBufferHost)
        return;
    // Give the room back to whichever view was lending it.
    if (m_miniBufferEditor) {
        disconnect(m_miniBufferEditor, &TextEditorWidget::resized, this, nullptr);
        m_miniBufferEditor->setEditorTextMargin("FakeVim.CommandLine", Qt::BottomEdge, 0);
    }
    if (m_miniBufferView) {
        disconnect(m_miniBufferView, nullptr, this, nullptr);
        m_miniBufferView->setTextInset("FakeVim.CommandLine", Qt::BottomEdge, 0);
    }
    m_miniBufferEditor = nullptr;
    m_miniBufferView = nullptr;
    m_miniBufferHost = nullptr;
    if (m_editorMiniBuffer)
        m_editorMiniBuffer->hide();
}

int FakeVimPlugin::currentFile() const
{
    IEditor *editor = EditorManager::currentEditor();
    if (editor) {
        const std::optional<int> index = DocumentModel::indexOfDocument(editor->document());
        if (QTC_GUARD(index))
            return *index;
    }
    return -1;
}

void FakeVimPlugin::switchToFile(int n)
{
    int size = DocumentModel::entryCount();
    QTC_ASSERT(size, return);
    n = n % size;
    if (n < 0)
        n += size;
    EditorManager::activateEditorForEntry(DocumentModel::entries().at(n));
}

} // FakeVim::Internal

#include "fakevimplugin.moc"
