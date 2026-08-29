// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "utils_global.h"

#include "treemodel.h"

#include <QWidget>

namespace Utils {

class MacroExpander;
class MacroExpanderProvider;

namespace Internal { class VariableChooserPrivate; }

// What a macro expander offers, as rows: a group per provider and a variable
// under each. Separate from the chooser that shows it, so that something
// other than a QTreeView can - the roles are named, which is what a Qt Quick
// view reads them by.
class QTCREATOR_UTILS_EXPORT VariableModel : public TreeModel<>
{
public:
    enum Role {
        // "%{Foo}", what gets inserted.
        UnexpandedTextRole = Qt::UserRole,
        // What it stands for right now.
        ExpandedTextRole,
        // What it is for, and its current value, as rich text.
        CurrentValueDisplayRole
    };

    explicit VariableModel(QObject *parent = nullptr);

    void addMacroExpanderProvider(const MacroExpanderProvider &provider);

    // The variable being edited, which must not be offered as a value for
    // itself: it is listed and cannot be chosen.
    void setCurrentVariableName(const QByteArray &name);
    QByteArray currentVariableName() const;

    QHash<int, QByteArray> roleNames() const override;

private:
    QByteArray m_currentVariableName;
};

class QTCREATOR_UTILS_EXPORT VariableChooser : public QWidget
{
public:
    explicit VariableChooser(QWidget *parent = nullptr);
    ~VariableChooser() override;

    void addMacroExpanderProvider(const MacroExpanderProvider &provider);
    void addSupportedWidget(QWidget *textcontrol, const QByteArray &ownName = QByteArray());

    static void addSupportForChildWidgets(QWidget *parent, const MacroExpanderProvider &provider);

protected:
    bool event(QEvent *ev) override;
    bool eventFilter(QObject *, QEvent *event) override;

private:
    Internal::VariableChooserPrivate *d;
};

} // namespace Utils
