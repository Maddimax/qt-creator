// Copyright (C) 2023 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "cropandtrim.h"
#include "cropscene.h"

#include <utils/aspects.h>
#include <utils/aspectwidgets.h>
#include <utils/guard.h>

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

class SelectionSlider : public QSlider
{
public:
    explicit SelectionSlider(QWidget *parent = nullptr);

    void setSelectionRange(FrameRange range);

protected:
    void paintEvent(QPaintEvent *) override;

private:
    FrameRange m_range;
};

SelectionSlider::SelectionSlider(QWidget *parent)
    : QSlider(Qt::Horizontal, parent)
    , m_range({-1, -1})
{
    setPageStep(50);
}

void SelectionSlider::setSelectionRange(FrameRange range)
{
    m_range = range;
    update();
}

void SelectionSlider::paintEvent(QPaintEvent *)
{
    QStyleOptionSlider opt;
    initStyleOption(&opt);
    opt.subControls = QStyle::SC_SliderHandle; // Draw only the handle. We draw the rest, here.

    const int tickOffset = style()->pixelMetric(QStyle::PM_SliderTickmarkOffset, &opt, this);
    QRect grooveRect = style()->subControlRect(QStyle::CC_Slider, &opt,
                                               QStyle::SC_SliderGroove, this)
                           .adjusted(tickOffset, 0, -tickOffset, 0);
    grooveRect.setTop(rect().top());
    grooveRect.setBottom(rect().bottom());
    const QColor bgColor = palette().window().color();
    const QColor fgColor = palette().text().color();
    const QColor grooveColor = StyleHelper::mergedColors(bgColor, fgColor, 80);
    const QColor selectionColor = StyleHelper::mergedColors(bgColor, fgColor, 45);
    QPainter p(this);
    p.fillRect(grooveRect, grooveColor);
    const qreal pixelsPerFrame = grooveRect.width() / qreal(maximum());
    const int startPixels = int(m_range.first * pixelsPerFrame);
    const int endPixels = int((maximum() - m_range.second) * pixelsPerFrame);
    p.fillRect(grooveRect.adjusted(startPixels, 0, -endPixels, 0), selectionColor);
    style()->drawComplexControl(QStyle::CC_Slider, &opt, &p, this);
}

class TrimWidget : public QWidget
{
    Q_OBJECT

public:
    explicit TrimWidget(const ClipInfo &clip, QWidget *parent = nullptr);

    void setCurrentFrame(int frame);
    int currentFrame() const;
    void setTrimRange(FrameRange range);
    FrameRange trimRange() const;

signals:
    void positionChanged();
    void trimRangeChanged(FrameRange range);

private:
    void resetTrimRange();
    void updateTrimWidgets();
    void emitTrimRangeChange();

    ClipInfo m_clipInfo;
    SelectionSlider *m_frameSlider;
    TimeLabel *m_currentTime;
    TimeLabel *m_clipDuration;

    struct {
        QPushButton *button;
        TimeLabel *timeLabel;
    } m_trimStart, m_trimEnd;
    TimeLabel *m_trimRange;
    QToolButton *m_trimResetButton;
};

TrimWidget::TrimWidget(const ClipInfo &clip, QWidget *parent)
    : QWidget(parent)
    , m_clipInfo(clip)
{
    m_frameSlider = new SelectionSlider;

    m_currentTime = new TimeLabel(m_clipInfo);

    m_clipDuration = new TimeLabel(m_clipInfo);

    m_trimStart.button = new QPushButton(Tr::tr("Start:"));
    m_trimStart.timeLabel = new TimeLabel(m_clipInfo);

    m_trimEnd.button = new QPushButton(Tr::tr("End:"));
    m_trimEnd.timeLabel = new TimeLabel(m_clipInfo);

    m_trimRange = new TimeLabel(m_clipInfo);

    m_trimResetButton = new QToolButton;
    m_trimResetButton->setIcon(Icons::RESET.icon());

    using namespace Layouting;
    Column {
        Row { m_frameSlider, m_currentTime, QString("/"), m_clipDuration },
        Group {
            title(Tr::tr("Trimming")),
            Row {
                m_trimStart.button, m_trimStart.timeLabel,
                Space(20),
                m_trimEnd.button, m_trimEnd.timeLabel,
                st, Space(20),
                Tr::tr("Range:"), m_trimRange,
                m_trimResetButton,
            },
        },
        noMargin,
    }.attachTo(this);

    connect(m_frameSlider, &QSlider::valueChanged, this, [this] {
        m_currentTime->setFrame(currentFrame());
        updateTrimWidgets();
        emit positionChanged();
    });
    connect(m_trimStart.button, &QPushButton::clicked, this, [this] (){
        m_trimStart.timeLabel->setFrame(currentFrame());
        updateTrimWidgets();
        emitTrimRangeChange();
    });
    connect(m_trimEnd.button, &QPushButton::clicked, this, [this] (){
        m_trimEnd.timeLabel->setFrame(currentFrame());
        updateTrimWidgets();
        emitTrimRangeChange();
    });
    connect(m_trimResetButton, &QToolButton::clicked, this, &TrimWidget::resetTrimRange);

    m_frameSlider->setMaximum(m_clipInfo.framesCount());
    m_currentTime->setFrame(currentFrame());
    m_clipDuration->setFrame(m_clipInfo.framesCount());
    resetTrimRange();
}

