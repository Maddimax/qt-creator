// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "exportdialog.h"

#include "imageview.h" // ExportData
#include "imageviewertr.h"

#include <coreplugin/dialogs/ioptionspage.h>

#include <utils/aspects.h>
#include <utils/guard.h>
#include <utils/pathchooser.h>
#include <utils/utilsicons.h>

#ifdef WITH_TESTS
#include <QTest>
#endif

#include <QDialogButtonBox>
#include <QImageWriter>
#include <QMessageBox>
#include <QMimeDatabase>
#include <QVBoxLayout>

using namespace Utils;

namespace ImageViewer::Internal {

enum { exportMinimumSize = 1, exportMaximumSize = 2000 };

QString ExportDialog::imageNameFilterString()
{
    static QString result;
    if (result.isEmpty()) {
        QMimeDatabase mimeDatabase;
        const QString separator = ";;";
        const QList<QByteArray> mimeTypes = QImageWriter::supportedMimeTypes();
        for (const QByteArray &mimeType : mimeTypes) {
            const QString filter = mimeDatabase.mimeTypeForName(QLatin1String(mimeType)).filterString();
            if (!filter.isEmpty()) {
                if (mimeType == QByteArrayLiteral("image/png")) {
                    if (!result.isEmpty())
                        result.prepend(separator);
                    result.prepend(filter);
                } else {
                    if (!result.isEmpty())
                        result.append(separator);
                    result.append(filter);
                }
            }
        }
    }
    return result;
}

// The other side of a size the user typed, keeping the image's proportions.
// The widget version had a branch for a square, so that one side could not
// drift from the other by a pixel; a square's ratio is exactly 1.0 and
// dividing by it is exact, so the branch never changed an answer.
int heightForWidth(int width, const QSize &defaultSize)
{
    const qreal ratio = qreal(defaultSize.width()) / qreal(defaultSize.height());
    return qRound(qreal(width) / ratio);
}

int widthForHeight(int height, const QSize &defaultSize)
{
    const qreal ratio = qreal(defaultSize.width()) / qreal(defaultSize.height());
    return qRound(qreal(height) * ratio);
}

class ExportSettings final : public AspectContainer
{
public:
    ExportSettings()
    {
        setAutoApply(true);
        setQmlSource(QUrl("qrc:/qt/qml/QtCreator/ImageViewer/ExportDialog.qml"));

        file.setQmlName("File");
        file.setLabelText(Tr::tr("File:"));
        file.setExpectedKind(PathChooserKind::SaveFile);
        file.setPromptDialogFilter(ExportDialog::imageNameFilterString());

        width.setQmlName("Width");
        width.setLabelText(Tr::tr("Size:"));
        width.setRange(exportMinimumSize, exportMaximumSize);

        height.setQmlName("Height");
        //: Multiplication, as in 32x32
        height.setLabelText(Tr::tr("x"));
        height.setRange(exportMinimumSize, exportMaximumSize);

        reset.setQmlName("Reset");
        reset.setActionText(Tr::tr("Reset"));
        reset.setActionIcon(Icons::RESET.icon());
        reset.setAction([this] { setSize(m_defaultSize); });

        // Each side follows the other, and the guard is what stops the two
        // from answering each other for ever - the widget dialog blocked the
        // spin box's signals for the same reason.
        connect(&width, &BaseAspect::changed, this, [this] {
            if (m_updating.isLocked())
                return;
            const Utils::GuardLocker lock(m_updating);
            height.setValue(heightForWidth(width(), m_defaultSize));
        });
        connect(&height, &BaseAspect::changed, this, [this] {
            if (m_updating.isLocked())
                return;
            const Utils::GuardLocker lock(m_updating);
            width.setValue(widthForHeight(height(), m_defaultSize));
        });
    }

    // The size the image came in at, which is what Reset goes back to and what
    // the proportions are taken from.
    void setDefaultSize(const QSize &size)
    {
        m_defaultSize = size;
        setSize(size);
    }

    void setSize(const QSize &size)
    {
        const Utils::GuardLocker lock(m_updating);
        width.setValue(size.width());
        height.setValue(size.height());
    }

    QSize size() const { return {int(width()), int(height())}; }

