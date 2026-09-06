// Copyright (C) 2025 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "chatinputcompletion.h"

#include <texteditor/codeassist/assistinterface.h>
#include <texteditor/codeassist/assistproposalitem.h>
#include <texteditor/codeassist/asyncprocessor.h>
#include <texteditor/codeassist/functionhintproposal.h>
#include <texteditor/codeassist/genericproposal.h>
#include <texteditor/codeassist/ifunctionhintproposalmodel.h>
#include <texteditor/textdocument.h>
#include <texteditor/texteditor.h>

#include <projectexplorer/project.h>
#include <projectexplorer/projectmanager.h>

#include <utils/algorithm.h>

#include <QList>
#include <QRegularExpression>
#include <QSet>

using namespace TextEditor;

namespace AcpClient::Internal {

// ---------------------------------------------------------------------------
// CommandHintModel — single-entry model showing the input hint for a command
// ---------------------------------------------------------------------------

class CommandHintModel final : public IFunctionHintProposalModel
{
public:
    explicit CommandHintModel(const QString &hint) : m_hint(hint) {}

    void reset() override {}
    int size() const override { return 1; }
    QString text(int) const override { return m_hint; }
    int activeArgument(const QString &) const override { return 0; }

private:
    QString m_hint;
};

// ---------------------------------------------------------------------------
// CommandHintProcessor — shows a function hint when space is typed after a
// command name (e.g. "/foo ").
// ---------------------------------------------------------------------------

class CommandHintProcessor final : public IAssistProcessor
{
public:
    CommandHintProcessor(const QString &hint, int basePosition)
        : m_hint(hint)
        , m_basePosition(basePosition)
    {}

    IAssistProposal *perform() override
    {
        FunctionHintProposalModelPtr model(new CommandHintModel(m_hint));
        return new FunctionHintProposal(m_basePosition, model);
    }

private:
    const QString m_hint;
    const int m_basePosition;
};

// ---------------------------------------------------------------------------
// CommandCompletionProcessor — proposes slash-commands with description detail
// ---------------------------------------------------------------------------

class CommandCompletionProcessor final : public AsyncProcessor
{
public:
    explicit CommandCompletionProcessor(QList<CommandInfo> commands)
        : m_commands(std::move(commands))
    {}

    IAssistProposal *performAsync() override
    {
        const int cursorPos = interface()->position();
        int pos = cursorPos;
        QChar chr;
        do {
            chr = interface()->characterAt(--pos);
        } while (chr.isLetterOrNumber() || chr == '_');
        ++pos;

        QList<AssistProposalItemInterface *> items;
        items.reserve(m_commands.size());
        for (const CommandInfo &cmd : std::as_const(m_commands)) {
            auto *item = new AssistProposalItem;
            item->setText(cmd.name);
            if (!cmd.description.isEmpty())
                item->setDetail(cmd.description);
            items.append(item);
        }

        return new GenericProposal(pos, items);
    }

private:
    QList<CommandInfo> m_commands;
};

// ---------------------------------------------------------------------------
// ChatInputCompletionProcessor — word + filename completion for free text
// ---------------------------------------------------------------------------

class ChatInputCompletionProcessor final : public AsyncProcessor
{
public:
    explicit ChatInputCompletionProcessor(QString editorText, QStringList fileNames)
        : m_editorText(std::move(editorText))
        , m_fileNames(std::move(fileNames))
    {}

