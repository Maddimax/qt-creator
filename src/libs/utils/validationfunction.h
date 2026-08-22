// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "result.h"

#include <QFuture>
#include <QString>

#include <functional>
#include <variant>

namespace Utils {

class FancyLineEdit;

// Kept out of fancylineedit.h so that a header wanting only the validator type
// does not pull in QLineEdit. FancyLineEdit is used by reference, so a forward
// declaration is enough.
using AsyncValidationResult = Result<QString>;
using AsyncValidationFuture = QFuture<AsyncValidationResult>;
using AsyncValidationFunction = std::function<AsyncValidationFuture(QString)>;
using SynchronousValidationFunction = std::function<Result<>(FancyLineEdit &)>;
using SimpleSynchronousValidationFunction = std::function<Result<>(const QString &)>;
using ValidationFunction = std::variant<
    AsyncValidationFunction,
    SynchronousValidationFunction,
    SimpleSynchronousValidationFunction
>;

} // namespace Utils
