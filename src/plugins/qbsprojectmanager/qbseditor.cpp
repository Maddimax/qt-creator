// Copyright (C) 2023 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "qbseditor.h"

#include "qbslanguageclient.h"
#include "qbsprojectmanagertr.h"

#include <languageclient/languageclientcompletionassist.h>
#include <languageclient/languageclientmanager.h>
#include <projectexplorer/projectexplorerconstants.h>
#include <projectexplorer/projectnodes.h>
#include <qmljseditor/qmljscompletionassist.h>
#include <texteditor/codeassist/genericproposal.h>
#include <utils/utilsicons.h>
#include <utils/mimeconstants.h>

#include <QPointer>

#include <memory>
#include <optional>

#ifdef WITH_TESTS
#include <coreplugin/editormanager/editormanager.h>
#include <coreplugin/editormanager/ieditor.h>
#include <qmljseditor/qmljseditordocument.h>
#include <texteditor/texteditor.h>
#include <utils/algorithm.h>
#include <utils/temporarydirectory.h>

#include <QScopeGuard>
#include <QTest>
#endif

using namespace LanguageClient;
using namespace QmlJSEditor;
using namespace TextEditor;
using namespace Utils;

namespace QbsProjectManager::Internal {

class QbsCompletionAssistProcessor : public LanguageClientCompletionAssistProcessor
{
public:
    QbsCompletionAssistProcessor(Client *client);

private:
    QList<AssistProposalItemInterface *> generateCompletionItems(
        const QList<LanguageServerProtocol::CompletionItem> &items) const override;
};

class MergedCompletionAssistProcessor : public IAssistProcessor
{
public:
    MergedCompletionAssistProcessor(const AssistInterface *interface) : m_interface(interface) {}
    ~MergedCompletionAssistProcessor();

private:
    IAssistProposal *perform() override;
    bool running() override { return m_started && (!m_qmlProposal || !m_qbsProposal); }
    void checkFinished();

