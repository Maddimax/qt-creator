// Copyright (C) 2024 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "texteditor_global.h"

#include <utils/textutils.h>

#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>

#include <memory>

namespace TextEditor {

class BaseHoverHandler;

// What taking part of a suggestion needs of the view showing it: where the
// caret is, and somewhere to put the rest when only some of it was taken.
//
// It used to be TextEditorWidget, which is what kept inline suggestions a
// widget feature - none of the three operations is a widget's, and naming
// them is what lets another view offer suggestions once it can draw them.
class TextSuggestion;

class TEXTEDITOR_EXPORT SuggestionTarget
{
public:
    virtual ~SuggestionTarget();

    virtual QTextCursor textCursor() const = 0;
    virtual QTextDocument *document() const = 0;
    virtual void insertSuggestion(std::unique_ptr<TextSuggestion> &&suggestion) = 0;
};

class TEXTEDITOR_EXPORT TextSuggestion
{
public:
    class TEXTEDITOR_EXPORT Data
    {
    public:
        Utils::Text::Range range;
        Utils::Text::Position position;
        QString text;
    };

    TextSuggestion(const Data &suggestion, QTextDocument *sourceDocument);
    virtual ~TextSuggestion();
    // Returns true if the suggestion was applied completely, false if it was only partially applied.
    virtual bool apply();
    // Returns true if the suggestion was applied completely, false if it was only partially applied.
    virtual bool applyWord(SuggestionTarget &target);
    virtual bool applyLine(SuggestionTarget &target);
    virtual bool filterSuggestions(SuggestionTarget &target);

    int currentPosition() const { return m_currentPosition; }
    void setCurrentPosition(int position) { m_currentPosition = position; }

    QTextDocument *replacementDocument() { return &m_replacementDocument; }
    QTextDocument *sourceDocument() { return m_sourceDocument; }

private:
    enum Part {Word, Line};
    bool applyPart(Part part, SuggestionTarget &target);

    Data m_suggestion;
    QTextDocument m_replacementDocument;
    QTextDocument *m_sourceDocument = nullptr;
    int m_currentPosition = -1;
};

class TEXTEDITOR_EXPORT CyclicSuggestion : public TextSuggestion
{
public:
    CyclicSuggestion(
        const QList<Data> &suggestions, QTextDocument *sourceDocument, int currentCompletion = 0);

    QList<Data> suggestions() const { return m_suggestions; }
    int currentSuggestion() const { return m_currentSuggestion; }

private:
    bool filterSuggestions(SuggestionTarget &target) override;

    QList<Data> m_suggestions;
    int m_currentSuggestion = 0;
};

BaseHoverHandler &suggestionHoverHandler();

} // namespace TextEditor
