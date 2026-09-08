// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <QObject>

QT_BEGIN_NAMESPACE
class QAbstractItemModel;
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

signals:
    void currentRowChanged();

private:
    void follow(IEditor *editor);

    QAbstractItemModel *m_model = nullptr;
    int m_currentRow = -1;
};

} // namespace Core::Internal
