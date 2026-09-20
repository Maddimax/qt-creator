// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "fontsettingspage.h"

#include "colorschemeedit.h"
#include "fontsettings.h"
#include "texteditortr.h"

#include <coreplugin/dialogs/ioptionspage.h>
#include <coreplugin/icore.h>

#include <utils/aspectpresentation.h>
#include <utils/aspectwidgets.h>
#include <utils/filedialogs.h>
#include <utils/aspects.h>
#include <utils/filepath.h>
#include <utils/fileutils.h>
#include <utils/guiutils.h>
#include <utils/layoutbuilder.h>
#include <utils/qtcassert.h>
#include <utils/stringutils.h>
#include <utils/theme/theme.h>
#include <utils/utilsicons.h>

#include <QAbstractItemModel>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDebug>
#include <QFileDialog>
#include <QFontComboBox>
#include <QFontDatabase>
#include <QGroupBox>
#include <QInputDialog>
#include <QLabel>
#include <QMessageBox>
#ifdef WITH_TESTS
#include <QTest>
#endif
#include <QPalette>
#include <QPointer>
#include <QPushButton>
#include <QSpacerItem>
#include <QSpinBox>
#include <QTimer>

using namespace TextEditor::Internal;
using namespace Utils;

namespace TextEditor {
namespace Internal {

struct ColorSchemeEntry
{
    ColorSchemeEntry(const FilePath &filePath, bool readOnly) :
        filePath(filePath),
        name(ColorScheme::readNameOfScheme(filePath)),
        readOnly(readOnly)
    { }

    FilePath filePath;
    QString name;
    QString id;
    bool readOnly;
};

class SchemeListModel : public QAbstractListModel
{
public:
    SchemeListModel(QObject *parent = nullptr):
        QAbstractListModel(parent)
    {
    }

    int rowCount(const QModelIndex &parent) const final
    {
        return parent.isValid() ? 0 : m_colorSchemes.size();
    }

    QVariant data(const QModelIndex &index, int role) const final
    {
        if (role == Qt::DisplayRole)
            return m_colorSchemes.at(index.row()).name;

        return QVariant();
    }

    void removeColorScheme(int index)
    {
        beginRemoveRows(QModelIndex(), index, index);
        m_colorSchemes.removeAt(index);
        endRemoveRows();
    }

    void setColorSchemes(const QList<ColorSchemeEntry> &colorSchemes)
    {
        beginResetModel();
        m_colorSchemes = colorSchemes;
        endResetModel();
    }

    const ColorSchemeEntry &colorSchemeAt(int index) const
    { return m_colorSchemes.at(index); }

private:
    QList<ColorSchemeEntry> m_colorSchemes;
};

// The syntax formats of the scheme being edited, as rows read in the format
// each one describes - which is what makes the list legible. The scheme is the
// aspect's value; which row is current is the page's business.
class FormatListModel final : public QAbstractTableModel
{
public:
    explicit FormatListModel(QObject *parent)
        : QAbstractTableModel(parent)
    {}

    void setFormatDescriptions(const FormatDescriptions *descriptions)
    {
        beginResetModel();
        m_descriptions = descriptions;
        endResetModel();
    }

    void setColorScheme(const ColorScheme *scheme) { m_scheme = scheme; refresh(); }
    void setBaseFont(const QFont &font) { m_baseFont = font; refresh(); }

    void refresh()
    {
        if (m_descriptions && !m_descriptions->empty())
            emit dataChanged(index(0, 0), index(int(m_descriptions->size()) - 1, 0));
    }

    int rowCount(const QModelIndex &parent) const override
    {
        return (parent.isValid() || !m_descriptions) ? 0 : int(m_descriptions->size());
    }

    int columnCount(const QModelIndex &parent) const override
    {
        return parent.isValid() ? 0 : 1;
    }

    QVariant data(const QModelIndex &index, int role) const override
    {
        if (!m_descriptions || !m_scheme)
            return {};

        const FormatDescription &description = m_descriptions->at(index.row());
        const Format format = m_scheme->formatFor(description.id());

        switch (role) {
        case Qt::DisplayRole:
            return description.displayName();
        case Qt::ToolTipRole:
            return description.tooltipText();
        case Qt::ForegroundRole: {
            const QColor foreground = format.foreground();
            return foreground.isValid() ? foreground : m_scheme->formatFor(C_TEXT).foreground();
        }
        case Qt::BackgroundRole: {
            const QColor background = format.background();
            return background.isValid() ? QVariant(background) : QVariant();
        }
        case Qt::FontRole: {
            QFont font = m_baseFont;
            font.setBold(format.bold());
            font.setItalic(format.italic());
            font.setUnderline(format.underlineStyle() != QTextCharFormat::NoUnderline);
            return font;
        }
        case AspectTable::EditableRole:
            return false;
        default:
            return {};
        }
    }

    QVariant headerData(int section, Qt::Orientation orientation, int role) const override
    {
        if (orientation == Qt::Vertical || role != Qt::DisplayRole || section != 0)
            return {};
        return Tr::tr("Format");
    }

    QHash<int, QByteArray> roleNames() const override
    {
        return AspectTable::withRoleNames(QAbstractTableModel::roleNames());
    }

private:
    const FormatDescriptions *m_descriptions = nullptr;
    const ColorScheme *m_scheme = nullptr;
    QFont m_baseFont;
};

// The scheme's formats, and which one the page is showing the properties of.
class FormatsAspect final : public BaseAspect
{
    Q_OBJECT

public:
    FormatsAspect(AspectContainer *container, const FormatDescriptions &descriptions)
        : BaseAspect(container)
        , m_descriptions(descriptions)
    {
        // Handed to QML, which frees an unparented QObject it is given.
        m_model.setParent(this);
        m_model.setFormatDescriptions(&m_descriptions);
        m_model.setColorScheme(&m_scheme);
        setQmlName("Formats");
        setLabelText(Tr::tr("Formats:"));
    }

