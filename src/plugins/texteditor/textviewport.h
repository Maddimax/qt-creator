// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "texteditor_global.h"

#include <QColor>
#include <QPointer>
#include <QQmlEngine>
#include <QQuickItem>
#include <QRectF>
#include <QVariantMap>

#include <memory>
#include <vector>

QT_BEGIN_NAMESPACE
class QTextLayout;
QT_END_NAMESPACE

namespace TextEditor {

class CodeDocument;

// A Qt Quick view of a TextEditor::TextDocument drawn with the scene graph: one
// QSGTextNode per visible line, re-emitted every frame. This is the heavy path,
// the one a real editor needs - as opposed to CodeDocument on a TextEdit, which
// is a QTextDocument shown by a widget-shaped control and does not scale.
//
// Three rules from the spike (see "The QSGTextNode spike" in the migration
// design doc), each measured rather than reasoned about:
//
//  - Lay out on the GUI thread in updatePolish() and never in
//    updatePaintNode(). Creator's document layout is QObject-based, and laying
//    out on the render thread produced exactly the cross-thread QObject and
//    QTimer failures the plan predicted.
//  - Re-emit every visible line every frame. A per-line node cache saves 0.55 ms
//    of render thread at 1M lines and is not needed to hold vsync, so it is not
//    here to go stale.
//  - Merge selection foreground *and* background into QTextLayout::formats().
//    Emitting selection backgrounds as scene-graph rectangles punches
//    unhighlighted holes at tabs and at BiDi boundaries, and the geometry that
//    would fix it is private API. The one thing formats cannot cover is the
//    newline tail, which is a single rect.
//
// Cost is O(visible), not O(file), which is what makes a million lines the same
// price as ten thousand. That holds only while every line is the same height,
// so this does not wrap: a wrapped view needs a height cache before it can say
// which line is at a given scroll offset without laying out the ones above.
class TEXTEDITOR_EXPORT TextViewport : public QQuickItem
{
    Q_OBJECT
    QML_ELEMENT

    // The file being shown. A CodeDocument rather than a TextDocument because
    // opening a file is what gives it highlighting and an indenter, and that is
    // what CodeDocument is for.
    Q_PROPERTY(TextEditor::CodeDocument *document READ document WRITE setDocument
                   NOTIFY documentChanged)
    // How far into the document the viewport is, in pixels.
    Q_PROPERTY(qreal scrollY READ scrollY WRITE setScrollY NOTIFY scrollYChanged)
    Q_PROPERTY(qreal scrollX READ scrollX WRITE setScrollX NOTIFY scrollXChanged)
    // The whole document's height, so that a ScrollBar has something to size
    // itself against without knowing what a line is.
    Q_PROPERTY(qreal contentHeight READ contentHeight NOTIFY metricsChanged)
    Q_PROPERTY(qreal lineHeight READ lineHeight NOTIFY metricsChanged)
    // The theme's editor background. The viewport does not paint it - a QML
    // Rectangle behind it does - but the colour lives in FontSettings, which
    // QML cannot reach on its own.
    Q_PROPERTY(QColor backgroundColor READ backgroundColor NOTIFY metricsChanged)
    // What is on screen. Readable so that a test can say what was drawn without
    // reading the scene graph.
    Q_PROPERTY(int firstVisibleLine READ firstVisibleLine NOTIFY metricsChanged)
    Q_PROPERTY(int visibleLineCount READ visibleLineCount NOTIFY metricsChanged)
    // The selection, as positions in the document. Both -1 for none.
    Q_PROPERTY(int selectionStart READ selectionStart WRITE setSelectionStart
                   NOTIFY selectionChanged)
    Q_PROPERTY(int selectionEnd READ selectionEnd WRITE setSelectionEnd NOTIFY selectionChanged)

public:
    explicit TextViewport(QQuickItem *parent = nullptr);
    ~TextViewport() override;

    CodeDocument *document() const;
    void setDocument(CodeDocument *document);

    qreal scrollY() const;
    void setScrollY(qreal scrollY);
    qreal scrollX() const;
    void setScrollX(qreal scrollX);

    qreal contentHeight() const;
    qreal lineHeight() const;
    QColor backgroundColor() const;
    int firstVisibleLine() const;
    int visibleLineCount() const;

    int selectionStart() const;
    void setSelectionStart(int position);
    int selectionEnd() const;
    void setSelectionEnd(int position);

    // What the visible line at \a index was laid out with: its "text", its
    // "formats" as a list of {start, length} maps, and its "newlineTail" rect.
    // A selection is merged into the layout's formats rather than drawn as
    // anything of its own - that is the whole conclusion of the spike - so this
    // is the only way to see that it arrived. Empty for an index that is not on
    // screen.
    Q_INVOKABLE QVariantMap visibleLine(int index) const;

signals:
    void documentChanged();
    void scrollYChanged();
    void scrollXChanged();
    void metricsChanged();
    void selectionChanged();

protected:
    void updatePolish() override;
    QSGNode *updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *) override;
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;

private:
    // One visible line, ready to hand to the render thread. It owns its layout:
    // the document's own layouts belong to the GUI thread and to a QObject-based
    // document layout, and nothing here may reach back into either.
    struct Line
    {
        Line();
        ~Line();
        Line(Line &&other) noexcept;
        Line &operator=(Line &&other) noexcept;

        std::unique_ptr<QTextLayout> layout;
        QPointF at;
        // Where a selection runs past the end of the line. Empty otherwise.
        QRectF newlineTail;
        QColor newlineTailColour;
    };

    void rebuild();
    void documentChangedInternal();

    QPointer<CodeDocument> m_document;
    qreal m_scrollY = 0;
    qreal m_scrollX = 0;
    int m_selectionStart = -1;
    int m_selectionEnd = -1;

    // Everything below is produced in updatePolish() on the GUI thread and read
    // in updatePaintNode() on the render thread, with the GUI thread blocked.
    std::vector<Line> m_lines;
    qreal m_lineHeight = 0;
    qreal m_contentHeight = 0;
    int m_firstVisibleLine = 0;
    QColor m_background;
};

} // namespace TextEditor