    const AssistInterface * const m_interface;
    std::unique_ptr<IAssistProcessor> m_qmlProcessor;
    std::unique_ptr<IAssistProcessor> m_qbsProcessor;
    std::optional<IAssistProposal *> m_qmlProposal;
    std::optional<IAssistProposal *> m_qbsProposal;
    bool m_started = false;
};

class QbsCompletionAssistProvider : public QmlJSCompletionAssistProvider
{
private:
    IAssistProcessor *createProcessor(const AssistInterface *interface) const override
    {
        return new MergedCompletionAssistProcessor(interface);
    }
};

class QbsCompletionItem : public LanguageClientCompletionItem
{
public:
    using LanguageClientCompletionItem::LanguageClientCompletionItem;

private:
    QIcon icon() const override;
};

class MergedProposalModel : public GenericProposalModel
{
public:
    MergedProposalModel(const QList<GenericProposalModelPtr> &sourceModels);
};

static Client *clientForDocument(const TextDocument *doc)
{
    if (!doc)
        return nullptr;
    const QList<Client *> &candidates = LanguageClientManager::clientsSupportingDocument(doc);
    for (Client * const candidate : candidates) {
        if (const auto qbsClient = qobject_cast<QbsLanguageClient *>(candidate);
            qbsClient && qbsClient->isActive() && qbsClient->documentOpen(doc)) {
            return qbsClient;
        }
    }
    return nullptr;
}

// The QML answer first - a qbs file is QML - and the qbs language server for
// what QML does not know about.
static void findQbsLinkAt(TextDocument *document, const QTextCursor &cursor,
                          const LinkHandler &processLinkCallback,
                          bool resolveTarget, bool inNextSplit)
{
    const LinkHandler extendedCallback = [document = QPointer(document), cursor,
                                          processLinkCallback, resolveTarget](const Link &link) {
        if (link.hasValidTarget())
            return processLinkCallback(link);
        if (!document)
            return;
        if (Client * const client = clientForDocument(document)) {
            client->findLinkAt(document, cursor, processLinkCallback, resolveTarget,
                               LinkTarget::SymbolDef);
        }
    };
    findQmlJSLinkAt(document, cursor, extendedCallback, resolveTarget, inNextSplit);
}

QbsEditorFactory::QbsEditorFactory() : QmlJSEditorFactory("QbsEditor.QbsEditor")
{
    setDisplayName(Tr::tr("Qbs Editor"));
    setMimeTypes({Utils::Constants::QBS_MIMETYPE});
    // QML's document and editor, with the two things below of Qbs's own; the
    // widget subclass this had added nothing to QML's.
    setUsesQuickEditor(true);
    setCompletionAssistProvider(new QbsCompletionAssistProvider);
    setLinkFinder(&findQbsLinkAt);
}


MergedCompletionAssistProcessor::~MergedCompletionAssistProcessor()
{
    if (m_qmlProposal)
        delete *m_qmlProposal;
    if (m_qbsProposal)
        delete *m_qbsProposal;
}

IAssistProposal *MergedCompletionAssistProcessor::perform()
{
    m_started = true;
    if (Client *const qbsClient = clientForDocument(
            TextDocument::textDocumentForFilePath(m_interface->filePath()))) {
        m_qbsProcessor.reset(new QbsCompletionAssistProcessor(qbsClient));
        m_qbsProcessor->setAsyncCompletionAvailableHandler([this](IAssistProposal *proposal) {
            m_qbsProposal = proposal;
            checkFinished();
        });
        m_qbsProcessor->start(std::make_unique<AssistInterface>(m_interface->cursor(),
                                                                m_interface->filePath(),
                                                                m_interface->reason()));
    } else {
        m_qbsProposal = nullptr;
    }
    m_qmlProcessor.reset(QmlJSCompletionAssistProvider().createProcessor(m_interface));
    m_qmlProcessor->setAsyncCompletionAvailableHandler([this](IAssistProposal *proposal) {
        m_qmlProposal = proposal;
        checkFinished();
    });
    const auto qmlJsIface = static_cast<const QmlJSCompletionAssistInterface *>(m_interface);
    return m_qmlProcessor->start(
        std::make_unique<QmlJSCompletionAssistInterface>(qmlJsIface->cursor(),
                                                         qmlJsIface->filePath(),
                                                         m_interface->reason(),
                                                         qmlJsIface->semanticInfo()));
}

void MergedCompletionAssistProcessor::checkFinished()
{
    if (running())
        return;

    QList<GenericProposalModelPtr> sourceModels;
    int basePosition = -1;
    for (const IAssistProposal * const proposal : {*m_qmlProposal, *m_qbsProposal}) {
        if (proposal) {
            if (const auto model = proposal->model().dynamicCast<GenericProposalModel>())
                sourceModels << model;
            if (basePosition == -1)
                basePosition = proposal->basePosition();
            else
                QTC_CHECK(basePosition == proposal->basePosition());
        }
    }
    setAsyncProposalAvailable(
        new GenericProposal(basePosition >= 0 ? basePosition : m_interface->position(),
                            GenericProposalModelPtr(new MergedProposalModel(sourceModels))));
}

MergedProposalModel::MergedProposalModel(const QList<GenericProposalModelPtr> &sourceModels)
{
    QList<AssistProposalItemInterface *> items;
    for (const GenericProposalModelPtr &model : sourceModels) {
        items << model->originalItems();
        model->loadContent({});
    }
    loadContent(items);
}

QbsCompletionAssistProcessor::QbsCompletionAssistProcessor(Client *client)
    : LanguageClientCompletionAssistProcessor(client, nullptr, {})
{}

QList<AssistProposalItemInterface *> QbsCompletionAssistProcessor::generateCompletionItems(
    const QList<LanguageServerProtocol::CompletionItem> &items) const
{
    return Utils::transform<QList<AssistProposalItemInterface *>>(
        items, [](const LanguageServerProtocol::CompletionItem &item) {
            return new QbsCompletionItem(item);
        });
}

QIcon QbsCompletionItem::icon() const
{
    if (!item().detail()) {
        return ProjectExplorer::DirectoryIcon(
                   ProjectExplorer::Constants::FILEOVERLAY_MODULES).icon();
    }
    return CodeModelIcon::iconForType(CodeModelIcon::Property);
}

#ifdef WITH_TESTS

class QbsEditorTest final : public QObject
{
    Q_OBJECT

private slots:
    // A .qbs file is QML with a language server on top. It opened in a widget
    // subclass with nothing in it, over the document and factory QML uses; it
    // opens in the Qt Quick editor, with Qbs's merged completion where QML's
    // alone would be.
    void testAQbsFileOpensInTheQtQuickEditorWithQbssCompletion()
    {
        Utils::TemporaryDirectory dir("qbs-editor");
        QVERIFY(dir.isValid());
        const FilePath file = dir.filePath("project.qbs");
        QVERIFY(file.writeFileContents("import qbs\n\nProduct {\n    name: \"thing\"\n}\n"));
        TextEditorFactory * const factory = TextEditorFactory::preferredFactoryFor(file);
        QVERIFY2(factory, "no text editor claims a .qbs file");
        QCOMPARE(factory->id(), Utils::Id("QbsEditor.QbsEditor"));
        QVERIFY2(factory->usesQuickEditor(), "the Qbs editor is the widget editor");

        Core::IEditor * const editor = Core::EditorManager::openEditor(file);
        QVERIFY(editor);
        const QScopeGuard closeIt([editor] { Core::EditorManager::closeEditors({editor}, false); });
        QVERIFY2(!TextEditorWidget::fromEditor(editor), "the .qbs file opened in the widget editor");
        const QList<QWidget *> children = editor->widget()->findChildren<QWidget *>();
        QVERIFY2(Utils::anyOf(children, [](QWidget *w) { return w->inherits("QQuickWidget"); }),
                 "the .qbs file opened in no Qt Quick view either");
        auto * const document = qobject_cast<QmlJSEditorDocument *>(editor->document());
        QVERIFY2(document, "a .qbs file's document is not a QML document");
        QCOMPARE(document->id(), Utils::Id("QbsEditor.QbsEditor"));
        QVERIFY2(dynamic_cast<QbsCompletionAssistProvider *>(document->completionAssistProvider()),
                 "the .qbs file completes without the qbs language server's answers");
        QVERIFY2(TextEditorFactory::linkFinderFor(document), "the .qbs file answers no Follow Symbol");
    }
};

QObject *createQbsEditorTest()
{
    return new QbsEditorTest;
}

#endif // WITH_TESTS

} // namespace QbsProjectManager::Internal

#include "qbseditor.moc"
