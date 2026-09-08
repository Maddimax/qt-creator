// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <QObject>

#include <memory>

QT_BEGIN_NAMESPACE
class QAbstractItemModel;
class QMenu;
class QMimeData;
QT_END_NAMESPACE

namespace Core {
class IEditor;
}

namespace Core::Internal {

// What the Qt Quick Open Documents view needs of the editor manager: the rows
// to draw, which of them the reader is in, and the two things a row does. The
// widget view reaches into DocumentModel and EditorManager from its own item
// delegate; a QML delegate cannot, so this stands between them - and it is
// the only thing here that knows the editor manager, which keeps the QML to
// laying rows out.
class OpenDocumentsList : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QAbstractItemModel *model READ model CONSTANT)
    Q_PROPERTY(int currentRow READ currentRow NOTIFY currentRowChanged)

public:
    explicit OpenDocumentsList(QObject *parent = nullptr);

    QAbstractItemModel *model() const;
    int currentRow() const;

    // A row is a row of the model above - the documents in the order the
    // sidebar shows them, with DocumentModel's <no document> entry left out.
    Q_INVOKABLE void activate(int row);
    Q_INVOKABLE void close(int row);

    // Dragging a row carries the document's file out of the sidebar - into a
    // split, an editor area, another application. The mime data is the
    // model's own, so a drop target that looks for Utils::DropMimeData sees
    // exactly what the tree view would have sent; the caller owns what comes
    // back. startDrag() is what a gesture calls.
    Q_INVOKABLE QMimeData *dragMimeData(int row) const;
    Q_INVOKABLE void startDrag(int row);

    // What the right-click menu holds for a row, in the order the widget
    // sidebar holds it. The QActions belong to a menu kept alive here while
    // they are on screen - a fresh one per row, the way the editor's gutter
    // builds its mark menu. A separator arrives as an entry with no text,
    // which is what the QML draws a line for.
    Q_INVOKABLE QObjectList contextMenuActions(int row);

signals:
    void currentRowChanged();

private:
    void follow(IEditor *editor);

    QAbstractItemModel *m_model = nullptr;
    // Owns the QActions contextMenuActions() hands out. They are built for
    // one row and are worthless once another row is asked about.
    std::unique_ptr<QMenu> m_menu;
    int m_currentRow = -1;
};

} // namespace Core::Internal
