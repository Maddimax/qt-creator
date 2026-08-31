// Copyright (C) 2023 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "cropandtrim.h"
#include "cropscene.h"

#ifdef WITH_TESTS
#include <QTest>
#endif

#include <utils/aspects.h>
#include <utils/aspectwidgets.h>
#include <utils/guard.h>

#include <QHBoxLayout>
#include <QVBoxLayout>

#include "ffmpegutils.h"
#include "screenrecordersettings.h"
#include "screenrecordertr.h"

#include <utils/filedialogs.h>
#include <utils/fileutils.h>
#include <utils/layoutbuilder.h>
#include <utils/qtcprocess.h>
#include <utils/stylehelper.h>
#include <utils/utilsicons.h>
#include <utils/widgets.h>

#include <coreplugin/icore.h>

#include <QAction>
#include <QClipboard>
#include <QDialog>
#include <QDialogButtonBox>
#include <QGuiApplication>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QSpinBox>
#include <QStyleOptionSlider>
#include <QToolButton>

using namespace Utils;

namespace ScreenRecorder {

// What the cropping half of the dialog asks: the rectangle, in the scene and
// in four fields, and what can be done with what is inside it.
class CropAspects final : public AspectContainer
{
public:
    CropAspects();

    QRect cropRect() const { return scene.cropRect(); }
    void setCropRect(const QRect &rect) { scene.setCropRect(rect); }
    void setImage(const QImage &image);

    Internal::CropSceneAspect scene{this};
    IntegerAspect x{this};
    IntegerAspect y{this};
    IntegerAspect width{this};
    IntegerAspect height{this};
    ActionAspect reset{this};
    ActionAspect saveImage{this};
    ActionAspect copyImage{this};

private:
    void showTheRectangle();

    Guard m_updating;
};

CropAspects::CropAspects()
{
    setAutoApply(true);
    setQmlSource(QUrl("qrc:/qt/qml/QtCreator/ScreenRecorder/CropPage.qml"));

    scene.setQmlName("Scene");

    const auto field = [](IntegerAspect &aspect, const char *qmlName, const QString &label) {
        aspect.setQmlName(QString::fromLatin1(qmlName));
        aspect.setLabelText(label);
        aspect.setSuffix(" px");
        aspect.setRange(0, 99999);
    };
    field(x, "X", Tr::tr("X:"));
    field(y, "Y", Tr::tr("Y:"));
    field(width, "Width", Tr::tr("Width:"));
    field(height, "Height", Tr::tr("Height:"));
    width.setRange(1, 99999);
    height.setRange(1, 99999);

    reset.setQmlName("Reset");
    reset.setActionIcon(Icons::RESET.icon());
    reset.setToolTip(Tr::tr("Select the whole frame."));
    reset.setAction([this] { scene.selectEverything(); });

    saveImage.setQmlName("SaveImage");
    saveImage.setActionIcon(Icons::SAVEFILE.icon());
    saveImage.setToolTip(Tr::tr("Save current, cropped frame as image file."));
    saveImage.setAction([this] {
        FilePathAspect &lastDir = Internal::settings().lastSaveImageDirectory;
        const QString ext(".png");
        FilePath file = FileUtils::getSaveFilePath(Tr::tr("Save Current Frame As"),
                                                   lastDir(), "*" + ext);
        if (file.isEmpty())
            return;
        if (!file.endsWith(ext))
            file = file.stringAppended(ext);
        lastDir.setValue(file.parentDir());
        lastDir.writeToSettingsImmediatly();
        scene.croppedFrame().save(file.toUrlishString());
    });

    copyImage.setQmlName("CopyImage");
    copyImage.setActionIcon(Icons::SNAPSHOT.icon());
    copyImage.setToolTip(Tr::tr("Copy current, cropped frame as image to the clipboard."));
    copyImage.setAction([this] {
        QGuiApplication::clipboard()->setImage(scene.croppedFrame());
    });

    // The scene and the four fields are two ways of saying the same rectangle.
    connect(&scene, &BaseAspect::changed, this, [this] {
        if (!m_updating.isLocked())
            showTheRectangle();
    });
    for (IntegerAspect *aspect : {&x, &y, &width, &height}) {
        connect(aspect, &BaseAspect::changed, this, [this] {
            if (m_updating.isLocked())
                return;
            const GuardLocker locker(m_updating);
            scene.setCropRect({int(x()), int(y()), int(width()), int(height())});
            showTheRectangle();
        });
    }
    showTheRectangle();
}

void CropAspects::showTheRectangle()
{
    const GuardLocker locker(m_updating);
    const QRect r = scene.cropRect();
    x.setValue(r.x());
    y.setValue(r.y());
    width.setValue(r.width());
    height.setValue(r.height());
    // Nothing to reset when the whole frame is already picked.
    reset.setEnabled(!scene.fullySelected());
}

void CropAspects::setImage(const QImage &image)
{
    {
        const GuardLocker locker(m_updating);
        scene.setFrame(image);
        const QRect full = scene.fullRect();
        x.setRange(0, qMax(0, full.width() - 1));
        y.setRange(0, qMax(0, full.height() - 1));
        width.setRange(1, qMax(1, full.width()));
        height.setRange(1, qMax(1, full.height()));
    }
    showTheRectangle();
}

// The cropping half of the dialog: an aspect form, held in a widget so that
// the dialog's layout can take it.
class CropWidget : public QWidget
{
public:
    explicit CropWidget(QWidget *parent = nullptr);

