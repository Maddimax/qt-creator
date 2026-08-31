// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <utils/aspects.h>

#include <QImage>
#include <QRect>

namespace ScreenRecorder::Internal {

// The frame being looked at, served to QML as image://screenrecorderframe/<n>.
// One frame at a time: putting the next one in drops the last, so scrubbing a
// clip does not keep the film. The number in the URL is only there to make an
// Image reload - the provider always answers with the current frame.
//
// Registered on QtcQuick::engine(), which every Qt Quick surface shares and a
// plugin may add to.
QString showFrame(const QImage &frame);
QImage currentFrame();

// A frame with a rectangle over it: the picture, everything outside the
// rectangle dimmed, and a dashed line on each of its edges. Drawn by
// CropScene.qml, which both the recording options and the crop-and-trim
// dialog put on their page.
class CropSceneAspect final : public Utils::BaseAspect
{
    Q_OBJECT

public:
    explicit CropSceneAspect(Utils::AspectContainer *container = nullptr);

    Utils::AspectPresentation presentation() const override;

    // Where the frame is and how big it is, and the rectangle over it. Read by
    // the QML as one map rather than as separate properties, because they only
    // ever change together.
    QVariant volatileVariantValue() const override;

    void setFrame(const QImage &frame);
    QRect fullRect() const { return m_full; }

    QRect cropRect() const { return m_crop; }
    // Said by the drawn scene as an edge is dragged, and by whatever fields sit
    // beside it. Always inside the frame, and never empty.
    Q_INVOKABLE void setCropRect(const QRect &rect);

    bool fullySelected() const;
    void selectEverything() { setCropRect(m_full); }

    QImage croppedFrame() const;

private:
    QString m_source;
    QRect m_full;
    QRect m_crop;
};


#ifdef WITH_TESTS
QObject *createCropSceneTest();
// The two containers that hold a scene, so that one test can render both
// pages. Owned by the caller.
Utils::AspectContainer *cropAspectsForTest();
Utils::AspectContainer *recordOptionsAspectsForTest();
// And the trimming half, which needs a clip to be about.
Utils::AspectContainer *trimAspectsForTest();
Utils::AspectContainer *cropAndTrimBarAspectsForTest();
#endif

} // namespace ScreenRecorder::Internal