    QAbstractItemModel *tableModel() override { return &m_model; }

    AspectPresentation presentation() const override
    {
        AspectPresentation p = BaseAspect::presentation();
        p.control = AspectControls::Table;
        // The rows are the known format descriptions.
        p.allowAdding = false;
        p.allowRemoving = false;
        // The list shows what it describes, so it is read on the background the
        // scheme says code is read on. A format that sets none of its own has
        // no way to say that per row - the model answers nothing for it - and
        // on the form's background a dark scheme's colours are unreadable.
        p.rowBackground = m_scheme.formatFor(C_TEXT).background();
        return p;
    }

    // The group's title, which names the theme the scheme is for. The page
    // asks rather than hard-coding it.
    Q_PROPERTY(QString groupTitle READ groupTitle CONSTANT)

    QString groupTitle() const
    {
        return Tr::tr("Color Scheme for Theme \"%1\"").arg(Utils::creatorTheme()->displayName());
    }

    Q_INVOKABLE void setCurrentRow(int row)
    {
        const int clamped = row >= 0 && row < int(m_descriptions.size()) ? row : -1;
        if (m_currentRow == clamped)
            return;
        m_currentRow = clamped;
        emit currentFormatChanged();
    }

    int currentRow() const { return m_currentRow; }

    const FormatDescription *currentDescription() const
    {
        if (m_currentRow < 0 || m_currentRow >= int(m_descriptions.size()))
            return nullptr;
        return &m_descriptions.at(m_currentRow);
    }

    Format currentFormat() const
    {
        const FormatDescription *description = currentDescription();
        return description ? m_scheme.formatFor(description->id()) : Format();
    }

    void setCurrentFormat(const Format &format)
    {
        const FormatDescription *description = currentDescription();
        if (!description || m_scheme.formatFor(description->id()) == format)
            return;
        m_scheme.setFormatFor(description->id(), format);
        m_model.refresh();
        // Editing C_TEXT's background changes what every other row is read on.
        if (description->id() == C_TEXT)
            emit controlConfigurationChanged();
        emit schemeEdited();
    }

    const ColorScheme &colorScheme() const { return m_scheme; }

    void setColorScheme(const ColorScheme &scheme)
    {
        m_scheme = scheme;
        m_model.setColorScheme(&m_scheme);
        // The background the rows are read on comes from the scheme, and it is
        // part of the presentation rather than of any row - so the renderer has
        // to be told to ask again.
        emit controlConfigurationChanged();
        emit currentFormatChanged();
    }

    void setBaseFont(const QFont &font) { m_model.setBaseFont(font); }