    QRect cropRect() const { return d->cropRect(); }
    void setCropRect(const QRect &rect) { d->setCropRect(rect); }
    void setImage(const QImage &image) { d->setImage(image); }

private:
    const std::unique_ptr<CropAspects> d;
};

CropWidget::CropWidget(QWidget *parent)
    : QWidget(parent)
    , d(new CropAspects)
{
    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    if (QWidget *form = AspectWidgets::createAspectForm(d.get()))
        layout->addWidget(form);
}

// What a frame number reads as: the number itself, zero-padded to the widest
// the clip needs, and the time it lands at. What TimeLabel drew.
static QString frameLabel(const ClipInfo &clip, int frame)
{
    const int digits = qCeil(log10(double(clip.framesCount() + 1)));
    return QString("<b>%1</b> (%2)")
        .arg(frame, digits, 10, QLatin1Char('0'))
        .arg(clip.timeStamp(frame));
}

// Where the reader is in the clip and which part of it is being kept. Drawn as
// one slider with a band behind its handle: the widget painted the band into
// the groove itself and left the handle to the style.
class TrimSliderAspect final : public BaseAspect
{
    Q_OBJECT

public:
    explicit TrimSliderAspect(AspectContainer *container = nullptr)
        : BaseAspect(container)
    {}

    AspectPresentation presentation() const override
    {
        AspectPresentation p = BaseAspect::presentation();
        // Drawn by the page itself, never by a delegate.
        p.control = AspectControls::Custom;
        return p;
    }

    QVariant volatileVariantValue() const override
    {
        return QVariantMap{{"frames", m_frames},
                           {"current", m_current},
                           {"start", m_range.first},
                           {"end", m_range.second}};
    }

    void setFrameCount(int frames)
    {
        m_frames = frames;
        emit changed();
    }

    int currentFrame() const { return m_current; }

    // Said by the drawn slider as it is dragged, and by whatever else moves
    // the reader through the clip.
    Q_INVOKABLE void setCurrentFrame(int frame)
    {
        const int inside = qBound(0, frame, m_frames);
        if (inside == m_current)
            return;
        m_current = inside;
        emit changed();
    }

    FrameRange range() const { return m_range; }
    void setRange(FrameRange range)
    {
        if (range == m_range)
            return;
        m_range = range;
        emit changed();
    }

private:
    int m_frames = 0;
    int m_current = 0;
    FrameRange m_range = {0, 0};
};

// What the trimming half of the dialog asks: where the reader is, and where
// the part being kept starts and ends.
class TrimAspects final : public AspectContainer
{
    Q_OBJECT

public:
    explicit TrimAspects(const ClipInfo &clip);

    int currentFrame() const { return slider.currentFrame(); }
    void setCurrentFrame(int frame) { slider.setCurrentFrame(frame); }
    FrameRange trimRange() const { return slider.range(); }
    void setTrimRange(FrameRange range);

    TrimSliderAspect slider{this};
    TextDisplay currentTime{this};
    TextDisplay clipDuration{this};
    ActionAspect setStart{this};
    TextDisplay startTime{this};
    ActionAspect setEnd{this};
    TextDisplay endTime{this};
    TextDisplay rangeTime{this};
    ActionAspect resetTrim{this};

signals:
    void positionChanged();
    void trimRangeChanged(FrameRange range);

private:
    void showWhatIsKept();

