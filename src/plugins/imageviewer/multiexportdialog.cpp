// Copyright (C) 2018 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "multiexportdialog.h"

#include "exportdialog.h"
#include "imageview.h" // ExportData
#include "imageviewertr.h"

#include <coreplugin/dialogs/ioptionspage.h>
#include <coreplugin/icore.h>

#include <utils/aspects.h>
#include <utils/pathchooser.h>
#include <utils/stringutils.h>
#include <utils/utilsicons.h>

#ifdef WITH_TESTS
#include <QTest>
#endif

#include <QDialogButtonBox>
#include <QMessageBox>
#include <QVBoxLayout>

using namespace Utils;

namespace ImageViewer::Internal {

static const int standardIconSizesValues[] = {16, 24, 32, 48, 64, 128, 256};

// Helpers to convert a size specifications from QString to QSize
// and vv. The format is '2x4' or '4' as shortcut for '4x4'.
static QSize sizeFromString(const QString &r)
{
    if (r.isEmpty())
        return {};
    const int xPos = r.indexOf('x');
    bool ok;
    const int width = xPos < 0
        ? r.toInt(&ok)
        : r.left(xPos).toInt(&ok);
    if (!ok || width <= 0)
        return {};
    if (xPos < 0)
        return {width, width};
    const int height = r.mid(xPos + 1).toInt(&ok);
    if (!ok || height <= 0)
        return {};
    return {width, height};
}

static void appendSizeSpec(const QSize &size, QString *target)
{
    target->append(QString::number(size.width()));
    if (size.width() != size.height()) {
        target->append('x');
        target->append(QString::number(size.height()));
    }
}

static inline QString sizeToString(const QSize &size)
{
    QString result;
    appendSizeSpec(size, &result);
    return result;
}

static QString sizesToString(const QVector<QSize> &sizes)
{
    QString result;
    for (int i = 0, size = sizes.size(); i < size; ++i) {
        if (i)
            result.append(',');
        appendSizeSpec(sizes.at(i), &result);
    }
    return result;
}

static QVector<QSize> stringToSizes(const QString &s)
{
    QVector<QSize> result;
    const QString trimmed = s.trimmed();
    const QStringList &sizes = trimmed.split(',', Qt::SkipEmptyParts);
    result.reserve(sizes.size());
    for (const QString &sizeSpec : sizes) {
        const QSize size = sizeFromString(sizeSpec);
        if (!size.isValid() || size.isEmpty())
            return {};
        else
            result.append(size);
    }
    return result;
}

static FilePath fileNameForSize(QString pattern, const QSize &s)
{
    pattern.replace("%1", QString::number(s.width()));
    pattern.replace("%2", QString::number(s.height()));
    return FilePath::fromString(pattern);
}

// Helpers for writing/reading the user-specified size specifications
// from/to the settings.
static Key settingsGroup() { return "ExportSvgSizes"; }

static QVector<QSize> readSettings(const QSize &size)
{
    QVector<QSize> result;
    QtcSettings *settings = Core::ICore::settings();
    settings->beginGroup(settingsGroup());
    const QStringList keys = settings->allKeys();
    const int idx = keys.indexOf(sizeToString(size));
    if (idx >= 0)
        result = stringToSizes(settings->value(keyFromString(keys.at(idx))).toString());
    settings->endGroup();
    return result;
}

static void writeSettings(const QSize &size, const QString &sizeSpec)
{
    QtcSettings *settings = Core::ICore::settings();
    settings->beginGroup(settingsGroup());
    const QString spec = sizeToString(size);
    settings->setValue(keyFromString(spec), QVariant(sizeSpec));

    // Limit the number of sizes to 10. Remove the
    // first element unless it is the newly added spec.
    QStringList keys = settings->allKeys();
    while (keys.size() > 10) {
        const int existingIndex = keys.indexOf(spec);
        const int removeIndex = existingIndex == 0 ? 1 : 0;
        settings->remove(keyFromString(keys.takeAt(removeIndex)));
    }
    settings->endGroup();
}

// The sizes offered for an image that is not an icon: half of it where that
// is still worth having, itself, and then doublings until there are four.
QVector<QSize> generatedSizes(const QSize &svgSize)
{
    QVector<QSize> sizes;
    if (svgSize.width() >= 16)
        sizes.append(svgSize / 2);
    sizes.append(svgSize);
    for (int factor = 2; sizes.size() < 4; factor *= 2)
        sizes.append(svgSize * factor);
    return sizes;
}

// Where the width and the height go in the name. The caller passes a name for
// one image and gets one per size out of it, so the name has to say where the
// numbers belong.
FilePath withSizePlaceholder(const FilePath &filePath)
{
    FilePath f = filePath;
    QString ff = f.path();
    const int lastDot = ff.lastIndexOf('.');
    if (lastDot != -1) {
        ff.insert(lastDot, "-%1");
        f = f.withNewPath(ff);
    }
    return f;
}

QVector<QSize> MultiExportDialog::standardIconSizes()
{
    QVector<QSize> result;
    const int size = int(sizeof(standardIconSizesValues) / sizeof(standardIconSizesValues[0]));
    result.reserve(size);
    for (int standardIconSizesValue : standardIconSizesValues)
        result.append(QSize(standardIconSizesValue, standardIconSizesValue));
    return result;
}

// --- MultiExportDialog

// What the menu on the size field offers.
enum class SizeChoice { Clear, StandardIcons, Generated };

class MultiExportSettings final : public AspectContainer
{
public:
    MultiExportSettings()
    {
        setAutoApply(true);
        setQmlSource(QUrl("qrc:/qt/qml/QtCreator/ImageViewer/MultiExportDialog.qml"));

        const QString fileToolTip
            = Tr::tr("Enter a file name containing place holders %1 "
                     "which will be replaced by the width and height of the image, respectively.")
                  .arg("%1, %2");
        file.setQmlName("File");
        file.setLabelText(Tr::tr("File:"));
        file.setToolTip(fileToolTip);
        file.setExpectedKind(PathChooserKind::SaveFile);
        file.setPromptDialogFilter(ExportDialog::imageNameFilterString());

        sizes.setQmlName("Sizes");
        sizes.setLabelText(Tr::tr("Sizes:"));
        sizes.setDisplayStyle(StringAspect::LineEditDisplay);
        sizes.setToolTip(Tr::tr(
            "A comma-separated list of size specifications of the form \"<width>x<height>\"."));

        // The widget field carried a menu on a button inside itself. An action
        // that offers choices is the same thing said once.
        sizeOptions.setQmlName("SizeOptions");
        sizeOptions.setActionText(Tr::tr("Sizes"));
        sizeOptions.setActionIcon(Icons::ARROW_DOWN.icon());
        sizeOptions.setChoices({
            {.display = Tr::tr("Clear"), .id = int(SizeChoice::Clear)},
            {.display = Tr::tr("Set Standard Icon Sizes"), .id = int(SizeChoice::StandardIcons)},
            {.display = Tr::tr("Generate Sizes"), .id = int(SizeChoice::Generated)},
        });
    }

