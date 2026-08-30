// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <utils/treemodel.h>

#include <QIcon>

QT_BEGIN_NAMESPACE
class QAbstractItemModel;
QT_END_NAMESPACE

#include <QColor>

namespace Debugger::Internal {

class ConsoleItem : public Utils::TreeItem
{
public:
    enum Roles {
        TypeRole = Qt::UserRole,
        FileRole,
        LineRole,
        ExpressionRole,
        // The file as it is shown, which is not the file it points at: see
        // shownFileName().
        FileNameRole,
        // What the row is written in. The delegate worked this out from the
        // type while painting, which is why it had no name and nothing else
        // could ask for it.
        TextColorRole
    };

    enum ItemType
    {
        DefaultType  = 0x01, // Can be used for unknown and for Return values
        DebugType    = 0x02,
        WarningType  = 0x04,
        ErrorType    = 0x08,
        InputType    = 0x10,
        AllTypes     = DefaultType | DebugType | WarningType | ErrorType | InputType
    };
    Q_DECLARE_FLAGS(ItemTypes, ItemType)

    ConsoleItem(ItemType itemType = ConsoleItem::DefaultType, const QString &expression = QString(),
                const QString &file = QString(), int line = -1);
    ConsoleItem(ItemType itemType, const QString &expression,
                std::function<void(ConsoleItem *)> doFetch);

    // The file a row points at, as it is written on the row. A URL comes back
    // as a path, and only the last component is shown - the rest of a path is
    // width the message needs more.
    static QString shownFileName(const QString &file);

    // Walking the console's history: the row of the previous or next thing
    // that was *typed*, skipping everything the console said back. -1 where
    // there is no such row, which is what leaves the entry as it is.
    //
    // Kept out of the editor because it is a question about the model, and
    // inside a QTextEdit the only way to ask it was to press Up.
    static int previousInputRow(const QAbstractItemModel *model, int fromRow);
    static int nextInputRow(const QAbstractItemModel *model, int fromRow);

    static QColor colorForType(ItemType type);
    static QIcon iconForType(ItemType type);

    ItemType itemType() const;
    QString expression() const;
    QString text() const;
    QString file() const;
    int line() const;
    Qt::ItemFlags flags(int column) const override;
    QVariant data(int column, int role) const override;
    bool setData(int column, const QVariant &data, int role) override;

    bool canFetchMore() const override;
    void fetchMore() override;

private:
    ItemType m_itemType;
    QString m_text;
    QString m_file;
    int m_line = -1;

    std::function<void(ConsoleItem *)> m_doFetch;
};

} // Debugger::Internal