    const ClipInfo m_clipInfo;
};

TrimAspects::TrimAspects(const ClipInfo &clip)
    : m_clipInfo(clip)
{
    setAutoApply(true);
    setQmlSource(QUrl("qrc:/qt/qml/QtCreator/ScreenRecorder/TrimPage.qml"));

    slider.setQmlName("Slider");
    slider.setFrameCount(m_clipInfo.framesCount());

    const auto time = [](TextDisplay &aspect, const char *qmlName) {
        aspect.setQmlName(QString::fromLatin1(qmlName));
        aspect.setTextFormat(AspectControls::TextFormat::RichText);
    };
    time(currentTime, "CurrentTime");
    time(clipDuration, "ClipDuration");
    time(startTime, "StartTime");
    time(endTime, "EndTime");
    time(rangeTime, "RangeTime");

    setStart.setQmlName("SetStart");
    setStart.setActionText(Tr::tr("Start:"));
    setStart.setToolTip(Tr::tr("Keep the clip from here."));
    setStart.setAction([this] {
        setTrimRange({currentFrame(), trimRange().second});
        emit trimRangeChanged(trimRange());
    });

    setEnd.setQmlName("SetEnd");
    setEnd.setActionText(Tr::tr("End:"));
    setEnd.setToolTip(Tr::tr("Keep the clip up to here."));
    setEnd.setAction([this] {
        setTrimRange({trimRange().first, currentFrame()});
        emit trimRangeChanged(trimRange());
    });

    resetTrim.setQmlName("ResetTrim");
    resetTrim.setActionIcon(Icons::RESET.icon());
    resetTrim.setToolTip(Tr::tr("Keep the whole clip."));
    resetTrim.setAction([this] {
        setTrimRange({0, m_clipInfo.framesCount()});
        emit trimRangeChanged(trimRange());
    });

    connect(&slider, &BaseAspect::changed, this, [this] {
        showWhatIsKept();
        emit positionChanged();
    });

    clipDuration.setText(frameLabel(m_clipInfo, m_clipInfo.framesCount()));
    setTrimRange({0, m_clipInfo.framesCount()});
}

void TrimAspects::setTrimRange(FrameRange range)
{
    slider.setRange(range);
    showWhatIsKept();
}

void TrimAspects::showWhatIsKept()
{
    const FrameRange range = trimRange();
    const int current = currentFrame();

    currentTime.setText(frameLabel(m_clipInfo, current));
    startTime.setText(frameLabel(m_clipInfo, range.first));
    endTime.setText(frameLabel(m_clipInfo, range.second));
    rangeTime.setText(frameLabel(m_clipInfo, range.second - range.first));

    // Only where there would still be a clip left afterwards.
    setStart.setEnabled(current < m_clipInfo.framesCount() && current < range.second);
    setEnd.setEnabled(current > 0 && current > range.first);
    resetTrim.setEnabled(!m_clipInfo.isCompleteRange(range));
}

// The trimming half of the dialog: an aspect form, held in a widget so that
// the dialog's layout can take it.
class TrimWidget : public QWidget
{
    Q_OBJECT

public:
    explicit TrimWidget(const ClipInfo &clip, QWidget *parent = nullptr);

    void setCurrentFrame(int frame) { d->setCurrentFrame(frame); }
    int currentFrame() const { return d->currentFrame(); }
    void setTrimRange(FrameRange range) { d->setTrimRange(range); }
    FrameRange trimRange() const { return d->trimRange(); }

signals:
    void positionChanged();
    void trimRangeChanged(FrameRange range);

private:
    const std::unique_ptr<TrimAspects> d;
};

TrimWidget::TrimWidget(const ClipInfo &clip, QWidget *parent)
    : QWidget(parent)
    , d(new TrimAspects(clip))
{
    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    if (QWidget *form = AspectWidgets::createAspectForm(d.get()))
        layout->addWidget(form);

    connect(d.get(), &TrimAspects::positionChanged, this, &TrimWidget::positionChanged);
    connect(d.get(), &TrimAspects::trimRangeChanged, this, &TrimWidget::trimRangeChanged);
}

class CropAndTrimDialog : public QDialog
{
public:
    explicit CropAndTrimDialog(const ClipInfo &clip, QWidget *parent = nullptr);

