// Copyright (C) 2018 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "languageclient_global.h"
#include "languageclientsymbolsupport.h"
#include "languageclientutils.h"
#include "semantichighlightsupport.h"

#include <texteditor/refactoringchanges.h>

#ifdef WITH_TESTS
#include "languageclientinterface.h"
#include <utils/algorithm.h>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#endif

namespace Core { class IDocument; }

namespace ProjectExplorer {
class BuildConfiguration;
class Project;
}

namespace TextEditor {
class IAssistProcessor;
class TextDocument;
class TextEditorWidget;
}

namespace Utils { namespace Text { class Range; } }

QT_BEGIN_NAMESPACE
class QWidget;
QT_END_NAMESPACE

namespace LanguageServerProtocol {
class ClientCapabilities;
class ClientInfo;
class ProgressToken;
class PublishDiagnosticsParams;
class Registration;
class ServerCapabilities;
class Unregistration;
} // namespace LanguageServerProtocol

namespace LanguageClient {
class BaseClientInterface;
class ClientPrivate;
class DiagnosticManager;
class DocumentSymbolCache;
class DynamicCapabilities;
class FunctionHintAssistProvider;
class HoverHandler;
class InterfaceController;
class LanguageClientCompletionAssistProvider;
class LanguageClientOutlineItem;
class LanguageClientQuickFixProvider;
class LanguageFilter;
class ProgressManager;

class LANGUAGECLIENT_EXPORT Client : public QObject
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(Client)

public:
    explicit Client(BaseClientInterface *clientInterface, const Utils::Id &id = {}); // takes ownership
     ~Client() override;

    // basic properties
    Utils::Id id() const;
    void setName(const QString &name);
    QString name() const;

    enum class SendDocUpdates { Send, Ignore };
    void sendMessage(const LanguageServerProtocol::JsonRpcMessage &message,
                     SendDocUpdates sendUpdates = SendDocUpdates::Send,
                     Schedule semanticTokensSchedule = Schedule::Delayed);

    void cancelRequest(const LanguageServerProtocol::MessageId &id);

    // server state handling
    void start();
    void setInitializationOptions(const QJsonObject& initializationOptions);
    void initialize();
    bool reset();
    void shutdown();
    enum State {
        Uninitialized,
        InitializeRequested,
        FailedToInitialize,
        Initialized,
        ShutdownRequested,
        FailedToShutdown,
        Shutdown,
        Error
    };
    State state() const;
    QString stateString() const;
    bool reachable() const;
    void resetRestartCounter();

    void setClientInfo(const LanguageServerProtocol::ClientInfo &clientInfo);
    // capabilities
    static LanguageServerProtocol::ClientCapabilities defaultClientCapabilities();
    void setClientCapabilities(const LanguageServerProtocol::ClientCapabilities &caps);
    const LanguageServerProtocol::ServerCapabilities &capabilities() const;
    QString serverName() const;
    QString serverVersion() const;
    const DynamicCapabilities &dynamicCapabilities() const;
    DynamicCapabilities &dynamicCapabilities();
    void registerCapabilities(const QList<LanguageServerProtocol::Registration> &registrations);
    void unregisterCapabilities(const QList<LanguageServerProtocol::Unregistration> &unregistrations);

    void setLocatorsEnabled(bool enabled);
    bool locatorsEnabled() const;
    void setAutoRequestCodeActions(bool enabled);

    // document synchronization
    void setSupportedLanguage(const LanguageFilter &filter);
    void setActivateDocumentAutomatically(bool enabled);
    bool isSupportedDocument(const TextEditor::TextDocument *document) const;
    bool isSupportedFile(const Utils::FilePath &filePath, const QString &mimeType) const;
    bool isSupportedUri(const LanguageServerProtocol::DocumentUri &uri) const;
    virtual void openDocument(TextEditor::TextDocument *document);
    void closeDocument(TextEditor::TextDocument *document,
                       const std::optional<Utils::FilePath> &overwriteFilePath = {});
    bool activatable() const;
    void setActivatable(bool active);
    virtual void activateDocument(TextEditor::TextDocument *document);
    void activateEditor(Core::IEditor *editor);
    virtual void deactivateDocument(TextEditor::TextDocument *document);
    void deactivateEditor(Core::IEditor *editor);