    bool isReadOnly() const { return m_readOnly; }
    void setReadOnly(bool readOnly)
    {
        if (m_readOnly == readOnly)
            return;
        m_readOnly = readOnly;
        emit currentFormatChanged();
    }

signals:
    void currentFormatChanged();
    void schemeEdited();

private:
    FormatDescriptions m_descriptions;
    ColorScheme m_scheme;
    FormatListModel m_model{this};
    int m_currentRow = -1;
    bool m_readOnly = false;
};


} // namespace Internal

static FilePath customStylesPath()
{
    return Core::ICore::userResourcePath("styles");
}

static FilePath createColorSchemeFileName(const QString &pattern)
{
    const FilePath stylesPath = customStylesPath();

    // Find an available file name
    int i = 1;
    FilePath filePath;
    do {
        filePath = stylesPath.pathAppended(pattern.arg((i == 1) ? QString() : QString::number(i)));
        ++i;
    } while (filePath.exists());

    // Create the base directory when it doesn't exist
    if (!stylesPath.exists() && !stylesPath.createDir()) {
        qWarning() << "Failed to create color scheme directory:" << stylesPath;
        return {};
    }

    return filePath;
}

// ------- FormatDescription
FormatDescription::FormatDescription(TextStyle id,
                                     const QString &displayName,
                                     const QString &tooltipText,
                                     const QColor &foreground,
                                     FormatDescription::ShowControls showControls)
    : m_id(id),
      m_displayName(displayName),
      m_tooltipText(tooltipText),
      m_showControls(showControls)
{
    m_format.setForeground(foreground);
    m_format.setBackground(defaultBackground(id));
}

FormatDescription::FormatDescription(TextStyle id,
                                     const QString &displayName,
                                     const QString &tooltipText,
                                     const Format &format,
                                     FormatDescription::ShowControls showControls)
    : m_id(id),
      m_format(format),
      m_displayName(displayName),
      m_tooltipText(tooltipText),
      m_showControls(showControls)
{
}

FormatDescription::FormatDescription(TextStyle id,
                                     const QString &displayName,
                                     const QString &tooltipText,
                                     const QColor &underlineColor,
                                     const QTextCharFormat::UnderlineStyle underlineStyle,
                                     FormatDescription::ShowControls showControls)
    : m_id(id),
      m_displayName(displayName),
      m_tooltipText(tooltipText),
      m_showControls(showControls)
{
    m_format.setForeground(defaultForeground(id));
    m_format.setBackground(defaultBackground(id));
    m_format.setUnderlineColor(underlineColor);
    m_format.setUnderlineStyle(underlineStyle);
}

FormatDescription::FormatDescription(TextStyle id,
                                     const QString &displayName,
                                     const QString &tooltipText,
                                     FormatDescription::ShowControls showControls)
    : m_id(id),
      m_displayName(displayName),
      m_tooltipText(tooltipText),
      m_showControls(showControls)
{
    m_format.setForeground(defaultForeground(id));
    m_format.setBackground(defaultBackground(id));
}

QColor FormatDescription::defaultForeground(TextStyle id)
{
    if (id == C_TEXT) {
        return Qt::black;
    } else if (id == C_LINE_NUMBER) {
        const QPalette palette = Utils::Theme::initialPalette();
        const QColor bg = palette.window().color();
        if (bg.value() < 128)
            return palette.windowText().color();
        else
            return palette.dark().color();
    } else if (id == C_CURRENT_LINE_NUMBER) {
        const QPalette palette = Utils::Theme::initialPalette();
        const QColor bg = palette.window().color();
        if (bg.value() < 128)
            return palette.windowText().color();
        else
            return QColor();
    } else if (id == C_PARENTHESES) {
        return QColor(Qt::red);
    } else if (id == C_AUTOCOMPLETE) {
        return QColor(Qt::darkBlue);
    } else if (id == C_SEARCH_RESULT_ALT1) {
        return QColor(0x00, 0x00, 0x33);
    } else if (id == C_SEARCH_RESULT_ALT2) {
        return QColor(0x33, 0x00, 0x00);
    }
    return QColor();
}

QColor FormatDescription::defaultBackground(TextStyle id)
{
    if (id == C_TEXT) {
        return Qt::white;
    } else if (id == C_LINE_NUMBER) {
        return Utils::Theme::initialPalette().window().color();
    } else if (id == C_SEARCH_RESULT) {
        return QColor(0xffef0b);
    } else if (id == C_SEARCH_RESULT_ALT1) {
        return QColor(0xb6, 0xcc, 0xff);
    } else if (id == C_SEARCH_RESULT_ALT2) {
        return QColor(0xff, 0xb6, 0xcc);
    } else if (id == C_PARENTHESES) {
        return QColor(0xb4, 0xee, 0xb4);
    } else if (id == C_PARENTHESES_MISMATCH) {
        return QColor(0xed, 0xb0, 0xed);
    } else if (id == C_AUTOCOMPLETE) {
        return QColor(192, 192, 255);
    } else if (id == C_CURRENT_LINE || id == C_SEARCH_SCOPE) {
        const QPalette palette = Utils::Theme::initialPalette();
        const QColor &fg = palette.color(QPalette::Highlight);
        const QColor &bg = palette.color(QPalette::Base);

        qreal smallRatio;
        qreal largeRatio;
        if (id == C_CURRENT_LINE) {
            smallRatio = .3;
            largeRatio = .6;
        } else {
            smallRatio = .05;
            largeRatio = .4;
        }
        const qreal ratio = ((palette.color(QPalette::Text).value() < 128) !=
                (palette.color(QPalette::HighlightedText).value() < 128)) ? smallRatio : largeRatio;

        const QColor &col = QColor::fromRgbF(fg.redF() * ratio + bg.redF() * (1 - ratio),
                                             fg.greenF() * ratio + bg.greenF() * (1 - ratio),
                                             fg.blueF() * ratio + bg.blueF() * (1 - ratio));
        return col;
    } else if (id == C_SELECTION) {
        return Utils::Theme::initialPalette().color(QPalette::Highlight);
    } else if (id == C_OCCURRENCES) {
        return QColor(180, 180, 180);
    } else if (id == C_OCCURRENCES_RENAME) {
        return QColor(255, 100, 100);
    } else if (id == C_DISABLED_CODE) {
        return QColor(239, 239, 239);
    }
    return QColor(); // invalid color
}

bool FormatDescription::showControl(FormatDescription::ShowControls showControl) const
{
    return m_showControls & showControl;
}

namespace Internal {

// The page. The font settings are the value; each control on the page is an
// aspect of its own, and the properties of the selected format are a detail
// pane over the formats list - the shape the Snippets page uses.
class FontSettingsPageContainer final : public AspectContainer
{
public:
    explicit FontSettingsPageContainer(const FormatDescriptions &fd);

    void apply() override;
    void cancel() override;
    bool isDirty() const override;

    FontFamilyAspect family{this};
    SelectionAspect size{this};
    IntegerAspect zoom{this};
    IntegerAspect lineSpacing{this};
    TextDisplay lineSpacingWarning{this};
    BoolAspect antialias{this};

    SelectionAspect scheme{this};
    ActionAspect copyScheme{this};
    ActionAspect deleteScheme{this};
    ActionAspect importScheme{this};
    ActionAspect exportScheme{this};

    FormatsAspect formats;
    TextDisplay builtinSchemeNote{this};

    ColorAspect foreground{this};
    ColorAspect background{this};
    DoubleAspect foregroundSaturation{this};
    DoubleAspect foregroundLightness{this};
    DoubleAspect backgroundSaturation{this};
    DoubleAspect backgroundLightness{this};
    BoolAspect bold{this};
    BoolAspect italic{this};
    ColorAspect underlineColor{this};
    SelectionAspect underlineStyle{this};

private:
    void refreshPointSizes();
    void refreshSchemeList();
    void showFormat();
    void writeFormatBack();
    void selectScheme();
    void maybeSaveColorScheme();
    void doCopyScheme(const QString &name);
    void doDeleteScheme();

