// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "texteditor_global.h"

#include <coreplugin/helpitem.h>
#include <coreplugin/icontext.h>

#include <functional>

QT_BEGIN_NAMESPACE
class QPoint;
class QTextCursor;
class QWidget;
QT_END_NAMESPACE

namespace TextEditor {

class SuggestionHost;
class TextDocument;

// What a hover handler may ask of the editor under the mouse. The widget
// editor and the Qt Quick one both answer it; a handler that needs more than
// this is asking about a view rather than about the text, and has to say so by
// casting.
class TEXTEDITOR_EXPORT HoverTarget
{
public:
    virtual ~HoverTarget();

    virtual TextDocument *textDocument() const = 0;
    virtual QTextCursor textCursor() const = 0;
    virtual void setContextHelpItem(const Core::HelpItem &item) = 0;

    // Qt's tooltips are widgets, so a Quick editor answers with the widget
    // hosting it.
    virtual QWidget *tooltipParent() = 0;

    // The cursor's top left in global coordinates, for a tooltip that places
    // itself rather than following the mouse.
    virtual QPoint globalCursorTopLeft() const = 0;

    // Whether this view is showing an inline suggestion. Only a view that can
    // show one says yes.
    virtual bool suggestionVisible() const { return false; }

    // The suggestion being shown, for a handler that drives one - cycles
    // between the alternatives, applies one - rather than describing it.
    // Null where the view cannot show suggestions at all.
    virtual SuggestionHost *suggestionHost() { return nullptr; }

    QString extraSelectionTooltip(int pos) const;
};

class TEXTEDITOR_EXPORT BaseHoverHandler
{
public:
    virtual ~BaseHoverHandler();

    void contextHelpId(HoverTarget *target,
                       int pos,
                       const Core::IContext::HelpCallback &callback);

    using ReportPriority = std::function<void(int priority)>;
    void checkPriority(HoverTarget *target, int pos, ReportPriority report);
    virtual void abort() {} // Implement for asynchronous priority reporter

    void showToolTip(HoverTarget *target, const QPoint &point);
    bool lastHelpItemAppliesTo(const HoverTarget *target) const;
    const QString &toolTip() const;

    enum {
        Priority_None = 0,
        Priority_Tooltip = 5,
        Priority_Help = 10,
        Priority_Diagnostic = 20,
        Priority_Suggestion = 40
    };

protected:
    void setPriority(int priority);
    int priority() const;

    void setToolTip(const QString &tooltip, Qt::TextFormat format = Qt::PlainText);

    void setLastHelpItemIdentified(const Core::HelpItem &help);
    const Core::HelpItem &lastHelpItemIdentified() const;

    bool isContextHelpRequest() const;

    void propagateHelpId(HoverTarget *target, const Core::IContext::HelpCallback &callback);

    // identifyMatch() is required to report a priority by using the "report" callback.
    // It is recommended to use e.g.
    //    Utils::ExecuteOnDestruction reportPriority([this, report](){ report(priority()); });
    // at the beginning of an implementation to ensure this in any case.
    virtual void identifyMatch(HoverTarget *target, int pos, ReportPriority report);
    virtual void operateTooltip(HoverTarget *target, const QPoint &point);

private:
    void process(HoverTarget *target, int pos, ReportPriority report);

    QString m_toolTip;
    Qt::TextFormat m_textFormat = Qt::PlainText;
    Core::HelpItem m_lastHelpItemIdentified;
    int m_priority = -1;
    bool m_isContextHelpRequest = false;
    HoverTarget *m_lastTarget = nullptr;
};

} // namespace TextEditor
