// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "qtcquickwidget.h"

QT_BEGIN_NAMESPACE
class QTextDocument;
QT_END_NAMESPACE

namespace QtcQuick {

// A pane's output, drawn with Qt Quick. Shows a QTextDocument someone else
// fills - Utils::OutputFormatter writes into one - rather than holding text of
// its own, so the formatter, its line parsers and the links they leave all
// carry across untouched.
//
// The document is handed over rather than copied: a Quick TextArea can be
// given an existing one, and then it follows it. Which is why this class
// exists at all - QQuickTextDocument::setTextDocument() cannot be called from
// QML.
class QTCQUICK_EXPORT OutputView : public QuickWidget
{
    Q_OBJECT

public:
    explicit OutputView(QWidget *parent = nullptr);

    // What to show. Null draws nothing, which is what a pane that has not run
    // anything yet has.
    void setDocument(QTextDocument *document);
    QTextDocument *document() const;

    // The font the output is read in, before any zoom the pane applies.
    void setBaseFont(const QFont &font);

private:
    QTextDocument *m_document = nullptr;
};

} // namespace QtcQuick
