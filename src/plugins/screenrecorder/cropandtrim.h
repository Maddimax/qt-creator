// Copyright (C) 2023 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "ffmpegutils.h"

#include <utils/widgets.h>

#include <memory>

QT_BEGIN_NAMESPACE
class QToolButton;
QT_END_NAMESPACE

namespace ScreenRecorder {

class CropAndTrimAspects;

// The button that opens the crop and trim dialog, with what it would do beside
// it. A StyledBar so that the recorder's own layout can take it.
class CropAndTrimWidget : public Utils::StyledBar
{
    Q_OBJECT

public:
    CropAndTrimWidget(QWidget *parent = nullptr);
    ~CropAndTrimWidget() override;

    void setClip(const ClipInfo &clip);

signals:
    void cropRectChanged(const QRect &rect);
    void trimRangeChanged(FrameRange range);

private:
    const std::unique_ptr<CropAndTrimAspects> d;
};

// What the button says it would do - the crop and the trim, or that neither
// narrows anything.
QString cropAndTrimSummary(const ClipInfo &clip, const QRect &cropRect, FrameRange trimRange);


#ifdef WITH_TESTS
QObject *createTrimTest();
#endif

} // namespace ScreenRecorder