    FontSettingsData m_value;
    FontSettingsData m_lastValue;
    QList<ColorSchemeEntry> m_schemes;
    FormatDescriptions m_descriptions;
    // Set while a control is being filled in from the model, so that filling it
    // in is not mistaken for the user editing it.
    bool m_showing = false;
    bool m_refreshingSchemeList = false;
};

FontSettingsPageContainer::FontSettingsPageContainer(const FormatDescriptions &fd)
    : formats(this, fd)
    , m_value(globalFontSettings().data())
    , m_descriptions(fd)
{
    setAutoApply(false);
    m_lastValue = m_value;

    family.setQmlName("Family");
    family.setLabelText(Tr::tr("Family:"));
    family.setValue(m_value.family());

    size.setQmlName("Size");
    size.setLabelText(Tr::tr("Size:"));
    size.setDisplayStyle(SelectionAspect::DisplayStyle::ComboBox);

    zoom.setQmlName("Zoom");
    zoom.setLabelText(Tr::tr("Zoom:"));
    zoom.setSuffix(Tr::tr("%"));
    zoom.setRange(10, 3000);
    zoom.setSingleStep(10);
    zoom.setValue(m_value.fontZoom());

    lineSpacing.setQmlName("LineSpacing");
    lineSpacing.setLabelText(Tr::tr("Line spacing:"));
    lineSpacing.setSuffix(Tr::tr("%"));
    lineSpacing.setRange(50, 3000);
    lineSpacing.setValue(m_value.relativeLineSpacing());

    lineSpacingWarning.setQmlName("LineSpacingWarning");
    lineSpacingWarning.setIconType(InfoType::Warning);
    lineSpacingWarning.setWordWrap(true);
    lineSpacingWarning.setText(
        Tr::tr("A line spacing value other than 100% disables text wrapping. "
               "A value less than 100% can result in overlapping and misaligned graphics."));
    lineSpacingWarning.setVisible(m_value.relativeLineSpacing() != 100);

    antialias.setQmlName("Antialias");
    antialias.setLabelText(Tr::tr("Antialias"));
    antialias.setValue(m_value.antialias());

    scheme.setQmlName("Scheme");
    scheme.setLabelText(Tr::tr("Scheme:"));
    scheme.setDisplayStyle(SelectionAspect::DisplayStyle::ComboBox);

    copyScheme.setQmlName("CopyScheme");
    copyScheme.setActionText(Tr::tr("Copy..."));
    copyScheme.setAction([this] {
        auto dialog = new QInputDialog(Core::ICore::dialogParent());
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->setInputMode(QInputDialog::TextInput);
        dialog->setWindowTitle(Tr::tr("Copy Color Scheme"));
        dialog->setLabelText(Tr::tr("Color scheme name:"));
        dialog->setTextValue(Tr::tr("%1 (copy)").arg(m_value.colorScheme().displayName()));
        connect(dialog, &QInputDialog::textValueSelected,
                this, [this](const QString &name) { doCopyScheme(name); });
        dialog->open();
    });

    deleteScheme.setQmlName("DeleteScheme");
    deleteScheme.setActionText(Tr::tr("Delete"));
    deleteScheme.setEnabled(false);
    deleteScheme.setAction([this] {
        const int index = scheme.volatileValue();
        if (index < 0 || index >= m_schemes.size() || m_schemes.at(index).readOnly)
            return;
        auto messageBox = new QMessageBox(
            QMessageBox::Warning,
            Tr::tr("Delete Color Scheme"),
            Tr::tr("Are you sure you want to delete this color scheme permanently?"),
            QMessageBox::Discard | QMessageBox::Cancel,
            Core::ICore::dialogParent());
        auto deleteButton = static_cast<QPushButton *>(messageBox->button(QMessageBox::Discard));
        deleteButton->setText(Tr::tr("Delete"));
        messageBox->addButton(deleteButton, QMessageBox::AcceptRole);
        messageBox->setDefaultButton(deleteButton);
        connect(messageBox, &QDialog::accepted, this, [this] { doDeleteScheme(); });
        messageBox->setAttribute(Qt::WA_DeleteOnClose);
        messageBox->open();
    });

    importScheme.setQmlName("ImportScheme");
    importScheme.setActionText(Tr::tr("Import"));
    importScheme.setAction([this] {
        const FilePath importedFile
            = Utils::FileUtils::getOpenFilePath(Tr::tr("Import Color Scheme"), {},
                                                Tr::tr("Color scheme (*.xml);;All files (*)"));
        if (importedFile.isEmpty())
            return;

        maybeSaveColorScheme();

        auto dialog = new QInputDialog(Core::ICore::dialogParent());
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->setInputMode(QInputDialog::TextInput);
        dialog->setWindowTitle(Tr::tr("Import Color Scheme"));
        dialog->setLabelText(Tr::tr("Color scheme name:"));
        dialog->setTextValue(importedFile.baseName());
        connect(dialog, &QInputDialog::textValueSelected, this,
                [this, importedFile](const QString &name) {
                    const FilePath saveFileName = createColorSchemeFileName(
                        importedFile.baseName() + "%1." + importedFile.suffix());
                    ColorScheme imported;
                    if (imported.load(importedFile)) {
                        imported.setDisplayName(name);
                        imported.save(saveFileName);
                        m_value.loadColorScheme(saveFileName, m_descriptions);
                    } else {
                        qWarning() << "Failed to import color scheme:" << importedFile;
                    }
                    refreshSchemeList();
                });
        dialog->open();
    });

    exportScheme.setQmlName("ExportScheme");
    exportScheme.setActionText(Tr::tr("Export"));
    exportScheme.setAction([this] {
        const int index = scheme.volatileValue();
        if (index < 0 || index >= m_schemes.size())
            return;
        const FilePath filePath
            = Utils::FileUtils::getSaveFilePath(Tr::tr("Export Color Scheme"),
                                                m_schemes.at(index).filePath,
                                                Tr::tr("Color scheme (*.xml);;All files (*)"));
        if (!filePath.isEmpty())
            formats.colorScheme().save(filePath);
    });

    builtinSchemeNote.setQmlName("BuiltinSchemeNote");
    builtinSchemeNote.setIconType(InfoType::Information);
    builtinSchemeNote.setWordWrap(true);
    builtinSchemeNote.setText(
        Tr::tr("Copy this color scheme to edit it."));
    builtinSchemeNote.setVisible(false);

    // The properties of the selected format. Which of them apply depends on the
    // format, so their visibility follows the selection.
    foreground.setQmlName("Foreground");
    foreground.setLabelText(Tr::tr("Foreground:"));
    background.setQmlName("Background");
    background.setLabelText(Tr::tr("Background:"));
    underlineColor.setQmlName("UnderlineColor");
    underlineColor.setLabelText(Tr::tr("Underline color:"));

    const auto saturation = [](DoubleAspect &aspect, const QString &label, const QString &name) {
        aspect.setQmlName(name);
        aspect.setLabelText(label);
        aspect.setRange(-1.0, 1.0);
        aspect.setSingleStep(0.1);
    };
    saturation(foregroundSaturation, Tr::tr("Foreground saturation:"), "ForegroundSaturation");
    saturation(foregroundLightness, Tr::tr("Foreground lightness:"), "ForegroundLightness");
    saturation(backgroundSaturation, Tr::tr("Background saturation:"), "BackgroundSaturation");
    saturation(backgroundLightness, Tr::tr("Background lightness:"), "BackgroundLightness");

    bold.setQmlName("Bold");
    bold.setLabelText(Tr::tr("Bold"));
    italic.setQmlName("Italic");
    italic.setLabelText(Tr::tr("Italic"));

    underlineStyle.setQmlName("UnderlineStyle");
    underlineStyle.setLabelText(Tr::tr("Underline style:"));
    underlineStyle.setDisplayStyle(SelectionAspect::DisplayStyle::ComboBox);
    // Same list the widget picker offers, in the same order.
    const QList<std::pair<QString, QTextCharFormat::UnderlineStyle>> styles = {
        {Tr::tr("No Underline"), QTextCharFormat::NoUnderline},
        {Tr::tr("Single Underline"), QTextCharFormat::SingleUnderline},
        {Tr::tr("Wave Underline"), QTextCharFormat::WaveUnderline},
        {Tr::tr("Dot Underline"), QTextCharFormat::DotLine},
        {Tr::tr("Dash Underline"), QTextCharFormat::DashUnderline},
        {Tr::tr("Dash-Dot Underline"), QTextCharFormat::DashDotLine},
        {Tr::tr("Dash-Dot-Dot Underline"), QTextCharFormat::DashDotDotLine},
    };
    for (const auto &[name, style] : styles)
        underlineStyle.addOption({name, {}, int(style)});

    // Behaviour. None of this belongs to a layout: it is what the page does.
    connect(&family, &BaseAspect::volatileValueChanged, this, [this] {
        if (m_showing)
            return;
        m_value.setFamily(family.volatileValue());
        refreshPointSizes();
        formats.setBaseFont(m_value.font());
    });
    connect(&size, &BaseAspect::volatileValueChanged, this, [this] {
        if (m_showing)
            return;
        const int points = size.itemValue().toInt();
        if (points > 0) {
            m_value.setFontSize(points);
            formats.setBaseFont(m_value.font());
        }
    });
    connect(&zoom, &BaseAspect::volatileValueChanged, this, [this] {
        m_value.setFontZoom(zoom.volatileValue());
    });
    connect(&antialias, &BaseAspect::volatileValueChanged, this, [this] {
        m_value.setAntialias(antialias.volatileValue());
        formats.setBaseFont(m_value.font());
    });
    connect(&lineSpacing, &BaseAspect::volatileValueChanged, this, [this] {
        m_value.setRelativeLineSpacing(lineSpacing.volatileValue());
        lineSpacingWarning.setVisible(lineSpacing.volatileValue() != 100);
    });
    connect(&globalFontSettings(), &FontSettings::changed, this, [this] {
        zoom.setValue(globalFontSettings().data().fontZoom());
    });

    connect(&scheme, &BaseAspect::volatileValueChanged, this, [this] { selectScheme(); });
    connect(&formats, &FormatsAspect::currentFormatChanged, this, [this] { showFormat(); });
    connect(&formats, &FormatsAspect::schemeEdited, this, [] { markSettingsDirty(); });

    for (BaseAspect *aspect : QList<BaseAspect *>{&foreground, &background,
                                                  &foregroundSaturation, &foregroundLightness,
                                                  &backgroundSaturation, &backgroundLightness,
                                                  &bold, &italic,
                                                  &underlineColor, &underlineStyle}) {
        connect(aspect, &BaseAspect::volatileValueChanged, this, [this] { writeFormatBack(); });
    }

    setQmlSource(QUrl("qrc:/qt/qml/QtCreator/TextEditor/FontSettingsPage.qml"));

    refreshPointSizes();
    refreshSchemeList();
    showFormat();
}

void FontSettingsPageContainer::refreshPointSizes()
{
    const QString familyName = family.volatileValue();
    QList<int> sizes = QFontDatabase::pointSizes(familyName);
    if (sizes.isEmpty()) {
        const QStringList styles = QFontDatabase::styles(familyName);
        if (!styles.isEmpty())
            sizes = QFontDatabase::pointSizes(familyName, styles.first());
    }
    if (sizes.isEmpty())
        sizes = QFontDatabase::standardSizes();

    // The size in use is always offered, even where the family does not list
    // it - otherwise selecting a family would silently change the size.
    const int current = m_value.fontSize();
    if (!sizes.contains(current)) {
        sizes.append(current);
        std::sort(sizes.begin(), sizes.end());
    }

    // SelectionAspect keeps its options, so the list is rewritten in place and
    // any surplus entries are made unreachable rather than removed.
    for (int i = 0; i < sizes.size(); ++i) {
        const SelectionAspect::Option option{QString::number(sizes.at(i)), {}, sizes.at(i)};
        if (i < size.optionCount())
            size.setOptionForIndex(i, option);
        else
            size.addOption(option);
    }
    for (int i = sizes.size(); i < size.optionCount(); ++i)
        size.setOptionForIndex(i, {QString::number(current), {}, current});

    m_showing = true;
    size.setValue(std::max(0, int(sizes.indexOf(current))));
    m_showing = false;
}

void FontSettingsPageContainer::refreshSchemeList()
{
    m_schemes.clear();

    const FilePath styleDir = Core::ICore::resourcePath("styles");
    FilePaths schemeList = styleDir.dirEntries(FileFilter({"*.xml"}, DirFilterFlag::Files));
    const FilePath defaultScheme = FontSettingsData::defaultSchemeFileName();
    if (schemeList.removeAll(defaultScheme))
        schemeList.prepend(defaultScheme);

    int selected = 0;
    for (const FilePath &file : std::as_const(schemeList)) {
        if (m_value.colorSchemeFileName().fileName() == file.fileName())
            selected = m_schemes.size();
        m_schemes.append(ColorSchemeEntry(file, true));
    }
    if (m_schemes.isEmpty())
        qWarning() << "Warning: no color schemes found in path:" << styleDir.toUserOutput();

    const FilePaths files
        = customStylesPath().dirEntries(FileFilter({"*.xml"}, DirFilterFlag::Files));
    for (const FilePath &file : files) {
        if (m_value.colorSchemeFileName().fileName() == file.fileName())
            selected = m_schemes.size();
        m_schemes.append(ColorSchemeEntry(file, false));
    }

    for (int i = 0; i < m_schemes.size(); ++i) {
        const SelectionAspect::Option option{m_schemes.at(i).name, {}, i};
        if (i < scheme.optionCount())
            scheme.setOptionForIndex(i, option);
        else
            scheme.addOption(option);
    }

    // The flag stays set across selectScheme() too: refreshing the list is not
    // the user switching schemes, and maybeSaveColorScheme() would put up a
    // modal dialog asking about changes nobody made.
    m_refreshingSchemeList = true;
    scheme.setValue(selected);
    selectScheme();
    m_refreshingSchemeList = false;
}

void FontSettingsPageContainer::selectScheme()
{
    const int index = scheme.volatileValue();
    bool readOnly = true;
    if (index >= 0 && index < m_schemes.size()) {
        if (!m_refreshingSchemeList)
            maybeSaveColorScheme();
        const ColorSchemeEntry &entry = m_schemes.at(index);
        readOnly = entry.readOnly;
        m_value.loadColorScheme(entry.filePath, m_descriptions);
        formats.setColorScheme(m_value.colorScheme());
        formats.setBaseFont(m_value.font());
    }
    copyScheme.setEnabled(index >= 0);
    deleteScheme.setEnabled(!readOnly);
    formats.setReadOnly(readOnly);
    builtinSchemeNote.setVisible(readOnly);
    showFormat();
}

void FontSettingsPageContainer::showFormat()
{
    const FormatDescription *description = formats.currentDescription();
    const Format format = formats.currentFormat();
    const bool editable = description && !formats.isReadOnly();

    // setValue(), deliberately: it does not emit volatileValueChanged, which is
    // what the controls write back on. Filling ten of them in one at a time
    // would otherwise compose a format from a mix of the old values and the new
    // and write that back, so showing a format would edit it.
    m_showing = true;

    foreground.setValue(format.foreground());
    background.setValue(format.background());
    foregroundSaturation.setValue(format.relativeForegroundSaturation());
    foregroundLightness.setValue(format.relativeForegroundLightness());
    backgroundSaturation.setValue(format.relativeBackgroundSaturation());
    backgroundLightness.setValue(format.relativeBackgroundLightness());
    bold.setValue(format.bold());
    italic.setValue(format.italic());
    underlineColor.setValue(format.underlineColor());
    underlineStyle.setValue(
        std::max(0, underlineStyle.indexForItemValue(int(format.underlineStyle()))));

    const auto show = [&](BaseAspect &aspect, FormatDescription::ShowControls flag) {
        aspect.setVisible(description && description->showControl(flag));
        aspect.setEnabled(editable);
    };
    show(foreground, FormatDescription::ShowForegroundControl);
    show(background, FormatDescription::ShowBackgroundControl);
    show(foregroundSaturation, FormatDescription::ShowRelativeForegroundControl);
    show(foregroundLightness, FormatDescription::ShowRelativeForegroundControl);
    show(backgroundSaturation, FormatDescription::ShowRelativeBackgroundControl);
    show(backgroundLightness, FormatDescription::ShowRelativeBackgroundControl);
    show(bold, FormatDescription::ShowFontControls);
    show(italic, FormatDescription::ShowFontControls);
    show(underlineColor, FormatDescription::ShowUnderlineControl);
    show(underlineStyle, FormatDescription::ShowUnderlineControl);

    m_showing = false;
}

void FontSettingsPageContainer::writeFormatBack()
{
    if (formats.isReadOnly())
        return;

    Format format = formats.currentFormat();
    format.setForeground(foreground.volatileValue());
    format.setBackground(background.volatileValue());
    format.setRelativeForegroundSaturation(foregroundSaturation.volatileValue());
    format.setRelativeForegroundLightness(foregroundLightness.volatileValue());
    format.setRelativeBackgroundSaturation(backgroundSaturation.volatileValue());
    format.setRelativeBackgroundLightness(backgroundLightness.volatileValue());
    format.setBold(bold.volatileValue());
    format.setItalic(italic.volatileValue());
    format.setUnderlineColor(underlineColor.volatileValue());
    format.setUnderlineStyle(
        QTextCharFormat::UnderlineStyle(underlineStyle.itemValue().toInt()));
    formats.setCurrentFormat(format);
}

void FontSettingsPageContainer::maybeSaveColorScheme()
{
    if (m_value.colorScheme() == formats.colorScheme())
        return;

    QMessageBox messageBox(
        QMessageBox::Warning,
        Tr::tr("Color Scheme Changed"),
        Tr::tr("The color scheme \"%1\" was modified, do you want to save the changes?")
            .arg(formats.colorScheme().displayName()),
        QMessageBox::Discard | QMessageBox::Save,
        Core::ICore::dialogParent());

    auto discardButton = static_cast<QPushButton *>(messageBox.button(QMessageBox::Discard));
    discardButton->setText(Tr::tr("Discard"));
    messageBox.addButton(discardButton, QMessageBox::DestructiveRole);
    messageBox.setDefaultButton(QMessageBox::Save);

    if (messageBox.exec() == QMessageBox::Save)
        formats.colorScheme().save(m_value.colorSchemeFileName());
}

void FontSettingsPageContainer::doCopyScheme(const QString &name)
{
    const int index = scheme.volatileValue();
    if (index < 0 || index >= m_schemes.size())
        return;

    QString baseFileName = m_schemes.at(index).filePath.completeBaseName();
    baseFileName += QLatin1String("_copy%1.xml");
    const FilePath filePath = createColorSchemeFileName(baseFileName);
    if (filePath.isEmpty())
        return;

    maybeSaveColorScheme();

    m_value.setColorScheme(formats.colorScheme());
    ColorScheme copy = m_value.colorScheme();
    copy.setDisplayName(name);
    if (copy.save(filePath))
        m_value.setColorSchemeFileName(filePath);

    refreshSchemeList();
    markSettingsDirty();
}

void FontSettingsPageContainer::doDeleteScheme()
{
    const int index = scheme.volatileValue();
    QTC_ASSERT(index >= 0 && index < m_schemes.size(), return);
    QTC_ASSERT(!m_schemes.at(index).readOnly, return);

    if (m_schemes.at(index).filePath.removeFile())
        refreshSchemeList();
}

bool FontSettingsPageContainer::isDirty() const
{
    return m_value != m_lastValue || m_value.colorScheme() != formats.colorScheme()
           || AspectContainer::isDirty();
}

void FontSettingsPageContainer::apply()
{
    if (m_value.colorScheme() != formats.colorScheme()) {
        // Update the scheme and save it under the name it already has
        m_value.setColorScheme(formats.colorScheme());
        m_value.colorScheme().save(m_value.colorSchemeFileName());
    }

    const int points = size.itemValue().toInt();
    if (points > 0 && m_value.fontSize() != points) {
        m_value.setFontSize(points);
        formats.setBaseFont(m_value.font());
    }

    const int index = scheme.volatileValue();
    if (index >= 0 && index < m_schemes.size()) {
        const ColorSchemeEntry &entry = m_schemes.at(index);
        if (entry.filePath != m_value.colorSchemeFileName())
            m_value.loadColorScheme(entry.filePath, m_descriptions);
    }

    AspectContainer::apply();

    m_lastValue = m_value;
    globalFontSettings().setData(m_value);
    globalFontSettings().apply();
}

void FontSettingsPageContainer::cancel()
{
    AspectContainer::cancel();

    m_value = m_lastValue;
    m_showing = true;
    family.setValue(m_value.family());
    zoom.setValue(m_value.fontZoom());
    lineSpacing.setValue(m_value.relativeLineSpacing());
    antialias.setValue(m_value.antialias());
    m_showing = false;

    refreshPointSizes();
    refreshSchemeList();
}


class FontSettingsPage final : public Core::IOptionsPage
{
public:
    FontSettingsPage()
    {
        const FormatDescriptions fd = initialFormats();
        setupFontSettings(fd);
        setId(Constants::TEXT_EDITOR_FONT_SETTINGS);
        setDisplayName(Tr::tr("Font && Colors"));
        setCategory(TextEditor::Constants::TEXT_EDITOR_SETTINGS_CATEGORY);
        setSettingsProvider([fd] {
            static FontSettingsPageContainer thePage(fd);
            return &thePage;
        });
    }
};

void setupFontSettingsPage()
{
    static FontSettingsPage theFontSettingsPage;
}

#ifdef WITH_TESTS

// The formats list is read in the colours it describes, and the properties
// below it are those of the selected format. Both used to live in the widgets
// the page built.
class FontSettingsTest : public QObject
{
    Q_OBJECT

private slots:
    void testFormatsAreReadInTheirOwnColours();
    void testSelectingAFormatShowsOnlyWhatItHas();
    void testEveryShippedSchemeTintsTheCurrentLineRatherThanCoveringIt();
};

// How far \a tint has moved from \a background towards \a text, as a share of
// the whole distance. A current line that is a tint of the editor's background
// is a small number; one that ignores the background is a large one.
static qreal lift(const QColor &background, const QColor &text, const QColor &tint)
{
    qreal total = 0;
    int counted = 0;
    const auto channel = [&](qreal bg, qreal fg, qreal t) {
        if (qFuzzyCompare(bg, fg))
            return;
        total += (t - bg) / (fg - bg);
        ++counted;
    };
    channel(background.redF(), text.redF(), tint.redF());
    channel(background.greenF(), text.greenF(), tint.greenF());
    channel(background.blueF(), text.blueF(), tint.blueF());
    return counted == 0 ? 0 : total / counted;
}

void FontSettingsTest::testEveryShippedSchemeTintsTheCurrentLineRatherThanCoveringIt()
{
    // The line the caret is on is read through, so its colour has to be a lift
    // of the editor's own background. A scheme that does not name one gets a
    // computed colour, and computing that from the system palette rather than
    // from the scheme is how a light scheme under a dark system appearance ends
    // up with a near-black band across a white editor.
    const FilePath styleDir = Core::ICore::resourcePath("styles");
    const FilePaths schemes = styleDir.dirEntries(FileFilter({"*.xml"}, DirFilterFlag::Files));
    QVERIFY2(schemes.size() > 5, "the shipped schemes were not found");

    QStringList offenders;
    for (const FilePath &scheme : schemes) {
        FontSettingsData settings;
        if (!settings.loadColorScheme(scheme, initialFormats()))
            QFAIL(qPrintable("could not load " + scheme.fileName()));

        const QColor background = settings.toTextCharFormat(C_TEXT).background().color();
        const QColor text = settings.toTextCharFormat(C_TEXT).foreground().color();
        const QBrush brush = settings.toTextCharFormat(C_CURRENT_LINE).background();
        // A scheme is allowed to ask for no current-line highlight at all.
        if (brush.style() == Qt::NoBrush)
            continue;

        const qreal moved = lift(background, text, brush.color());
        // The widest any scheme picks for itself is 0.22; a slab of somebody
        // else's background lands far outside that.
        if (moved < 0 || moved > 0.4) {
            offenders << QString("%1: bg %2, current line %3 (%4 of the way to the text)")
                             .arg(scheme.fileName(), background.name(),
                                  brush.color().name(), QString::number(moved, 'f', 2));
        }
    }
    QVERIFY2(offenders.isEmpty(), qPrintable(offenders.join("; ")));
}

void FontSettingsTest::testFormatsAreReadInTheirOwnColours()
{
    FontSettingsPageContainer page(initialFormats());
    QAbstractItemModel *model = page.formats.tableModel();
    QVERIFY(model->rowCount({}) > 1);

    // Every row names a format and says what colour to read it in - that is
    // what makes the list legible at all.
    for (int row = 0; row < model->rowCount({}); ++row) {
        const QModelIndex index = model->index(row, 0);
        QVERIFY2(!index.data(Qt::DisplayRole).toString().isEmpty(),
                 qPrintable(QString("row %1 has no name").arg(row)));
        const QVariant foreground = index.data(Qt::ForegroundRole);
        QVERIFY2(foreground.value<QColor>().isValid(),
                 qPrintable(QString("row %1 has no colour to be read in").arg(row)));
    }

    // And not all the same colour: a list where every row looks alike is the
    // bug this replaces.
    QSet<QRgb> colours;
    for (int row = 0; row < model->rowCount({}); ++row)
        colours.insert(model->index(row, 0).data(Qt::ForegroundRole).value<QColor>().rgb());
    QVERIFY2(colours.size() > 1, "every format is drawn in the same colour");
}

void FontSettingsTest::testSelectingAFormatShowsOnlyWhatItHas()
{
    FontSettingsPageContainer page(initialFormats());

    // Nothing selected: no properties to show.
    page.formats.setCurrentRow(-1);
    QVERIFY(!page.foreground.isVisible());
    QVERIFY(!page.bold.isVisible());

    // A format that does not offer every property, so that "what this one has"
    // and "everything" are told apart. Checking a format that happens to offer
    // all of them proves nothing.
    const FormatDescriptions all = initialFormats();
    int restricted = -1;
    for (int row = 0; row < int(all.size()); ++row) {
        if (!all.at(row).showControl(FormatDescription::ShowFontControls)) {
            restricted = row;
            break;
        }
    }
    QVERIFY2(restricted != -1, "no format without font controls - the test needs one");

    page.formats.setCurrentRow(restricted);
    const FormatDescription *description = page.formats.currentDescription();
    QVERIFY(description);
    QVERIFY(!description->showControl(FormatDescription::ShowFontControls));
    QVERIFY(!page.bold.isVisible());
    QVERIFY(!page.italic.isVisible());
    QCOMPARE(page.foreground.isVisible(),
             description->showControl(FormatDescription::ShowForegroundControl));
    QCOMPARE(page.underlineStyle.isVisible(),
             description->showControl(FormatDescription::ShowUnderlineControl));

    page.formats.setCurrentRow(0);

    // The values shown are the format's own.
    QVERIFY(page.formats.currentDescription());
    QCOMPARE(page.foreground.value(), page.formats.currentFormat().foreground());
    QCOMPARE(page.bold.value(), page.formats.currentFormat().bold());

    // Showing a format is not editing it. Ten controls each write back when
    // they change, so filling them in has to be told apart from the user
    // touching them - otherwise merely looking at the page dirties the scheme.
    QVERIFY(!page.isDirty());
    page.formats.setCurrentRow(1);
    page.formats.setCurrentRow(0);
    QVERIFY(!page.isDirty());

    // The scheme that comes with Qt Creator is read-only, so its controls are
    // not usable and an edit that reached them anyway is refused.
    QVERIFY(page.formats.isReadOnly());
    QVERIFY(!page.bold.isEnabled());
    const bool wasBold = page.formats.currentFormat().bold();
    page.bold.setVolatileValue(!wasBold);
    QCOMPARE(page.formats.currentFormat().bold(), wasBold);

    // On a scheme that can be edited, the same change lands. Through the
    // volatile value, which is the path a control takes - setValue() is the
    // page filling the control in.
    page.formats.setReadOnly(false);
    QVERIFY(page.bold.isEnabled());
    page.bold.setVolatileValue(!wasBold);
    QCOMPARE(page.formats.currentFormat().bold(), !wasBold);
    QVERIFY(page.isDirty());
}

QObject *createFontSettingsTest()
{
    return new FontSettingsTest;
}

#endif // WITH_TESTS

} // namespace Internal
} // namespace TextEditor

#include "fontsettingspage.moc"
