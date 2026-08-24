// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "codecompletion.h"

#include "codeassist/assistinterface.h"
#include "codeassist/assisttarget.h"
#include "codeassist/completionassistprovider.h"
#include "codeassist/assistproposaliteminterface.h"
#include "codeassist/genericproposalmodel.h"
#include "codeassist/iassistprocessor.h"
#include "codeassist/iassistproposal.h"
#include "codedocument.h"
#include "textdocument.h"

#include <utils/qtcassert.h>

#include <QPointer>
#include <QTextDocument>

using namespace Utils;

namespace TextEditor {

class CodeCompletionPrivate
{
public:
    QPointer<CodeDocument> m_codeDocument;
    std::unique_ptr<IAssistProcessor> m_processor;
    std::unique_ptr<IAssistProposal> m_proposal;
    ProposalModelPtr m_model;
    QStringList m_proposals;
    // Where the cursor was when the proposal was asked for. An item replaces
    // from the base position to there, so it has to be remembered: the view's
    // own cursor may have moved on by the time an answer arrives.
    int m_cursorPosition = -1;
};

CodeCompletion::CodeCompletion(QObject *parent)
    : QObject(parent)
    , d(new CodeCompletionPrivate)
{}

CodeCompletion::~CodeCompletion()
{
    delete d;
}

CodeDocument *CodeCompletion::codeDocument() const
{
    return d->m_codeDocument;
}

void CodeCompletion::setCodeDocument(CodeDocument *document)
{
    if (d->m_codeDocument == document)
        return;
    cancel();
    d->m_codeDocument = document;
    emit codeDocumentChanged();
}

QStringList CodeCompletion::proposals() const
{
    return d->m_proposals;
}

bool CodeCompletion::isActive() const
{
    return !d->m_proposals.isEmpty();
}

void CodeCompletion::invoke(int position)
{
    cancel();

    if (!d->m_codeDocument)
        return;
    TextDocument *document = d->m_codeDocument->textDocument();
    if (!document)
        return;
    CompletionAssistProvider *provider = document->completionAssistProvider();
    if (!provider)
        return;

    d->m_cursorPosition = position;
    QTextCursor cursor(document->document());
    cursor.setPosition(position);
    auto interface = std::make_unique<AssistInterface>(cursor,
                                                       document->filePath(),
                                                       ExplicitlyInvoked);

    d->m_processor.reset(provider->createProcessor(interface.get()));
    if (!d->m_processor)
        return;

    // A provider that has to go and ask - a language server does - answers
    // later; one that knows already answers from start().
    d->m_processor->setAsyncCompletionAvailableHandler(
        [this](IAssistProposal *proposal) { takeProposal(proposal); });
    takeProposal(d->m_processor->start(std::move(interface)));
}

void CodeCompletion::takeProposal(IAssistProposal *proposal)
{
    if (!proposal)
        return;

    d->m_proposal.reset(proposal);
    d->m_model = proposal->model();

    QStringList words;
    if (d->m_model) {
        for (int i = 0, n = d->m_model->size(); i < n; ++i)
            words.append(d->m_model->text(i));
    }
    d->m_proposals = words;
    emit proposalsChanged();
}

void CodeCompletion::apply(int index)
{
    if (!d->m_proposal || !d->m_model || index < 0 || index >= d->m_model->size())
        return;
    auto model = qSharedPointerCast<GenericProposalModel>(d->m_model);
    if (!model)
        return;
    AssistProposalItemInterface *item = model->proposalItem(index);
    if (!item)
        return;

    TextDocument *document = d->m_codeDocument ? d->m_codeDocument->textDocument() : nullptr;
    QTC_ASSERT(document, return);

    DocumentAssistTarget target(document->document());
    // Where the word being completed started; the item replaces from there.
    const int basePosition = d->m_proposal->basePosition();
    target.setCursorPosition(d->m_cursorPosition);
    item->apply(target, basePosition);

    cancel();
}

void CodeCompletion::cancel()
{
    if (d->m_processor)
        d->m_processor->cancel();
    d->m_processor.reset();
    d->m_proposal.reset();
    d->m_model.reset();
    if (!d->m_proposals.isEmpty()) {
        d->m_proposals.clear();
        emit proposalsChanged();
    }
}

} // namespace TextEditor
