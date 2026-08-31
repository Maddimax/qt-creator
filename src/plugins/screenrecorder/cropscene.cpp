// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "cropscene.h"

#include <qtcquick/qtcquickengine.h>

#include <QQmlEngine>
#include <QQuickImageProvider>

#ifdef WITH_TESTS
#include <coreplugin/dialogs/ioptionspage.h>
#include <QTest>
#endif

namespace ScreenRecorder::Internal {

namespace {

const char kProviderName[] = "screenrecorderframe";

class FrameProvider : public QQuickImageProvider
{
public:
    FrameProvider()
        : QQuickImageProvider(QQuickImageProvider::Image)
    {}

    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override
    {
        Q_UNUSED(id)
        Q_UNUSED(requestedSize)
        if (size)
            *size = m_frame.size();
        return m_frame;
    }

    QImage m_frame;
};

// Owned by the engine once it is added; this only remembers where it is.
FrameProvider *s_provider = nullptr;
int s_generation = 0;

} // namespace

QString showFrame(const QImage &frame)
{
    if (!s_provider) {
        s_provider = new FrameProvider;
        QtcQuick::engine()->addImageProvider(QLatin1String(kProviderName), s_provider);
    }
    s_provider->m_frame = frame;
    // A new number every time: an Image does not reload a URL it is already
    // showing.
    return QString("image://%1/%2").arg(QLatin1String(kProviderName)).arg(++s_generation);
}

QImage currentFrame()
{
    return s_provider ? s_provider->m_frame : QImage();
}

CropSceneAspect::CropSceneAspect(Utils::AspectContainer *container)
    : BaseAspect(container)
{}

Utils::AspectPresentation CropSceneAspect::presentation() const
{
    Utils::AspectPresentation p = BaseAspect::presentation();
    // Drawn by the page itself, never by a delegate.
    p.control = Utils::AspectControls::Custom;
    return p;
}

QVariant CropSceneAspect::volatileVariantValue() const
{
    return QVariantMap{{"source", m_source},
                       {"fullWidth", m_full.width()},
                       {"fullHeight", m_full.height()},
                       {"x", m_crop.x()},
                       {"y", m_crop.y()},
                       {"width", m_crop.width()},
                       {"height", m_crop.height()}};
}

void CropSceneAspect::setFrame(const QImage &frame)
{
    m_source = showFrame(frame);
    const QRect was = m_full;
    m_full = QRect(QPoint(), frame.deviceIndependentSize().toSize());
    // A frame of a different size says nothing about the old rectangle.
    if (was != m_full)
        m_crop = m_full;
    emit changed();
}

void CropSceneAspect::setCropRect(const QRect &rect)
{
    QRect inside = m_full.isEmpty() ? rect : rect.intersected(m_full);
    // An empty rectangle is not a selection; keep at least one pixel.
    if (inside.isEmpty() && !m_full.isEmpty())
        inside = QRect(qBound(0, rect.x(), m_full.width() - 1),
                       qBound(0, rect.y(), m_full.height() - 1), 1, 1);
    if (inside == m_crop)
        return;
    m_crop = inside;
    emit changed();
}

bool CropSceneAspect::fullySelected() const
{
    return !m_full.isEmpty() && m_full == m_crop;
}

QImage CropSceneAspect::croppedFrame() const
{
    const QImage frame = currentFrame();
    return frame.isNull() ? QImage() : frame.copy(m_crop);
}

#ifdef WITH_TESTS

class CropSceneTest final : public QObject
{
    Q_OBJECT

private slots:
    void testTheRectangleStaysInsideTheFrame()
    {
        CropSceneAspect scene;
        scene.setFrame(QImage(100, 80, QImage::Format_RGB32));
        QCOMPARE(scene.fullRect(), QRect(0, 0, 100, 80));
        // A new frame is picked whole, which is what the widget did.
        QVERIFY(scene.fullySelected());

        scene.setCropRect({-10, -10, 500, 500});
        QCOMPARE(scene.cropRect(), QRect(0, 0, 100, 80));

        scene.setCropRect({10, 20, 30, 40});
        QCOMPARE(scene.cropRect(), QRect(10, 20, 30, 40));
        QVERIFY(!scene.fullySelected());

        // Dragging an edge past the far one leaves a rectangle, not nothing.
        scene.setCropRect({10, 20, 0, 0});
        QVERIFY2(!scene.cropRect().isEmpty(), "the whole frame was deselected");
    }

