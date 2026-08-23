// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "qtcquick_global.h"

#include <QAbstractListModel>
#include <QHash>
#include <QQmlEngine>

#include <utils/aspects.h>

namespace QtcQuick {

// Registering the aspect types lets a QML delegate declare "property Aspect"
// and have qmllint check every binding against the real properties.
struct AspectForeign
{
    Q_GADGET
    QML_FOREIGN(Utils::BaseAspect)
    QML_NAMED_ELEMENT(Aspect)
    QML_UNCREATABLE("Aspects are created in C++")
};

// Exposes the aspects of a Utils::AspectContainer with named roles, so that a
// QML form can pick a delegate per aspect and bind to the aspect itself.
//
// This is deliberately a flat list rather than a Utils::TreeModel bridge:
// TreeItem::data() is column-oriented and has no role names, and solving that
// is a larger change than this needs.
class QTCQUICK_EXPORT AspectContainerModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Created from C++ by createAspectForm()")

public:
    // The control a generic editor should use for an aspect.
    enum Kind {
        Bool,
        String,
        FilePath,
        Integer,
        Double,
        Selection,
        StringList,
        FilePathList,
        MultiSelection,
        Color,
        FontFamily,
        TextDisplay,
        Container,
        Unsupported,
    };
    Q_ENUM(Kind)

    enum Role {
        AspectRole = Qt::UserRole + 1,
        KindRole,
        LabelTextRole,
        ToolTipRole,
        VisibleRole,
        OptionsRole,
        MinimumRole,
        MaximumRole,
        StepRole,
        ChildModelRole,
    };

    explicit AspectContainerModel(Utils::AspectContainer *container, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    static Kind kindOf(Utils::AspectControls::Control control);

private:
    QList<Utils::BaseAspect *> m_aspects;
    // A model per nested container, built on demand and owned by this one, so
    // that a group delegate can repeat over its children.
    mutable QHash<Utils::BaseAspect *, AspectContainerModel *> m_childModels;
};

} // namespace QtcQuick
