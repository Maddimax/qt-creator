// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "qtcquick_global.h"

#include <QAbstractListModel>
#include <QHash>
#include <QQmlEngine>

#include <utils/aspects.h>

namespace QtcQuick {

class AspectItemListModel;

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
        TriStateBool,
        String,
        FilePath,
        Integer,
        Double,
        Selection,
        StringList,
        StringListEditor,
        FilePathList,
        MultiSelection,
        Color,
        FontFamily,
        TextDisplay,
        Container,
        // A container whose aspects read as one value: drawn as one row, with
        // no group box around it. See AspectContainer::setInlineRow().
        InlineGroup,
        FlattenedGroup,
        BoolWithOwnLabel,
        AspectList,
        AspectInlineList,
        TextWithAction,
        Button,
        Radio,
        RadioGroup,
        Text,
        KeySequence,
        Secret,
        Table,
        GroupedList,
        Tree,
        // Shows nothing at all, which some aspects legitimately do.
        Invisible,
        Unsupported,
    };
    Q_ENUM(Kind)

    enum Role {
        AspectRole = Qt::UserRole + 1,
        KindRole,
    };

    explicit AspectContainerModel(Utils::AspectContainer *container, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    static Kind kindOf(Utils::AspectControls::Control control);

    // The kind for one aspect, which is kindOf() on its control plus the cases
    // where the control is right but the aspect cannot describe itself.
    static Kind kindOf(const Utils::BaseAspect *aspect);

    // Whether a generic form can show every aspect in \a container, nested
    // containers included. False means at least one would be a placeholder.
    static bool isFullyRenderable(const Utils::AspectContainer *container);

private:
    QList<Utils::BaseAspect *> m_aspects;
    // A model per nested container, built on demand and owned by this one, so
    // that a group delegate can repeat over its children.
};

} // namespace QtcQuick