    void setSizes(const QVector<QSize> &s) { sizes.setValue(sizesToString(s)); }
    QVector<QSize> chosenSizes() const { return stringToSizes(sizes().trimmed()); }

    FilePathAspect file{this};
    StringAspect sizes{this};
    ActionAspect sizeOptions{this};
};

MultiExportDialog::MultiExportDialog(QWidget *parent)
    : QDialog(parent)
    , m_settings(new MultiExportSettings)
{
    auto buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto layout = new QVBoxLayout(this);
    layout->addWidget(Core::createAspectForm(m_settings.get()));
    layout->addWidget(buttonBox);

    m_settings->sizeOptions.setOnChoice([this](const QVariant &id) {
        switch (SizeChoice(id.toInt())) {
        case SizeChoice::Clear: m_settings->sizes.setValue(QString()); break;
        case SizeChoice::StandardIcons: setStandardIconSizes(); break;
        case SizeChoice::Generated: setGeneratedSizes(); break;
        }
    });
}

MultiExportDialog::~MultiExportDialog() = default;

void MultiExportDialog::setSizes(const QVector<QSize> &s)
{
    m_settings->setSizes(s);
}

QVector<QSize> MultiExportDialog::sizes() const
{
    return m_settings->chosenSizes();
}

void MultiExportDialog::setStandardIconSizes()
{
    setSizes(standardIconSizes());
}

void MultiExportDialog::setGeneratedSizes()
{
    setSizes(generatedSizes(m_svgSize));
}

void MultiExportDialog::suggestSizes()
{
    const QVector<QSize> settingsEntries = readSettings(m_svgSize);
    if (!settingsEntries.isEmpty())
        setSizes(settingsEntries);
    else if (m_svgSize.width() == m_svgSize.height()) // Square: Assume this is an icon
        setStandardIconSizes();
    else
        setGeneratedSizes();
}

QVector<ExportData> MultiExportDialog::exportData() const
{
    const QVector<QSize> sizeList = sizes();
    const QString pattern = exportFileName().toUrlishString();
     QVector<ExportData> result;
     result.reserve(sizeList.size());
     for (const QSize &s : sizeList)
         result.append({fileNameForSize(pattern, s), s});
     return result;
}

QString MultiExportDialog::sizesSpecification() const
{
    return m_settings->sizes().trimmed();
}

void MultiExportDialog::accept()
{
    if (!m_settings->file.isValid()) {
        QMessageBox::warning(this, windowTitle(),
                             m_settings->file.validationMessage(m_settings->file.value()));
        return;
    }

    const QString &sizeSpec = sizesSpecification();
    if (sizeSpec.isEmpty()) {
        QMessageBox::warning(this, windowTitle(), Tr::tr("Please specify some sizes."));
        return;
    }

    const QVector<ExportData> &data = exportData();
    if (data.isEmpty()) {
        QMessageBox::warning(this, windowTitle(),
                             Tr::tr("Invalid size specification: %1").arg(sizeSpec));
        return;
    }
    if (data.size() > 1 && data.at(0).filePath == data.at(1).filePath) {
        QMessageBox::warning(this, windowTitle(),
                             Tr::tr("The file name must contain one of the placeholders %1, %2.")
                                .arg(QString("%1"), QString("%2")));
        return;
    }

    writeSettings(m_svgSize, sizeSpec);

    FilePaths existingFiles;
    for (const ExportData &d : data) {
        if (d.filePath.exists())
            existingFiles.append(d.filePath);
    }
    if (!existingFiles.isEmpty()) {
        const QString message = existingFiles.size() == 1
            ? Tr::tr("The file %1 already exists.\nWould you like to overwrite it?")
                .arg(existingFiles.constFirst().toUserOutput())
            : Tr::tr("The files %1 already exist.\nWould you like to overwrite them?")
                .arg(existingFiles.toUserOutput(", "));
        QMessageBox messageBox(QMessageBox::Question, windowTitle(), message,
                               QMessageBox::Yes | QMessageBox::No, this);
        if (messageBox.exec() != QMessageBox::Yes)
            return;
    }

    QDialog::accept();
}

FilePath MultiExportDialog::exportFileName() const
{
    return m_settings->file();
}

void MultiExportDialog::setExportFileName(const FilePath &filePath)
{
    m_settings->file.setValue(withSizePlaceholder(filePath));
}

#ifdef WITH_TESTS

class MultiExportDialogTest final : public QObject
{
    Q_OBJECT

private slots:
    void testTheDialogDrawsWithTheQmlItNames()
    {
        MultiExportSettings settings;
        const Result<> rendered
            = Core::aspectFormRenders(&settings, "MultiExportDialog.qml");
        QVERIFY2(rendered, qPrintable(rendered ? QString() : rendered.error()));
    }