    void setCropRect(const QRect &rect);
    QRect cropRect() const;
    void setTrimRange(FrameRange range);
    FrameRange trimRange() const;

    int currentFrame() const;
    void setCurrentFrame(int frame);

private:
    void onSeekPositionChanged();
    void startFrameFetch();

    ClipInfo m_clipInfo;
    CropWidget *m_cropWidget;
    TrimWidget *m_trimWidget;
    QImage m_previewImage;

    Process *m_process;
    int m_nextFetchFrame = -1;
};

CropAndTrimDialog::CropAndTrimDialog(const ClipInfo &clip, QWidget *parent)
    : QDialog(parent)
    , m_clipInfo(clip)
{
    setWindowTitle(Tr::tr("Crop and Trim"));
    setWindowFlags(Qt::Dialog | Qt::WindowMinMaxButtonsHint); // Make maximizable

    m_cropWidget = new CropWidget;

    m_trimWidget = new TrimWidget(m_clipInfo);

    auto buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);

    using namespace Layouting;
    Column {
        Group {
            title(Tr::tr("Cropping")),
            Column { m_cropWidget },
        },
        Space(16),
        m_trimWidget,
        buttonBox,
    }.attachTo(this);

    m_process = new Process(this);
    connect(m_process, &Process::done, this, [this] {
        if (m_process->exitCode() != 0) {
            FFmpegUtils::reportError(m_process->commandLine(), m_process->rawStdErr());
            return;
        }
        const QByteArray &imageData = m_process->rawStdOut();
        startFrameFetch();
        if (imageData.isEmpty())
            return;
        m_previewImage = QImage(reinterpret_cast<const uchar*>(imageData.constData()),
                                m_clipInfo.dimensions.width(), m_clipInfo.dimensions.height(),
                                QImage::Format_RGB32);
        m_previewImage.detach();
        m_cropWidget->setImage(m_previewImage);
    });
    connect(m_trimWidget, &TrimWidget::positionChanged,
            this, &CropAndTrimDialog::onSeekPositionChanged);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    onSeekPositionChanged();
    resize(1000, 800);
}

void CropAndTrimDialog::onSeekPositionChanged()
{
    // -1, because frame numbers are 0-based
    m_nextFetchFrame = qMin(m_trimWidget->currentFrame(), m_clipInfo.framesCount() - 1);
    if (!m_process->isRunning())
        startFrameFetch();
}

void CropAndTrimDialog::startFrameFetch()
{
    if (m_nextFetchFrame == -1)
        return;

    const CommandLine cl{
        Internal::settings().ffmpegTool(),
        {
            "-v", "error",
            "-ss", m_clipInfo.timeStamp(m_nextFetchFrame),
            "-i", m_clipInfo.file.toUserOutput(),
            "-threads", "1",
            "-frames:v", "1",
            "-f", "rawvideo",
            "-pix_fmt", "bgra",
            "-"
        }
    };
    m_process->close();
    m_nextFetchFrame = -1;
    m_process->setCommand(cl);
    m_process->setWorkingDirectory(Internal::settings().ffmpegTool().parentDir());
    m_process->start();
}

void CropAndTrimDialog::setCropRect(const QRect &rect)
{
    m_cropWidget->setCropRect(rect);
}

QRect CropAndTrimDialog::cropRect() const
{
    return m_cropWidget->cropRect();
}

void CropAndTrimDialog::setTrimRange(FrameRange range)
{
    m_trimWidget->setTrimRange(range);
}

FrameRange CropAndTrimDialog::trimRange() const
{
    return m_trimWidget->trimRange();
}

int CropAndTrimDialog::currentFrame() const
{
    return m_trimWidget->currentFrame();
}

void CropAndTrimDialog::setCurrentFrame(int frame)
{
    m_trimWidget->setCurrentFrame(frame);
}

QString cropAndTrimSummary(const ClipInfo &clip, const QRect &cropRect, FrameRange trimRange)
{
    if (clip.isNull())
        return {};
    const QString cropText = clip.isCompleteArea(cropRect)
                                 ? Tr::tr("Complete area.")
                                 : Tr::tr("Crop to %1x%2px.")
                                       .arg(cropRect.width()).arg(cropRect.height());
    const QString trimText = clip.isCompleteRange(trimRange)
                                 ? Tr::tr("Complete clip.")
                                 : Tr::tr("Frames %1 to %2.")
                                       .arg(trimRange.first).arg(trimRange.second);
    return cropText + " " + trimText;
}