void TrimWidget::setCurrentFrame(int frame)
{
    m_frameSlider->setValue(frame);
}

int TrimWidget::currentFrame() const
{
    return m_frameSlider->value();
}

void TrimWidget::setTrimRange(FrameRange range)
{
    m_trimStart.timeLabel->setFrame(range.first);
    m_trimEnd.timeLabel->setFrame(range.second);
    m_frameSlider->setSelectionRange(trimRange());
}

FrameRange TrimWidget::trimRange() const
{
    return { m_trimStart.timeLabel->frame(), m_trimEnd.timeLabel->frame() };
}

void TrimWidget::resetTrimRange()
{
    setTrimRange({0, m_clipInfo.framesCount()});
    emitTrimRangeChange();
    updateTrimWidgets();
}

void TrimWidget::updateTrimWidgets()
{
    const int current = currentFrame();
    const int trimStart = m_trimStart.timeLabel->frame();
    const int trimEnd = m_trimEnd.timeLabel->frame();
    m_trimStart.button->setEnabled(current < m_clipInfo.framesCount() && current < trimEnd);
    m_trimEnd.button->setEnabled(current > 0 && current > trimStart);
    m_trimRange->setFrame(trimEnd - trimStart);
    m_frameSlider->setSelectionRange(trimRange());
    m_trimResetButton->setEnabled(!m_clipInfo.isCompleteRange(trimRange()));
}

void TrimWidget::emitTrimRangeChange()
{
    emit trimRangeChanged(trimRange());
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

CropAndTrimWidget::CropAndTrimWidget(QWidget *parent)
    : StyledBar(parent)
{
    m_button = new QToolButton;
    m_button->setText(Tr::tr("Crop and Trim..."));

    m_cropSizeWarningIcon = new CropSizeWarningIcon(CropSizeWarningIcon::ToolBarVariant);

    using namespace Layouting;
    Row {
        m_button,
        m_cropSizeWarningIcon,
        noMargin, spacing(0),
    }.attachTo(this);

    connect(m_button, &QPushButton::clicked, this, [this] {
        CropAndTrimDialog dlg(m_clipInfo, Core::ICore::dialogParent());
        dlg.setCropRect(m_cropRect);
        dlg.setTrimRange(m_trimRange);
        dlg.setCurrentFrame(m_currentFrame);
        if (dlg.exec() == QDialog::Accepted) {
            m_cropRect = dlg.cropRect();
            m_trimRange = dlg.trimRange();
            m_currentFrame = dlg.currentFrame();
            emit cropRectChanged(m_cropRect);
            emit trimRangeChanged(m_trimRange);
            updateWidgets();
        }
    });

    updateWidgets();
}

void CropAndTrimWidget::setClip(const ClipInfo &clip)
{
    if (clip.dimensions != m_clipInfo.dimensions)
        m_cropRect = {QPoint(), clip.dimensions}; // Reset only if clip size changed
    m_clipInfo = clip;
    m_currentFrame = 0;
    m_trimRange = {m_currentFrame, m_clipInfo.framesCount()};
    updateWidgets();
}

void CropAndTrimWidget::updateWidgets()
{
    if (!m_clipInfo.isNull()) {
        const QString cropText =
            !m_clipInfo.isCompleteArea(m_cropRect)
                ? Tr::tr("Crop to %1x%2px.").arg(m_cropRect.width()).arg(m_cropRect.height())
                : Tr::tr("Complete area.");

        const QString trimText =
            !m_clipInfo.isCompleteRange(m_trimRange)
                ? Tr::tr("Frames %1 to %2.").arg(m_trimRange.first).arg(m_trimRange.second)
                : Tr::tr("Complete clip.");

        m_button->setToolTip(cropText + " " + trimText);
    }

    m_cropSizeWarningIcon->setCropSize(m_cropRect.size());
}


#ifdef WITH_TESTS
namespace Internal {
Utils::AspectContainer *cropAspectsForTest()
{
    return new CropAspects;
}
} // namespace Internal
#endif

} // namespace ScreenRecorder

#include "cropandtrim.moc"
