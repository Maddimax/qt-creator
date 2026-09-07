// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "formwindoweditor.h"
#include "formwindowfile.h"
#include "designerconstants.h"

namespace Designer {

using namespace Internal;

FormWindowEditor::~FormWindowEditor() = default;

QString FormWindowEditor::contents() const
{
    return formWindowFile()->formWindowContents();
}

FormWindowFile *FormWindowEditor::formWindowFile() const
{
    return qobject_cast<FormWindowFile *>(textDocument());
}

} // namespace Designer

