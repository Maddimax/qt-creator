// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "qtcquick_global.h"

// Complete rather than forward-declared: it is a Q_INVOKABLE return type, and
// moc needs the metatype.
#include "namedaspects.h"

#include <QAbstractItemModel>
#include <QObject>
#include <QQmlEngine>
#include <QUrl>

namespace Utils { class BaseAspect; }

namespace QtcQuick {

class AspectContainerModel;
class AspectItemListModel;

// The models a delegate needs for an aspect that holds other aspects. Reached
// from QML so that a delegate works both inside the generic form, where the
// repeater supplies the aspect, and in a hand-written page, where only the
// aspect is at hand.
class QTCQUICK_EXPORT AspectModels : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    using QObject::QObject;

    // All three are cached on the aspect, so a page and the generic form share
    // one.
    Q_INVOKABLE QtcQuick::AspectItemListModel *itemList(Utils::BaseAspect *aspect);
    Q_INVOKABLE QtcQuick::AspectContainerModel *container(Utils::BaseAspect *aspect);

    // The aspects of a nested container, by name, so that a page composing
    // sub-containers - Behavior is five of them - can lay out their aspects
    // itself rather than settle for the generic form of each.
    Q_INVOKABLE QtcQuick::NamedAspects *named(Utils::BaseAspect *aspect);

    // Everything a delegate needs beyond the aspect's own properties: the
    // bounds, the choices, what may be added or removed. Read from the aspect
    // rather than taken as model roles, because a hand-written page has no
    // roles to give.
    Q_INVOKABLE QVariantMap presentation(Utils::BaseAspect *aspect);

    // The rows an aspect holds, from BaseAspect::tableModel(). Asked for here
    // rather than straight from the aspect so that the engine is told the
    // model is C++'s: QML takes ownership of a parentless QObject an invokable
    // returns, and its collector then frees a model the aspect still holds.
    Q_INVOKABLE QAbstractItemModel *tableModel(Utils::BaseAspect *aspect);

    // Moves a row inside a model that says it may be reordered, through the
    // model's own mimeData()/dropMimeData() - which is how a QTreeView does an
    // internal move, and where a model puts whatever else it needs to know
    // about what is being moved. Answers whether the model took it.
    Q_INVOKABLE bool moveRow(QAbstractItemModel *model,
                             const QModelIndex &from,
                             const QModelIndex &toParent,
                             int toRow);

    // Whether a table's model names any of its columns. A one-column list does
    // not, and a header bar with nothing in it is neither what the widget view
    // showed nor something the style's own delegate copes with: it assigns the
    // missing name to its label and the engine warns about it.
    Q_INVOKABLE bool namesItsColumns(QAbstractItemModel *model);

    // A file dialog hands back a URL and a path aspect stores a path. QML has
    // no conversion of its own that is not string surgery on "file://".
    Q_INVOKABLE QString localPath(const QUrl &url);

    // The families \a aspect will accept, which is not always all of them:
    // a terminal's font picker offers monospaced ones only. Qt.fontFamilies()
    // knows no such distinction, and whether a family is fixed pitch is a
    // question for the font database.
    Q_INVOKABLE QStringList fontFamilies(Utils::BaseAspect *aspect);

    // Whether \a path is on this machine. A path on a device has to be
    // browsed with our own dialog: the platform's knows only the machine it
    // runs on. QML has no way to tell one from the other.
    Q_INVOKABLE bool isLocalPath(const QString &path);

    // Whether the platform has a file dialog of its own to offer. Where it
    // has none the widget path chooser reached for ours, which at least
    // reaches a device; the Quick fallback picker does neither.
    Q_INVOKABLE bool hasNativeFileDialog();

    // What a model put in Qt::DecorationRole, as something an Image can load.
    // A QIcon is what a widget view wants and what QML cannot carry, so the
    // conversion happens here rather than in every model that has one. Empty
    // where the cell has no icon, which is most of them.
    Q_INVOKABLE QString decorationUrl(const QVariant &decoration);
};

} // namespace QtcQuick