// What the bar asks: one button, and a warning where what it would produce
// cannot be encoded.
class CropAndTrimAspects final : public AspectContainer
{
    Q_OBJECT

public:
    CropAndTrimAspects();

    void setClip(const ClipInfo &clip);
    void showWhatItWouldDo();

    ActionAspect cropAndTrim{this};
    TextDisplay warning{this};

    ClipInfo clipInfo;
    QRect cropRect;
    FrameRange trimRange = {0, 0};
    int currentFrame = 0;

signals:
    void cropRectChanged(const QRect &rect);
    void trimRangeChanged(FrameRange range);
};

CropAndTrimAspects::CropAndTrimAspects()
{
    setAutoApply(true);
    setQmlSource(QUrl("qrc:/qt/qml/QtCreator/ScreenRecorder/CropAndTrimBar.qml"));

    cropAndTrim.setQmlName("CropAndTrim");
    cropAndTrim.setActionText(Tr::tr("Crop and Trim..."));
    cropAndTrim.setAction([this] {
        CropAndTrimDialog dlg(clipInfo, Core::ICore::dialogParent());
        dlg.setCropRect(cropRect);
        dlg.setTrimRange(trimRange);
        dlg.setCurrentFrame(currentFrame);
        if (dlg.exec() != QDialog::Accepted)
            return;
        cropRect = dlg.cropRect();
        trimRange = dlg.trimRange();
        currentFrame = dlg.currentFrame();
        emit cropRectChanged(cropRect);
        emit trimRangeChanged(trimRange);
        showWhatItWouldDo();
    });

    warning.setQmlName("Warning");
    warning.setIconType(InfoType::Warning);
    warning.setText(Tr::tr("Odd crop size."));
    warning.setToolTip(cropSizeWarning());
    warning.setVisible(false);

    showWhatItWouldDo();
}

void CropAndTrimAspects::setClip(const ClipInfo &clip)
{
    // Only a clip of a different size says nothing about the rectangle.
    if (clip.dimensions != clipInfo.dimensions)
        cropRect = {QPoint(), clip.dimensions};
    clipInfo = clip;
    currentFrame = 0;
    trimRange = {currentFrame, clipInfo.framesCount()};
    showWhatItWouldDo();
}

void CropAndTrimAspects::showWhatItWouldDo()
{
    const QString summary = cropAndTrimSummary(clipInfo, cropRect, trimRange);
    if (!summary.isEmpty())
        cropAndTrim.setToolTip(summary);
    // Said rather than drawn as a bare icon: the bar has room for three words,
    // and the whole sentence is still the tool tip.
    warning.setVisible(cropSizeNeedsWarning(cropRect.size()));
}

CropAndTrimWidget::CropAndTrimWidget(QWidget *parent)
    : StyledBar(parent)
    , d(new CropAndTrimAspects)
{
    auto layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    if (QWidget *form = AspectWidgets::createAspectForm(d.get()))
        layout->addWidget(form);

    connect(d.get(), &CropAndTrimAspects::cropRectChanged,
            this, &CropAndTrimWidget::cropRectChanged);
    connect(d.get(), &CropAndTrimAspects::trimRangeChanged,
            this, &CropAndTrimWidget::trimRangeChanged);
}

CropAndTrimWidget::~CropAndTrimWidget() = default;

void CropAndTrimWidget::setClip(const ClipInfo &clip)
{
    d->setClip(clip);
}

#ifdef WITH_TESTS

class TrimTest final : public QObject
{
    Q_OBJECT

private slots:
    void testWhatCanBeTrimmedFromWhereTheReaderIs()
    {
        // Start is only offered where there would still be a clip after it,
        // and End only where there would be one before it - which is what the
        // widget's two buttons said by greying themselves out.
        ClipInfo clip;
        clip.duration = 10;
        clip.rFrameRate = 25;
        TrimAspects trim(clip);
        const int frames = clip.framesCount();
        QCOMPARE(trim.trimRange(), qMakePair(0, frames));

        // The whole clip is kept to begin with, so there is nothing to reset,
        // and nothing before frame zero to end at.
        QVERIFY(!trim.resetTrim.isEnabled());
        QVERIFY2(!trim.setEnd.isEnabled(), "the clip could be ended before it began");
        QVERIFY(trim.setStart.isEnabled());

        trim.setCurrentFrame(100);
        QVERIFY(trim.setStart.isEnabled());
        QVERIFY(trim.setEnd.isEnabled());

        // Ending it here leaves a clip, and now there is something to reset.
        trim.setEnd.triggerAction();
        QCOMPARE(trim.trimRange(), qMakePair(0, 100));
        QVERIFY(trim.resetTrim.isEnabled());

        // The reader is on the end now, so it cannot also be the start.
        QVERIFY2(!trim.setStart.isEnabled(), "the clip could start where it ends");

        trim.resetTrim.triggerAction();
        QCOMPARE(trim.trimRange(), qMakePair(0, frames));
        QVERIFY(!trim.resetTrim.isEnabled());
    }

