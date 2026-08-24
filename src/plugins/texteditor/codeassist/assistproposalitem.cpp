// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "assistproposalitem.h"

#include "assisttarget.h"

#include <texteditor/quickfix.h>
#include <texteditor/snippets/snippet.h>
#include <texteditor/texteditor.h>

#include <QTextCursor>

namespace TextEditor {

/*!
    \class TextEditor::AssistProposalItem
    \brief The AssistProposalItem class acts as an interface for representing an assist
    proposal item.
    \ingroup CodeAssist

    This is class is part of the CodeAssist API.
*/

/*!
    \fn bool TextEditor::AssistProposalItem::implicitlyApplies() const

    Returns whether this item should implicitly apply in the case it is the only proposal
    item available.
*/

/*!
    \fn bool TextEditor::AssistProposalItem::prematurelyApplies(const QChar &c) const

    Returns whether the character \a c causes this item to be applied.
*/

/*!
    \fn void TextEditor::AssistProposalItem::apply(BaseTextEditor *editor, int basePosition) const

    This is the place to implement the actual application of the item.
*/

void AssistProposalItem::setIcon(const QIcon &icon)
{
    m_icon = icon;
}

QIcon AssistProposalItem::icon() const
{
    return m_icon;
}

void AssistProposalItem::setText(const QString &text)
{
    m_text = text;
}

QString AssistProposalItem::text() const
{
    return m_text;
}

void AssistProposalItem::setDetail(const QString &detail)
{
    m_detail = detail;
}

QString AssistProposalItem::detail() const
{
    return m_detail;
}

void AssistProposalItem::setData(const QVariant &var)
{
    m_data = var;
}

const QVariant &AssistProposalItem::data() const
{
    return m_data;
}

bool AssistProposalItem::isSnippet() const
{
    return data().canConvert<QString>();
}

bool AssistProposalItem::isValid() const
{
    return m_data.isValid();
}

quint64 AssistProposalItem::hash() const
{
    return 0;
}

bool AssistProposalItem::implicitlyApplies() const
{
    return !data().canConvert<QString>() && !data().canConvert<QuickFixOperation::Ptr>();
}

bool AssistProposalItem::prematurelyApplies(const QChar &c) const
{
    Q_UNUSED(c)
    return false;
}

void AssistProposalItem::apply(AssistTarget &target, int basePosition) const
{
    if (data().canConvert<QString>()) {
        applySnippet(target, basePosition);
    } else if (data().canConvert<QuickFixOperation::Ptr>()) {
        applyQuickFix(target, basePosition);
    } else {
        applyContextualContent(target, basePosition);
        target.encourageApply();
    }
}

void AssistProposalItem::applyContextualContent(AssistTarget &target, int basePosition) const
{
        const int currentPosition = target.position();
    target.replace(basePosition, currentPosition - basePosition, text());
}

void AssistProposalItem::applySnippet(AssistTarget &target, int basePosition) const
{
        target.insertCodeSnippet(basePosition, data().toString(), &Snippet::parse);
}

void AssistProposalItem::applyQuickFix(AssistTarget &target, int basePosition) const
{
    Q_UNUSED(target)
    Q_UNUSED(basePosition)

    QuickFixOperation::Ptr op = data().value<QuickFixOperation::Ptr>();
    op->perform();
}

} // namespace TextEditor
