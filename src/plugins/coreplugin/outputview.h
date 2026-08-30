// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "core_global.h"

#include <QWidget>

#include <functional>

QT_BEGIN_NAMESPACE
class QAction;
class QTextCursor;
class QTextDocument;
QT_END_NAMESPACE

namespace Core {

class IFindSupport;

// A view showing output: a QTextDocument that a Utils::OutputFormatter fills,
// with the font, zoom and links a pane expects around it.
//
// Abstract, and here rather than in QtcQuick, because Core cannot link a Qt
// Quick library and the panes that want one cannot reach past Core. A front
// end that can draw output installs the factory below; everything else asks
// Core for a view and does not learn what drew it.
class CORE_EXPORT OutputView : public QWidget
{
    Q_OBJECT

public:
    explicit OutputView(QWidget *parent = nullptr);
    ~OutputView() override;

    // The document to show. The view does not own it and does not copy it: a
    // pane keeps writing into it and the view follows.
    //
    // Not virtual, because setting a document also installs the search-result
    // highlighter on it, and that has to happen wherever the document comes
    // from. showDocument() below is what an implementation draws with.
    void setDocument(QTextDocument *document);
    QTextDocument *document() const;

    // The font before the zoom, which is what a font settings change sets.
    virtual void setBaseFont(const QFont &font) = 0;

    // Points added to the base font's size, as Core::OutputWindow means it -
    // a pane hands one view's zoom to its others, so the two have to agree.
    virtual void setFontZoom(float zoom) = 0;
    virtual float fontZoom() const = 0;
    void resetZoom() { setFontZoom(0); }

    virtual void setWheelZoomEnabled(bool enabled) = 0;

    // Long lines wrapped rather than reaching off to the right.
    virtual void setWordWrapEnabled(bool enabled) = 0;

    // What the output is drawn on, for a pane offering a colour of its own.
    virtual void setBackgroundColor(const QColor &color) = 0;

    // Entries a pane adds to the context menu, and whether they replace the
    // standard ones rather than joining them.
    virtual void setContextMenuActions(const QList<QAction *> &actions,
                                       bool replaceStandard) = 0;

    // Where in the document a point in the view is.
    virtual int documentPositionAt(qreal x, qreal y) const = 0;

    // What a pane's Copy and Select All act on.
    virtual void copy() = 0;
    virtual void selectAll() = 0;

    // Shows the end of the output.
    virtual void scrollToBottom() = 0;

    // Where the selection is, as a cursor on the document. Find support is
    // written against these two rather than against a text widget - see
    // Core::BaseTextFindBase, which asks an editor for a cursor, a document
    // and somewhere to hang the find bar, and none of that needs the editor to
    // be a QPlainTextEdit.
    virtual QTextCursor textCursor() const = 0;

    // Selects \a cursor's range and shows it, which is what a find step does
    // with a match it found.
    virtual void setTextCursor(const QTextCursor &cursor) = 0;

    // Ctrl+F over this view. Also aggregated onto the view, so a
    // FindToolBarPlaceHolder anchored to it finds it without being told.
    IFindSupport *findSupport() const;

private:
    IFindSupport *m_findSupport = nullptr;
    QTextDocument *m_document = nullptr;

protected:
    // Draw \a document. Called by setDocument(), which is what a pane uses.
    virtual void showDocument(QTextDocument *document) = 0;

signals:
    // The context menu is about to open at this point.
    void contextMenuAboutToShow(qreal x, qreal y);

    // What the view's context menu asked for. The view knows how to offer
    // these and nothing about how to do them.
    void saveContentsRequested();
    void copyContentsToScratchBufferRequested();
    void clearRequested();

    // A link was clicked. What it means is the pane's business - it is the one
    // holding the formatter whose handleLink() answers.
    void linkActivated(const QString &href);

    // The zoom changed by the wheel rather than by a caller, which is what a
    // pane listens for to zoom its other views to match.
    void wheelZoom();
};

using OutputViewFactory = std::function<OutputView *(QWidget *parent)>;
CORE_EXPORT void setOutputViewFactory(const OutputViewFactory &factory);

// Null where no front end installed a factory, so a caller can keep whatever
// it draws output with today.
CORE_EXPORT OutputView *createOutputView(QWidget *parent = nullptr);

} // namespace Core