    FilePathAspect file{this};
    IntegerAspect width{this};
    IntegerAspect height{this};
    ActionAspect reset{this};

private:
    QSize m_defaultSize;
    Utils::Guard m_updating;
};

ExportDialog::ExportDialog(QWidget *parent)
    : QDialog(parent)
    , m_settings(new ExportSettings)
{
    auto buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto layout = new QVBoxLayout(this);
    layout->addWidget(Core::createAspectForm(m_settings.get()));
    layout->addWidget(buttonBox);
}

ExportDialog::~ExportDialog() = default;

void ExportDialog::accept()
{
    if (!m_settings->file.isValid()) {
        QMessageBox::warning(this, windowTitle(),
                             m_settings->file.validationMessage(m_settings->file.value()));
        return;
    }
    const FilePath filePath = exportFileName();
    if (filePath.exists()) {
        const QString question = Tr::tr("%1 already exists.\nWould you like to overwrite it?")
            .arg(filePath.toUserOutput());
        if (QMessageBox::question(this, windowTitle(), question, QMessageBox::Yes | QMessageBox::No) !=  QMessageBox::Yes)
            return;
    }
    QDialog::accept();
}

QSize ExportDialog::exportSize() const
{
    return m_settings->size();
}

void ExportDialog::setExportSize(const QSize &size)
{
    m_settings->setDefaultSize(size);
}

FilePath ExportDialog::exportFileName() const
{
    return m_settings->file();
}

void ExportDialog::setExportFileName(const FilePath &f)
{
    m_settings->file.setValue(f);
}

ExportData ExportDialog::exportData() const
{
    return {exportFileName(), exportSize()};
}

#ifdef WITH_TESTS

class ExportDialogTest final : public QObject
{
    Q_OBJECT

private slots:
    void testTheDialogDrawsWithTheQmlItNames()
    {
        ExportSettings settings;
        const Result<> rendered = Core::aspectFormRenders(&settings, "ExportDialog.qml");
        QVERIFY2(rendered, qPrintable(rendered ? QString() : rendered.error()));
    }

    void testTheOtherSideOfASizeKeepsTheProportions()
    {
        const QSize wide(1600, 900);
        QCOMPARE(heightForWidth(1600, wide), 900);
        QCOMPARE(heightForWidth(800, wide), 450);
        QCOMPARE(widthForHeight(450, wide), 800);

        const QSize tall(600, 800);
        QCOMPARE(heightForWidth(300, tall), 400);
        QCOMPARE(widthForHeight(400, tall), 300);
    }

    void testASquareStaysSquareExactly()
    {
        // Dividing and rounding lets one side drift from the other by a pixel,
        // which on an icon is the difference between 32x32 and 32x31.
        const QSize square(32, 32);
        for (int side = 1; side <= 64; ++side) {
            QCOMPARE(heightForWidth(side, square), side);
            QCOMPARE(widthForHeight(side, square), side);
        }
    }

    void testTypingOneSideMovesTheOther()
    {
        ExportSettings settings;
        settings.setDefaultSize({1600, 900});
        QCOMPARE(settings.size(), QSize(1600, 900));

        settings.width.setValue(800);
        QCOMPARE(settings.size(), QSize(800, 450));

        settings.height.setValue(900);
        QCOMPARE(settings.size(), QSize(1600, 900));
    }

    void testTheTwoSidesDoNotAnswerEachOtherForEver()
    {
        // Each follows the other, so without a guard setting one would set the
        // other, which would set the first again. The widget dialog blocked
        // the spin box's signals; this holds a Utils::Guard.
        ExportSettings settings;
        settings.setDefaultSize({1000, 300});

        // A width that does not come back from its own height is the case
        // that matters: 101 gives 30, and 30 gives 100. Unguarded, the second
        // handler overwrites the number that was just typed.
        settings.width.setValue(101);
        QCOMPARE(settings.height(), 30);
        QCOMPARE(settings.width(), 101);

        settings.height.setValue(31);
        QCOMPARE(settings.width(), 103);
        QCOMPARE(settings.height(), 31);
    }

    void testResetGoesBackToTheImagesOwnSize()
    {
        ExportSettings settings;
        settings.setDefaultSize({640, 480});
        settings.width.setValue(320);
        QCOMPARE(settings.size(), QSize(320, 240));

        settings.reset.triggerAction();
        QCOMPARE(settings.size(), QSize(640, 480));
    }

    void testTheFileIsOneToWriteToNotOneToOpen()
    {
        ExportSettings settings;
        QCOMPARE(settings.file.presentation().pathKind, AspectControls::PathKind::SaveFile);
        QVERIFY2(!settings.file.presentation().promptDialogFilter.isEmpty(),
                 "the chooser offers every file rather than the images that can be written");
    }

    void testNeitherSideMayBeZero()
    {
        // A zero-sized export is not an image. The widget spin boxes had the
        // same bounds.
        ExportSettings settings;
        QCOMPARE(settings.width.presentation().minimum.toInt(), int(exportMinimumSize));
        QCOMPARE(settings.width.presentation().maximum.toInt(), int(exportMaximumSize));
        QCOMPARE(settings.height.presentation().minimum.toInt(), int(exportMinimumSize));
    }
};

QObject *createExportDialogTest()
{
    return new ExportDialogTest;
}

#endif // WITH_TESTS

} // ImageViewer::Internal

#ifdef WITH_TESTS
#include "exportdialog.moc"
#endif
