// Copyright (C) 2023 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <QObject>

namespace CppEditor::Internal {
class CppLocalRenaming;

// Watches local renamings and adapts the declaration if a function parameter
// was renamed. Everything it needs it asks the renaming for, and that knows
// which editor it is in, so a view that is not a widget gets this too.
class CppFunctionParamRenamingHandler : public QObject
{
    // For findChild: this is how either view's handler is found again.
    Q_OBJECT

public:
    explicit CppFunctionParamRenamingHandler(CppLocalRenaming &localRenaming,
                                             QObject *parent = nullptr);
    ~CppFunctionParamRenamingHandler() override;

#ifdef WITH_TESTS
    // Whether a declaration is lined up to follow the rename. The finder runs
    // after the rename starts, so a rename that ends before it answers takes
    // the declaration nowhere - a race nothing but a test is fast enough to
    // lose, and a test has to wait rather than sleep.
    bool waitingForDeclaration() const;
#endif

private:
    class Private;
    Private * const d;
};

} // namespace CppEditor::Internal
