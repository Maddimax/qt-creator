// Copyright (C) 2023 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "ffmpegutils.h"

#include <utils/widgets.h>

QT_BEGIN_NAMESPACE
class QToolButton;
QT_END_NAMESPACE

namespace ScreenRecorder {

class CropAndTrimWidget : public Utils::StyledBar
{
    Q_OBJECT

public:
    CropAndTrimWidget(QWidget *parent = nullptr);

    void setClip(const ClipInfo &clip);

signals:
    void cropRectChanged(const QRect &rect);
    void trimRangeChanged(FrameRange range);

private:
    void updateWidgets();

    QToolButton *m_button;

    ClipInfo m_clipInfo;
    QRect m_cropRect;
    int m_currentFrame = 0;
    FrameRange m_trimRange;
    CropSizeWarningIcon *m_cropSizeWarningIcon;
};


#ifdef WITH_TESTS
QObject *createTrimTest();
#endif

} // namespace ScreenRecorder