    void testTheReaderStaysInsideTheClip()
    {
        ClipInfo clip;
        clip.duration = 10;
        clip.rFrameRate = 25;
        TrimAspects trim(clip);

        trim.setCurrentFrame(-5);
        QCOMPARE(trim.currentFrame(), 0);
        trim.setCurrentFrame(99999);
        QCOMPARE(trim.currentFrame(), clip.framesCount());
    }

    void testWhatTheBarSaysItWouldDo()
    {
        // The button's tool tip is the whole answer: what is cropped and what
        // is trimmed, or that neither narrows anything.
        ClipInfo clip;
        clip.duration = 4;
        clip.rFrameRate = 25;
        clip.dimensions = {640, 480};
        const FrameRange whole = {0, clip.framesCount()};

        QCOMPARE(cropAndTrimSummary(clip, {0, 0, 640, 480}, whole),
                 QString("Complete area. Complete clip."));
        QCOMPARE(cropAndTrimSummary(clip, {10, 10, 100, 50}, whole),
                 QString("Crop to 100x50px. Complete clip."));
        QCOMPARE(cropAndTrimSummary(clip, {0, 0, 640, 480}, qMakePair(10, 20)),
                 QString("Complete area. Frames 10 to 20."));

        // A clip that is not there says nothing at all.
        QVERIFY(cropAndTrimSummary({}, {0, 0, 640, 480}, whole).isEmpty());
    }

    void testAnOddCropSizeIsWarnedAbout()
    {
        // Both sides have to be even for the lossy formats, which is what the
        // painted icon appeared for.
        QVERIFY(!cropSizeNeedsWarning({640, 480}));
        QVERIFY(cropSizeNeedsWarning({641, 480}));
        QVERIFY(cropSizeNeedsWarning({640, 481}));
        QVERIFY(cropSizeNeedsWarning({641, 481}));

        ClipInfo clip;
        clip.duration = 4;
        clip.rFrameRate = 25;
        clip.dimensions = {640, 480};
        CropAndTrimAspects bar;
        bar.setClip(clip);
        QVERIFY2(!bar.warning.isVisible(), "an even crop was warned about");

        bar.cropRect = {0, 0, 641, 480};
        bar.showWhatItWouldDo();
        QVERIFY(bar.warning.isVisible());
    }

    void testWhatTheSliderHandsToQml()
    {
        ClipInfo clip;
        clip.duration = 4;
        clip.rFrameRate = 25;
        TrimAspects trim(clip);
        trim.setCurrentFrame(40);
        trim.setEnd.triggerAction();

        const QVariantMap drawn = trim.slider.volatileVariantValue().toMap();
        QCOMPARE(drawn.value("frames").toInt(), clip.framesCount());
        QCOMPARE(drawn.value("current").toInt(), 40);
        QCOMPARE(drawn.value("start").toInt(), 0);
        QCOMPARE(drawn.value("end").toInt(), 40);
    }
};

QObject *createTrimTest()
{
    return new TrimTest;
}

namespace Internal {
Utils::AspectContainer *cropAndTrimBarAspectsForTest()
{
    return new CropAndTrimAspects;
}

Utils::AspectContainer *trimAspectsForTest()
{
    // A clip long enough that a range can be trimmed out of the middle.
    ClipInfo clip;
    clip.duration = 10;
    clip.rFrameRate = 25;
    return new TrimAspects(clip);
}

Utils::AspectContainer *cropAspectsForTest()
{
    return new CropAspects;
}
} // namespace Internal
#endif

} // namespace ScreenRecorder

#include "cropandtrim.moc"