    void testANewFrameOfTheSameSizeKeepsTheRectangle()
    {
        // Scrubbing a clip hands in one frame after another; the rectangle is
        // about the clip, not about the frame.
        CropSceneAspect scene;
        scene.setFrame(QImage(100, 80, QImage::Format_RGB32));
        scene.setCropRect({10, 20, 30, 40});

        scene.setFrame(QImage(100, 80, QImage::Format_RGB32));
        QCOMPARE(scene.cropRect(), QRect(10, 20, 30, 40));

        // A frame of another size says nothing about it, so it starts again.
        scene.setFrame(QImage(60, 40, QImage::Format_RGB32));
        QCOMPARE(scene.cropRect(), QRect(0, 0, 60, 40));
    }

    void testWhatTheSceneHandsToQml()
    {
        CropSceneAspect scene;
        QImage frame(100, 80, QImage::Format_RGB32);
        frame.fill(Qt::red);
        scene.setFrame(frame);
        scene.setCropRect({10, 20, 30, 40});

        const QVariantMap drawn = scene.volatileVariantValue().toMap();
        QCOMPARE(drawn.value("fullWidth").toInt(), 100);
        QCOMPARE(drawn.value("fullHeight").toInt(), 80);
        QCOMPARE(drawn.value("x").toInt(), 10);
        QCOMPARE(drawn.value("y").toInt(), 20);
        QCOMPARE(drawn.value("width").toInt(), 30);
        QCOMPARE(drawn.value("height").toInt(), 40);
        QVERIFY2(drawn.value("source").toString().startsWith("image://screenrecorderframe/"),
                 "the frame is not served from anywhere");
    }

    void testTheFrameIsServedAndTheLastOneDropped()
    {
        // One frame is held at a time - a scrub through a clip must not keep
        // the film - and the URL changes so that an Image reloads.
        QImage first(10, 10, QImage::Format_RGB32);
        first.fill(Qt::red);
        QImage second(10, 10, QImage::Format_RGB32);
        second.fill(Qt::green);

        const QString firstUrl = showFrame(first);
        QCOMPARE(currentFrame().pixelColor(0, 0), QColor(Qt::red));

        const QString secondUrl = showFrame(second);
        QCOMPARE(currentFrame().pixelColor(0, 0), QColor(Qt::green));
        QVERIFY2(firstUrl != secondUrl, "an Image would not reload the same URL");
    }

    void testBothPagesDrawWithTheQmlTheyName()
    {
        // The two dialogs that show a crop scene, each with its own page over
        // the same component.
        const Utils::Result<> crop = Core::aspectFormRenders(cropAspectsForTest(), "CropPage.qml");
        QVERIFY2(crop, qPrintable(crop ? QString() : crop.error()));

        const Utils::Result<> record
            = Core::aspectFormRenders(recordOptionsAspectsForTest(), "RecordOptionsPage.qml");
        QVERIFY2(record, qPrintable(record ? QString() : record.error()));

        const Utils::Result<> trim
            = Core::aspectFormRenders(trimAspectsForTest(), "TrimPage.qml");
        QVERIFY2(trim, qPrintable(trim ? QString() : trim.error()));

        const Utils::Result<> bar
            = Core::aspectFormRenders(cropAndTrimBarAspectsForTest(), "CropAndTrimBar.qml");
        QVERIFY2(bar, qPrintable(bar ? QString() : bar.error()));
    }

    void testWhatIsCroppedOutOfTheFrame()
    {
        QImage frame(100, 80, QImage::Format_RGB32);
        frame.fill(Qt::red);
        frame.setPixelColor(15, 25, Qt::blue);

        CropSceneAspect scene;
        scene.setFrame(frame);
        scene.setCropRect({10, 20, 30, 40});

        const QImage cropped = scene.croppedFrame();
        QCOMPARE(cropped.size(), QSize(30, 40));
        QCOMPARE(cropped.pixelColor(5, 5), QColor(Qt::blue));
    }
};

QObject *createCropSceneTest()
{
    return new CropSceneTest;
}

#endif // WITH_TESTS

} // namespace ScreenRecorder::Internal

#ifdef WITH_TESTS
#include "cropscene.moc"
#endif
