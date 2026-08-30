// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "qtcquickwidget.h"

#include <QFont>

QT_BEGIN_NAMESPACE
class QQuickItem;
class QTextCursor;
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

    // The font the output is read in, before the zoom.
    void setBaseFont(const QFont &font);
    QFont baseFont() const;

    // Points added to the base font's size, matching what Core::OutputWindow
    // means by it - a pane hands one view's zoom to its others.
    void setFontZoom(float zoom);
    float fontZoom() const;
    void resetZoom() { setFontZoom(0); }
    void setWheelZoomEnabled(bool enabled);

    // Where the selection is, as a cursor on the document. A Quick text item
    // keeps its selection as three integers rather than as a cursor, so this
    // is a conversion in both directions.
    QTextCursor textCursor() const;

    // Selects the range. Scrolling it into view needs nothing here: a
    // TextArea inside a ScrollView follows its own cursor, in both directions
    // - which a control that stopped this from scrolling proved by not
    // failing anything.
    void setTextCursor(const QTextCursor &cursor);

    // The link at a point in the view, empty where there is none. What the
    // widget got from QPlainTextEdit::anchorAt().
    QString linkAt(qreal x, qreal y) const;
    QString hoveredLink() const;

signals:
    // Where the pane calls Utils::OutputFormatter::handleLink(). The view does
    // not know what a link means, only that one was clicked.
    void linkActivated(const QString &href);

    // The zoom changed by the wheel rather than by a caller, which is what a
    // pane listens for to zoom its other views to match.
    void wheelZoom();

private slots:
    // Ctrl+wheel, in the units Core::OutputWindow uses: one notch is one
    // point. Refusing to go below the floor rather than clamping to it keeps
    // a wheel held down from silently accumulating zoom it will not show.
    void zoomBy(double delta);

private:
    void applyFont();
    QQuickItem *textArea() const;

    QTextDocument *m_document = nullptr;
    QFont m_baseFont;
    float m_zoom = 0;
};

} // namespace QtcQuick
