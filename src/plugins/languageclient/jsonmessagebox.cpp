// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "jsonmessagebox.h"

#include "languageclienttr.h"

#include <coreplugin/inavigationwidgetfactory.h>

#include <texteditor/textdocument.h>
#include <texteditor/textmark.h>

#include <utils/filepath.h>
#include <utils/id.h>
#include <utils/macroexpander.h>
#include <utils/mimeconstants.h>
#include <utils/textutils.h>
#include <utils/theme/theme.h>
#include <utils/utilsicons.h>
#include <utils/variablechooser.h>

#include <QJsonDocument>
#include <QUrl>

using namespace TextEditor;
using namespace Utils;

namespace LanguageClient::Internal {

JsonMessageBox::JsonMessageBox(QObject *parent)
    : QObject(parent)
    , m_buffer(new CodeBuffer(this))
{
    m_buffer->setMimeType(Utils::Constants::JSON_MIMETYPE);
    connect(m_buffer, &CodeBuffer::textChanged, this, &JsonMessageBox::checkJson);

    // Every group fetched up front: the Qt Quick chooser filters recursively,
    // and a group whose children were never fetched matches nothing once
    // anything is typed. Same as the aspect chooser in aspectmodels.cpp.
    auto * const variables = new VariableModel(this);
    variables->addMacroExpanderProvider(MacroExpanderProvider(globalMacroExpander()));
    const std::function<void(const QModelIndex &)> fetchAll = [&](const QModelIndex &parent) {
        if (variables->canFetchMore(parent))
            variables->fetchMore(parent);
        for (int row = 0; row < variables->rowCount(parent); ++row)
            fetchAll(variables->index(row, 0, parent));
    };
    fetchAll({});
    m_variables = variables;
}

CodeBuffer *JsonMessageBox::buffer() const
{
    return m_buffer;
}

QAbstractItemModel *JsonMessageBox::variables() const
{
    return m_variables;
}

TextDocument *JsonMessageBox::document() const
{
    return m_buffer->textDocument();
}

QString JsonMessageBox::text() const
{
    return m_buffer->text();
}

void JsonMessageBox::setText(const QString &text)
{
    m_buffer->setText(text);
}

QWidget *JsonMessageBox::widget()
{
    if (!m_widget) {
        m_widget = Core::createQmlView(
            QUrl("qrc:/qt/qml/QtCreator/LanguageClient/JsonMessageEditor.qml"), this);
    }
    return m_widget;
}

// A parse error is marked on the line it is on, which is the only thing about
// the box that is about JSON rather than about text.
void JsonMessageBox::checkJson()
{
    using namespace Text;
    TextDocument * const document = this->document();
    const Id jsonMarkId("LanguageClient.JsonTextMarkId");
    const TextMarks marks = document->marks();
    for (TextMark *mark : marks) {
        if (mark->category().id == jsonMarkId)
            delete mark;
    }
    const QString content = document->plainText().trimmed();
    if (content.isEmpty())
        return;
    QJsonParseError error;
    QJsonDocument::fromJson(content.toUtf8(), &error);
    if (error.error == QJsonParseError::NoError)
        return;
    const Position pos = Position::fromPositionInDocument(document->document(), error.offset);
    if (!pos.isValid())
        return;
    auto mark = new TextMark(
        FilePath(), pos.line, {::LanguageClient::Tr::tr("JSON Error"), jsonMarkId});
    mark->setLineAnnotation(error.errorString());
    mark->setColor(Theme::CodeModel_Error_TextMarkColor);
    mark->setIcon(Icons::CODEMODEL_ERROR.icon());
    document->addMark(mark);
}

} // namespace LanguageClient::Internal