    void testHowASizeIsWritten()
    {
        // "4" is 4x4, because a list of icon sizes is mostly squares and
        // writing each of them twice reads worse.
        QCOMPARE(sizeFromString("4"), QSize(4, 4));
        QCOMPARE(sizeFromString("2x4"), QSize(2, 4));
        QCOMPARE(sizeToString({4, 4}), QString("4"));
        QCOMPARE(sizeToString({2, 4}), QString("2x4"));

        // Nothing that is not a size at all.
        QVERIFY(!sizeFromString("").isValid());
        QVERIFY(!sizeFromString("x").isValid());
        QVERIFY(!sizeFromString("0").isValid());
        QVERIFY(!sizeFromString("-4").isValid());
        QVERIFY(!sizeFromString("4x0").isValid());
        QVERIFY(!sizeFromString("wide").isValid());
    }

    void testOneBadSizeSpoilsTheList()
    {
        // All or nothing, so that a typo cannot quietly export fewer images
        // than were asked for.
        QCOMPARE(stringToSizes("16,32,48").size(), 3);
        QCOMPARE(stringToSizes("16, 32x64 ,48").size(), 3);
        QVERIFY2(stringToSizes("16,nonsense,48").isEmpty(),
                 "a list with one bad entry exported the other two");
        QVERIFY(stringToSizes("").isEmpty());

        // And a list survives being written out and read back.
        const QVector<QSize> sizes = {{16, 16}, {32, 64}};
        QCOMPARE(stringToSizes(sizesToString(sizes)), sizes);
    }

