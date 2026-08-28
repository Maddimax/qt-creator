// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "../core_global.h"

#include <QColor>
#include <QImage>
#include <QObject>

#include <functional>
#include <optional>

QT_BEGIN_NAMESPACE
class QFont;
class QTextBlock;
class QTextDocument;
QT_END_NAMESPACE

namespace Core {

// How wide a minimap is and how much room one line takes in it.
class CORE_EXPORT MinimapMetrics
{
public:
    int width = 100;
    int pixelsPerLine = 2;
    int lineGap = 1;

    int lineHeight() const { return pixelsPerLine + lineGap; }
};

// A colour for a whole block, where something knows better than the
// highlighter does - a diff marking a line added or removed, say.
using MinimapBlockColor = std::function<std::optional<QColor>(const QTextBlock &)>;

// A document drawn as one pixel per character, coloured by whatever the
// highlighter said about it: the shape of the code without the text. Blocks
// that are folded away take no room, so the picture matches what is on screen
// rather than what is in the file.
//
// \a extraLines is the padding a view that can scroll past the end needs, in
// lines. Answering here rather than asking a widget is what lets a view that
// is not one draw the same picture.
CORE_EXPORT QImage renderMinimap(const QTextDocument *document,
                                 const MinimapMetrics &metrics,
                                 const QFont &font,
                                 const QColor &defaultTextColor,
                                 int extraLines = 0,
                                 const MinimapBlockColor &overrideColor = {});

#ifdef WITH_TESTS
namespace Internal { QObject *createMinimapImageTest(); }
#endif

} // namespace Core
