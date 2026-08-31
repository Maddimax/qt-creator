// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <QColor>
#include <QList>

namespace ScxmlEditor::Common {

// The colours a theme starts from, one per state kind. A theme stores only
// the ones that differ from these.
const QList<QColor> &defaultThemeColors();

} // namespace ScxmlEditor::Common