    bool documentOpen(const TextEditor::TextDocument *document) const;
    TextEditor::TextDocument *documentForFilePath(const Utils::FilePath &file) const;
    void setShadowDocument(const Utils::FilePath &filePath, const QString &contents);
    void removeShadowDocument(const Utils::FilePath &filePath);
    void documentContentsSaved(TextEditor::TextDocument *document);
    void documentWillSave(Core::IDocument *document);
    void documentContentsChanged(TextEditor::TextDocument *document,
                                 int position,
                                 int charsRemoved,
                                 int charsAdded);
    // The caret moved in \a editor, which is showing one of this client's
    // documents: ask the server what else the symbol under it touches. Takes
    // the editor rather than the widget - the caret is the view's and every
    // view has one.
    void cursorPositionChanged(Core::IEditor *editor);
    bool documentUpdatePostponed(const Utils::FilePath &fileName) const;
    int documentVersion(const Utils::FilePath &filePath) const;
    int documentVersion(const LanguageServerProtocol::DocumentUri &uri) const;
    void setDocumentChangeUpdateThreshold(int msecs);

    // workspace control
    virtual void setCurrentBuildConfiguration(ProjectExplorer::BuildConfiguration *bc);
    ProjectExplorer::BuildConfiguration *buildConfiguration() const;
    ProjectExplorer::Project *project() const;
    virtual void buildConfigurationOpened(ProjectExplorer::BuildConfiguration *bc);
    virtual void buildConfigurationClosed(ProjectExplorer::BuildConfiguration *bc);
    virtual bool canOpenProject(ProjectExplorer::Project *project);
    void updateConfiguration(const QJsonValue &configuration);

    // commands
    void requestCodeActions(const LanguageServerProtocol::DocumentUri &uri,
                            const LanguageServerProtocol::Diagnostic &diagnostic);
    void requestCodeActions(const LanguageServerProtocol::DocumentUri &uri,
                            const QList<LanguageServerProtocol::Diagnostic> &diagnostics);
    void requestCodeActions(const LanguageServerProtocol::CodeActionRequest &request);
    void handleCodeActionResponse(const LanguageServerProtocol::CodeActionRequest::Response &response,
                                  const LanguageServerProtocol::DocumentUri &uri);
    virtual void executeCommand(const LanguageServerProtocol::Command &command);

    // language support
    void addAssistProcessor(TextEditor::IAssistProcessor *processor);
    void removeAssistProcessor(TextEditor::IAssistProcessor *processor);
    SymbolSupport &symbolSupport();
    // In contrast to the findLinkAt of symbol support this find link makes sure that there is only
    // one request running at a time and cancels the running request if the document changes, cursor
    // moves or another link is requested
    void findLinkAt(TextEditor::TextDocument *document,
                    const QTextCursor &cursor,
                    Utils::LinkHandler callback,
                    const bool resolveTarget,
                    LinkTarget target);
    DocumentSymbolCache *documentSymbolCache();
    HoverHandler *hoverHandler();
    SemanticTokenSupport *semanticTokenSupport();
    QList<LanguageServerProtocol::Diagnostic> diagnosticsAt(const Utils::FilePath &filePath,
                                                            const QTextCursor &cursor) const;
    bool hasDiagnostic(const Utils::FilePath &filePath,
                       const LanguageServerProtocol::Diagnostic &diag) const;
    bool hasDiagnostics(const TextEditor::TextDocument *document) const;
    void hideDiagnostics(const Utils::FilePath &documentPath);
    void setSemanticTokensHandler(const SemanticTokensHandler &handler);
    void setSnippetsGroup(const QString &group);
    void setCompletionAssistProvider(LanguageClientCompletionAssistProvider *provider);
    void setFunctionHintAssistProvider(FunctionHintAssistProvider *provider);
    void setQuickFixAssistProvider(LanguageClientQuickFixProvider *provider);
    virtual bool supportsDocumentSymbols(const TextEditor::TextDocument *doc) const;
    virtual bool fileBelongsToProject(const Utils::FilePath &filePath) const;
    virtual LanguageClientOutlineItem *createOutlineItem(
        const LanguageServerProtocol::DocumentSymbol &symbol);

    LanguageServerProtocol::DocumentUri::PathMapper hostPathMapper() const;
    Utils::FilePath serverUriToHostPath(const LanguageServerProtocol::DocumentUri &uri) const;
    LanguageServerProtocol::DocumentUri hostPathToServerUri(const Utils::FilePath &path) const;
    Utils::OsType osType() const;

    // custom methods
    using CustomMethodHandler = std::function<bool(
        const LanguageServerProtocol::JsonRpcMessage &message)>;
    void registerCustomMethod(const QString &method, const CustomMethodHandler &handler);

    // logging
    enum class LogTarget { Console, Ui };
    void setLogTarget(LogTarget target);
    void log(QtMsgType msgType, const QString &message) const;