    void testWhatSizesAreOfferedForAnImage()
    {
        // Half of it, itself, and doublings until there are four.
        QCOMPARE(generatedSizes({64, 64}),
                 (QVector<QSize>{{32, 32}, {64, 64}, {128, 128}, {256, 256}}));

        // Below 16 there is no half worth having, so it starts at itself.
        const QVector<QSize> small = generatedSizes({8, 8});
        QCOMPARE(small.size(), 4);
        QCOMPARE(small.first(), QSize(8, 8));

        // The proportions are kept, whatever they are.
        const QVector<QSize> wide = generatedSizes({64, 32});
        QCOMPARE(wide.first(), QSize(32, 16));
    }

    void testTheNameSaysWhereTheNumbersGo()
    {
        // One name in, one image per size out - so the name has to carry the
        // width and the height, and the placeholder goes before the suffix.
        QCOMPARE(withSizePlaceholder(FilePath::fromString("/tmp/icon.png")),
                 FilePath::fromString("/tmp/icon-%1.png"));
        // No suffix, nowhere to put it before.
        QCOMPARE(withSizePlaceholder(FilePath::fromString("/tmp/icon")),
                 FilePath::fromString("/tmp/icon"));

        QCOMPARE(fileNameForSize("/tmp/icon-%1x%2.png", {16, 32}),
                 FilePath::fromString("/tmp/icon-16x32.png"));
    }

    void testTheFieldOffersTheThreeWaysToFillIt()
    {
        // The widget field carried them on a button inside itself; they are
        // the action's choices now, and all three have to be there.
        MultiExportSettings settings;
        const QList<AspectPresentation::Choice> choices = settings.sizeOptions.presentation().choices;
        QCOMPARE(choices.size(), 3);
        QCOMPARE(choices.at(0).id.toInt(), int(SizeChoice::Clear));
        QCOMPARE(choices.at(1).id.toInt(), int(SizeChoice::StandardIcons));
        QCOMPARE(choices.at(2).id.toInt(), int(SizeChoice::Generated));
    }

    void testEachWayOfFillingItDoesSomethingDifferent()
    {
        MultiExportDialog dialog;
        dialog.setSvgSize({64, 64});

        dialog.setSizes(MultiExportDialog::standardIconSizes());
        const QVector<QSize> standard = dialog.sizes();
        QVERIFY(standard.contains(QSize(16, 16)));
        QVERIFY(standard.contains(QSize(256, 256)));

        dialog.setSizes(generatedSizes({64, 64}));
        QCOMPARE(dialog.sizes().size(), 4);
        QVERIFY2(dialog.sizes() != standard, "generating sizes gave the standard icon sizes");

        dialog.setSizes({});
        QVERIFY(dialog.sizes().isEmpty());
    }

    void testASquareImageIsTakenForAnIcon()
    {
        // suggestSizes() has three answers and no settings to read here, so
        // what is left is the square/not-square choice.
        MultiExportDialog square;
        square.setSvgSize({64, 64});
        square.suggestSizes();
        QCOMPARE(square.sizes(), MultiExportDialog::standardIconSizes());

        MultiExportDialog wide;
        wide.setSvgSize({64, 32});
        wide.suggestSizes();
        QCOMPARE(wide.sizes(), generatedSizes({64, 32}));
    }

    void testTheFileIsOneToWriteTo()
    {
        MultiExportSettings settings;
        QCOMPARE(settings.file.presentation().pathKind, AspectControls::PathKind::SaveFile);
        QVERIFY(!settings.file.presentation().promptDialogFilter.isEmpty());
    }
};

QObject *createMultiExportDialogTest()
{
    return new MultiExportDialogTest;
}

#endif // WITH_TESTS

} // ImageViewer:Internal

#ifdef WITH_TESTS
#include "multiexportdialog.moc"
#endif
