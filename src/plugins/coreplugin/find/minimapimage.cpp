// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "minimapimage.h"

#include <QFontMetricsF>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextLayout>
#include <QtMath>

#ifdef WITH_TESTS
#include <QTest>
#endif

namespace Core {

static inline void updatePixel(quint32 *dst, QRgb bgPremul, const QColor &fg, bool blend = false)
{
    if (!blend) {
        *dst = bgPremul;
        return;
    }

    // Convert the foreground colour to premultiplied ARGB32 once.
    const QRgb fgPremul = qPremultiply(fg.rgba());

    const quint32 aF = (fgPremul >> 24) & 0xff;
    if (aF == 0) {
        *dst = bgPremul; // fully transparent foreground -> keep bg
        return;
    }
    if (aF == 255) {
        *dst = fgPremul; // fully opaque foreground -> replace bg
        return;
    }

    const quint32 aB = (bgPremul >> 24) & 0xff;

    // src‑over (premultiplied)
    const quint32 aOut = aF + aB * (255 - aF) / 255;
    const quint32 rOut = ((fgPremul >> 16) & 0xff) + ((bgPremul >> 16) & 0xff) * (255 - aF) / 255;
    const quint32 gOut = ((fgPremul >> 8) & 0xff) + ((bgPremul >> 8) & 0xff) * (255 - aF) / 255;
    const quint32 bOut = (fgPremul & 0xff) + (bgPremul & 0xff) * (255 - aF) / 255;

    *dst = (aOut << 24) | (rOut << 16) | (gOut << 8) | bOut;
}

QImage renderMinimap(const QTextDocument *document,
                     const MinimapMetrics &metrics,
                     const QFont &font,
                     const QColor &defaultTextColor,
                     int extraLines,
                     const MinimapBlockColor &overrideColor)
{
    if (!document)
        return {};

    int docLineCount = extraLines;
    for (QTextBlock block = document->firstBlock(); block.isValid(); block = block.next()) {
        if (block.isVisible())
            ++docLineCount;
    }

    const int imageWidth = metrics.width - 2;
    const int lineDup = metrics.pixelsPerLine;

    const int imageHeight = metrics.lineHeight() * docLineCount;
    if (imageWidth <= 0 || imageHeight <= 0)
        return {};

    QImage img(imageWidth, imageHeight, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    const QColor bgColor = QColor(Qt::transparent);
    const quint32 bgPremul = qPremultiply(bgColor.rgba());

    const QFontMetricsF fm(font);
    const int spaceWidth = qCeil(fm.horizontalAdvance(QLatin1Char(' ')));

    quint32 *dstLine = reinterpret_cast<quint32 *>(img.bits());

    auto advanceToNextLine = [&]() {
        for (int i = 1; i < lineDup; ++i)
            memcpy(dstLine + i * imageWidth, dstLine, imageWidth * sizeof(quint32));

        dstLine += lineDup * imageWidth;

        memset(dstLine, bgPremul, metrics.lineGap * imageWidth * sizeof(quint32));
        dstLine += metrics.lineGap * imageWidth;
    };

    for (QTextBlock block = document->firstBlock(); block.isValid(); block = block.next()) {
        if (!block.isVisible())
            continue;

        const QTextLayout *layout = block.layout();
        const std::optional<QColor> blockColor = overrideColor ? overrideColor(block)
                                                               : std::nullopt;
        QColor curFg = blockColor.value_or(defaultTextColor);
        QVector<QTextLayout::FormatRange> formats;
        if (!blockColor.has_value()) {
            formats = layout->formats();
            std::sort(
                formats.begin(),
                formats.end(),
                [](const QTextLayout::FormatRange &a, const QTextLayout::FormatRange &b) {
                    return a.start < b.start;
                });
        }

        const QString text = block.text();

        int formatIndex = 0;
        int currentFormatEnd = formats.isEmpty() ? block.length() : formats.first().start;

        int dstX = 0;
        for (int i = 0; i < text.length() && dstX < imageWidth; ++i) {
            if (i >= currentFormatEnd) {
                if (formatIndex >= formats.size()) {
                    currentFormatEnd = block.length();
                    curFg = defaultTextColor;
                } else {
                    const QTextLayout::FormatRange &format = formats[formatIndex];
                    if (i < format.start) {
                        currentFormatEnd = format.start;
                        curFg = defaultTextColor;
                    } else {
                        const QTextCharFormat &fmt = format.format;
                        if (fmt.foreground().style() != Qt::NoBrush)
                            curFg = fmt.foreground().color();
                        else
                            curFg = defaultTextColor;

                        currentFormatEnd = format.start + format.length;
                        ++formatIndex;
                    }
                }
            }
            QChar ch = text.at(i);
            if (ch.isSpace()) {
                updatePixel(&dstLine[dstX], bgPremul, curFg, false);
                ++dstX;
                continue;
            }

            if (ch == QLatin1Char('\t')) {
                int nextTabX = ((dstX * spaceWidth) / spaceWidth + 1) * spaceWidth;
                while (dstX < imageWidth && dstX < nextTabX) {
                    updatePixel(&dstLine[dstX], bgPremul, curFg, false);
                    ++dstX;
                }
                continue;
            }

            // printable character
            updatePixel(&dstLine[dstX], bgPremul, curFg, true);
            ++dstX;
        }

        while (dstX < imageWidth) {
            updatePixel(&dstLine[dstX], bgPremul, curFg, false);
            ++dstX;
        }

        advanceToNextLine();
    }

    return img;
}

#ifdef WITH_TESTS

namespace Internal {

class MinimapImageTest final : public QObject
{
    Q_OBJECT

private slots:
    void testALineWithTextIsDrawnAndAnEmptyOneIsNot()
    {
        QTextDocument document;
        document.setPlainText("alpha\n\nbeta\n");

        const MinimapMetrics metrics{20, 2, 1};
        const QImage image = renderMinimap(&document, metrics, QFont(), QColor(Qt::black));

        // Two pixels narrower than the column, and one line's worth of height
        // per line of the document.
        QCOMPARE(image.width(), metrics.width - 2);
        QCOMPARE(image.height(), metrics.lineHeight() * document.blockCount());

        const auto inkOnLine = [&image, &metrics](int line) {
            int ink = 0;
            for (int y = line * metrics.lineHeight();
                 y < line * metrics.lineHeight() + metrics.pixelsPerLine; ++y) {
                for (int x = 0; x < image.width(); ++x) {
                    if (qAlpha(image.pixel(x, y)) > 0)
                        ++ink;
                }
            }
            return ink;
        };

        QVERIFY2(inkOnLine(0) > 0, "the line with text on it was drawn blank");
        QCOMPARE(inkOnLine(1), 0);
        QVERIFY2(inkOnLine(2) > 0, "the third line was drawn blank");
    }

    void testAFoldedLineTakesNoRoom()
    {
        QTextDocument document;
        document.setPlainText("alpha\nbeta\ngamma\n");
        const MinimapMetrics metrics{20, 2, 1};
        const int whole = renderMinimap(&document, metrics, QFont(), QColor(Qt::black)).height();

        // What folding does to a document: the block is still there and takes
        // up no room on screen, so it takes none in the picture either.
        QTextBlock second = document.findBlockByNumber(1);
        second.setVisible(false);
        const int folded = renderMinimap(&document, metrics, QFont(), QColor(Qt::black)).height();
        QCOMPARE(folded, whole - metrics.lineHeight());
    }

    void testAViewThatScrollsPastTheEndGetsRoomForIt()
    {
        QTextDocument document;
        document.setPlainText("alpha\n");
        const MinimapMetrics metrics{20, 2, 1};
        const int whole = renderMinimap(&document, metrics, QFont(), QColor(Qt::black)).height();
        const int padded
            = renderMinimap(&document, metrics, QFont(), QColor(Qt::black), 5).height();
        QCOMPARE(padded, whole + 5 * metrics.lineHeight());
    }
};

QObject *createMinimapImageTest()
{
    return new MinimapImageTest;
}

} // namespace Internal

#endif // WITH_TESTS

} // namespace Core

#ifdef WITH_TESTS
#include "minimapimage.moc"
#endif