    template<typename Error>
    void log(const LanguageServerProtocol::ResponseError<Error> &responseError) const
    {
        log(QtMsgType::QtCriticalMsg, responseError.toString());
    }

    // Caller takes ownership.
    using CustomInspectorTab = std::pair<QWidget *, QString>;
    using CustomInspectorTabs = QList<CustomInspectorTab>;
    virtual const CustomInspectorTabs createCustomInspectorTabs() { return {}; }

    // Caller takes ownership
    virtual TextEditor::RefactoringFilePtr createRefactoringFile(const Utils::FilePath &filePath) const;

    void setCompletionResultsLimit(int limit);
    int completionResultsLimit() const;

    void foldOrUnfoldCommentBlocks(TextEditor::TextDocument *doc, bool fold);
    void foldOrUnfoldInactiveRegions(TextEditor::TextDocument *doc, bool fold);

signals:
    void initialized(const LanguageServerProtocol::ServerCapabilities &capabilities);
    void capabilitiesChanged(const DynamicCapabilities &capabilities);
    void documentUpdated(TextEditor::TextDocument *document);
    void workDone(const LanguageServerProtocol::ProgressToken &token);
    void shadowDocumentSwitched(const Utils::FilePath &filePath);
    void stateChanged(State state);
    void finished();

protected:
    void setError(const QString &message);
    ProgressManager *progressManager();
    void handleMessage(const LanguageServerProtocol::JsonRpcMessage &message);
    virtual void handleDiagnostics(const LanguageServerProtocol::PublishDiagnosticsParams &params);
    virtual DiagnosticManager *createDiagnosticManager();
    virtual void startImpl();

private:
    friend class ClientPrivate;
    ClientPrivate *d = nullptr;

    virtual void handleDocumentClosed(TextEditor::TextDocument *) {}
    virtual void handleDocumentOpened(TextEditor::TextDocument *) {}
    virtual QTextCursor adjustedCursorForHighlighting(const QTextCursor &cursor,
                                                      TextEditor::TextDocument *doc);
    virtual bool referencesShadowFile(const TextEditor::TextDocument *doc,
                                      const Utils::FilePath &candidate);
    virtual QList<Utils::Text::Range> additionalDocumentHighlights(
        TextEditor::TextDocument *, const QTextCursor &) { return {}; }
    virtual bool shouldSendDidSave(const TextEditor::TextDocument *) const { return true; }
};

#ifdef WITH_TESTS
namespace Internal {

// A server that never runs. It answers the initialize handshake with the
// capabilities it was given - which is the only answer a Client needs to reach
// Initialized - and records what it was asked to send. Reaching Initialized is
// the point: an unreachable Client queues its requests instead of sending
// them, so "the request was sent" is only an observable question once the
// handshake has happened.
class RecordingServer : public BaseClientInterface
{
public:
    explicit RecordingServer(const QJsonObject &capabilities)
        : m_capabilities(capabilities)
    {}

    Utils::FilePath serverDeviceTemplate() const override { return {}; }

    bool sawMethod(const QString &method) const
    {
        return Utils::contains(m_sent, [&method](const QJsonObject &message) {
            return message.value("method").toString() == method;
        });
    }

    // The position a request carried, so a test can pin what the caret was
    // taken to be rather than only that something was asked. Both numbers are
    // as the protocol counts them, from zero.
    QPair<int, int> positionOf(const QString &method) const
    {
        for (const QJsonObject &message : m_sent) {
            if (message.value("method").toString() != method)
                continue;
            const QJsonObject position
                = message.value("params").toObject().value("position").toObject();
            return {position.value("line").toInt(-1), position.value("character").toInt(-1)};
        }
        return {-1, -1};
    }

    void forget() { m_sent.clear(); }

protected:
    void sendData(const QByteArray &data) override
    {
        // sendMessage() hands the header and the content over as two separate
        // calls, so the content arrives as plain JSON with no framing to strip.
        const QJsonDocument document = QJsonDocument::fromJson(data);
        if (!document.isObject())
            return;
        const QJsonObject message = document.object();
        m_sent.append(message);
        if (message.value("method").toString() != "initialize")
            return;

        QJsonObject result;
        result["capabilities"] = m_capabilities;
        QJsonObject response;
        response["jsonrpc"] = "2.0";
        response["id"] = message.value("id");
        response["result"] = result;
        emit messageReceived(LanguageServerProtocol::JsonRpcMessage(response));
    }

private:
    const QJsonObject m_capabilities;
    QList<QJsonObject> m_sent;
};

} // namespace Internal

QObject *createClientEditorHandlerTest();
#endif

} // namespace LanguageClient
