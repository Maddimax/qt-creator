// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <texteditor/codebuffer.h>

#include <QAbstractItemModel>
#include <QObject>
#include <QPointer>

QT_BEGIN_NAMESPACE
class QWidget;
QT_END_NAMESPACE

namespace TextEditor { class TextDocument; }

namespace LanguageClient::Internal {

// A box for a JSON message: the text, highlighted as JSON and checked as it is
// typed, with a variable chooser beside it. Drawn by the Qt Quick code view;
// what the inspector needs from it - the widget to lay out, the text to send -
// is here, so nothing has to know what draws it.
class JsonMessageBox : public QObject
{
    Q_OBJECT
    Q_PROPERTY(TextEditor::CodeBuffer *buffer READ buffer CONSTANT)
    Q_PROPERTY(QAbstractItemModel *variables READ variables CONSTANT)

public:
    explicit JsonMessageBox(QObject *parent = nullptr);

    TextEditor::CodeBuffer *buffer() const;
    QAbstractItemModel *variables() const;
    TextEditor::TextDocument *document() const;

    QString text() const;
    void setText(const QString &text);

    // Built the first time it is asked for. It takes this box with it: the
    // view owns its controller, so whoever lays the widget out owns both.
    QWidget *widget();

private:
    void checkJson();

    TextEditor::CodeBuffer *m_buffer = nullptr;
    QAbstractItemModel *m_variables = nullptr;
    QPointer<QWidget> m_widget;
};

} // namespace LanguageClient::Internal
