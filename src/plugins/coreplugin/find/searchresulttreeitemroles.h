// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <QAbstractItemView>

namespace Core::Internal::ItemDataRoles {

enum Roles {
    ResultItemRole = Qt::UserRole,
    ResultLineRole,
    ResultBeginLineNumberRole,
    ResultIconRole,
    ResultHighlightBackgroundColor,
    ResultHighlightForegroundColor,
    FunctionHighlightBackgroundColor,
    FunctionHighlightForegroundColor,
    ResultBeginColumnNumberRole,
    SearchTermLengthRole,
    ContainingFunctionNameRole,
    IsGroupingItemRole,

    // What a row draws, rather than what it is made of. The three below are
    // one answer between them: the text with a group's child count appended
    // and its tabs expanded, and where inside *that* the match is. The
    // offsets the roles above carry are into the raw line, so a consumer that
    // used them without expanding tabs the same way put the highlight in the
    // wrong place.
    DrawnTextRole,
    DrawnHighlightStartRole,
    DrawnHighlightLengthRole,
    // "[in someFunction]", or empty where the language did not say.
    DrawnFunctionTextRole
};

} // namespace Core::Internal::ItemDataRoles