    IAssistProposal *performAsync() override
    {
        int pos = interface()->position();
        QChar chr;
        do {
            chr = interface()->characterAt(--pos);
        } while (chr.isLetterOrNumber() || chr == '_');
        ++pos;

        const int length = interface()->position() - pos;
        if (length < 2)
            return nullptr;

        static const QRegularExpression wordRE("([\\p{L}_][\\p{L}0-9_]{2,})");
        QSet<QString> seen;
        QList<AssistProposalItemInterface *> items;

        auto it = wordRE.globalMatch(m_editorText);
        while (it.hasNext()) {
            if (isCanceled())
                return nullptr;
            const QString word = it.next().captured();
            if (Utils::insert(seen, word)) {
                auto *item = new AssistProposalItem;
                item->setText(word);
                items.append(item);
            }
        }

        for (const QString &fileName : std::as_const(m_fileNames)) {
            if (isCanceled())
                return nullptr;
            if (Utils::insert(seen, fileName)) {
                auto *item = new AssistProposalItem;
                item->setText(fileName);
                items.append(item);
            }
        }

        return new GenericProposal(pos, items);
    }

private:
#ifdef WITH_TESTS
    friend class ChatInputCompletionTest;
#endif
    QString m_editorText;
    QStringList m_fileNames;
};

ChatInputCompletionProvider::ChatInputCompletionProvider(QObject *parent)
    : CompletionAssistProvider(parent)
{}

void ChatInputCompletionProvider::setAvailableCommands(const QList<CommandInfo> &commands)
{
    m_commands = commands;
}

bool ChatInputCompletionProvider::isActivationCharSequence(const QString &sequence) const
{
    return sequence == '/' || sequence == ' ';
}

IAssistProcessor *ChatInputCompletionProvider::createCommandProcessor(
    const TextEditor::AssistInterface *interface) const
{
    const QString plainText = interface->textDocument()->toPlainText();
    int index = 0;
    while (index < plainText.length() && plainText[index].isSpace())
        ++index;
    if (index == plainText.length() || plainText[index] != '/')
        return nullptr;

    const int startIndex = ++index;
    while (index < plainText.length() && !plainText[index].isSpace())
        ++index;

    if (index == plainText.length())
        return new CommandCompletionProcessor(m_commands);

    const QString command = plainText.mid(startIndex, index - startIndex);
    for (const CommandInfo &cmd : std::as_const(m_commands)) {
        if (cmd.name == command && !cmd.hint.isEmpty())
            return new CommandHintProcessor(cmd.hint, startIndex);
    }
    return nullptr;
}

IAssistProcessor *ChatInputCompletionProvider::createProcessor(const AssistInterface *interface) const
{
    if (auto commandProcessor = createCommandProcessor(interface))
        return commandProcessor;

    QString editorText;
    if (const TextDocument * const document = TextDocument::currentTextDocument())
        editorText = document->plainText();

    QStringList fileNames;
    if (const auto *project = ProjectExplorer::ProjectManager::startupProject()) {
        const Utils::FilePaths files = project->files(ProjectExplorer::Project::SourceFiles);
        fileNames.reserve(files.size());
        for (const Utils::FilePath &file : files)
            fileNames.append(file.fileName());
    }

    return new ChatInputCompletionProcessor(std::move(editorText), std::move(fileNames));
}

int ChatInputCompletionProvider::activationCharSequenceLength() const
{
    return 1;
}

} // namespace AcpClient::Internal

#ifdef WITH_TESTS

#include <coreplugin/editormanager/editormanager.h>
#include <texteditor/textdocument.h>

#include <utils/temporarydirectory.h>

#include <QTest>

namespace AcpClient::Internal {

class ChatInputCompletionTest final : public QObject
{
    Q_OBJECT

private slots:
    void testTheChatIsGivenTheFileTheReaderIsLookingAt()
    {
        Utils::TemporaryDirectory dir("acp-chat-completion");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("t.cpp");
        QVERIFY(file.writeFileContents("int distinctiveIdentifier = 1;\n"));

        TextEditor::TextEditorFactory * const factory
            = TextEditor::TextEditorFactory::preferredFactoryFor(file);
        QVERIFY(factory);
        const bool wasQuick = factory->usesQuickEditor();
        const QScopeGuard restore([factory, wasQuick] { factory->setUsesQuickEditor(wasQuick); });
        factory->setUsesQuickEditor(true);

        Core::IEditor * const editor = Core::EditorManager::openEditor(file);
        QVERIFY(editor);
        const QScopeGuard closeIt(
            [editor] { Core::EditorManager::closeEditors({editor}, false); });
        QVERIFY2(!TextEditor::TextEditorWidget::fromEditor(editor),
                 "the file opened in a widget editor, so this tests nothing");

        // What is being typed into the chat, which is not a text editor at
        // all - the file it completes from is whichever one is current.
        QTextDocument chatInput("dis");
        QTextCursor typed(&chatInput);
        typed.movePosition(QTextCursor::End);
        const TextEditor::AssistInterface interface(typed, {}, TextEditor::IdleEditor);

        ChatInputCompletionProvider provider;
        const std::unique_ptr<TextEditor::IAssistProcessor> processor(
            provider.createProcessor(&interface));
        QVERIFY(processor);
        auto * const chat = dynamic_cast<ChatInputCompletionProcessor *>(processor.get());
        QVERIFY2(chat, "the chat input was given a processor for something else");
        QVERIFY2(chat->m_editorText.contains("distinctiveIdentifier"),
                 "the chat was given none of the open file's text");
    }
};

QObject *createChatInputCompletionTest()
{
    return new ChatInputCompletionTest;
}

} // namespace AcpClient::Internal

#include "chatinputcompletion.moc"

#endif // WITH_TESTS
