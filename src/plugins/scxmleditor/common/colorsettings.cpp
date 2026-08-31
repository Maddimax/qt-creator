// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "colorsettings.h"
#include "themecolors.h"
#include "scxmleditorconstants.h"
#include "scxmleditortr.h"

#include <QInputDialog>
#include <QMessageBox>
#include <QStandardItem>
#include <QVBoxLayout>

#include <coreplugin/icore.h>

#include <utils/aspects.h>
#include <utils/aspectwidgets.h>
#include <utils/utilsicons.h>

#ifdef WITH_TESTS
#include <coreplugin/dialogs/ioptionspage.h>
#include <QTest>
#endif

using namespace Utils;

namespace ScxmlEditor::Common {

namespace {

// The colours of one theme, as many as defaultThemeColors() names.
// Drawn as the cascade of swatches the widget painted, which is why the page
// addresses this aspect itself rather than letting a delegate draw it.
class ThemeColorsAspect final : public BaseAspect
{
public:
    explicit ThemeColorsAspect(AspectContainer *container)
        : BaseAspect(container)
    {
        reset();
    }

    // The names, because QML has no QColor list and a string is what the
    // settings hold anyway.
    QVariant volatileVariantValue() const override
    {
        QVariantList names;
        for (const QColor &color : std::as_const(m_colors))
            names.append(color.name());
        return names;
    }

    Q_INVOKABLE void setColorAt(int index, const QColor &color)
    {
        if (index < 0 || index >= m_colors.size() || m_colors.at(index) == color)
            return;
        m_colors[index] = color;
        emit changed();
    }

    QColor colorAt(int index) const { return m_colors.value(index); }

    void reset()
    {
        m_colors = defaultThemeColors();
        emit changed();
    }

    // Only the colours that differ from the defaults are kept, which is what
    // the widget stored.
    QVariantMap savedColors() const
    {
        QVariantMap data;
        for (int i = 0; i < m_colors.size(); ++i) {
            if (m_colors.at(i) != defaultThemeColors().at(i))
                data[QString::number(i)] = m_colors.at(i).name();
        }
        return data;
    }

    void showSavedColors(const QVariantMap &data)
    {
        m_colors = defaultThemeColors();
        for (auto it = data.cbegin(); it != data.cend(); ++it) {
            const int index = it.key().toInt();
            if (index >= 0 && index < m_colors.size())
                m_colors[index] = QColor(it.value().toString());
        }
        emit changed();
    }

private:
    QList<QColor> m_colors;
};

} // namespace

// What the form asks: which theme is being edited, the two buttons that add
// and remove one, and the colours in it.
class ColorSettingsAspects final : public AspectContainer
{
public:
    ColorSettingsAspects();

    void save();

    StringSelectionAspect theme{this};
    ActionAspect addTheme{this};
    ActionAspect removeTheme{this};
    ThemeColorsAspect colors{this};

private:
    void showTheme(const QString &name);
    void keepCurrentColors();
    void createTheme();
    void deleteTheme();

    QVariantMap m_colorThemes;
    QStringList m_names;
    bool m_showing = false;
};

ColorSettingsAspects::ColorSettingsAspects()
{
    setAutoApply(true);
    setQmlSource(QUrl("qrc:/qt/qml/QtCreator/ScxmlEditor/ColorSettings.qml"));

    const QtcSettings *s = Core::ICore::settings();
    m_colorThemes = s->value(Constants::C_SETTINGS_COLORSETTINGS_COLORTHEMES).toMap();
    m_names = m_colorThemes.keys();

    theme.setQmlName("Theme");
    theme.setComboBoxEditable(false);
    theme.setFillCallback([this](const StringSelectionAspect::ResultCallback &cb) {
        QList<QStandardItem *> items;
        for (const QString &name : std::as_const(m_names)) {
            auto item = new QStandardItem(name);
            item->setData(name);
            items.append(item);
        }
        cb(items);
    });

    addTheme.setQmlName("AddTheme");
    addTheme.setActionIcon(Utils::Icons::PLUS.icon());
    addTheme.setToolTip(Tr::tr("Create New Color Theme"));
    addTheme.setAction([this] { createTheme(); });

    removeTheme.setQmlName("RemoveTheme");
    removeTheme.setActionIcon(Utils::Icons::MINUS.icon());
    removeTheme.setToolTip(Tr::tr("Remove Color Theme"));
    removeTheme.setAction([this] { deleteTheme(); });

    colors.setQmlName("Colors");

    connect(&theme, &BaseAspect::changed, this, [this] {
        if (!m_showing)
            showTheme(theme.value());
    });
    connect(&colors, &BaseAspect::changed, this, [this] {
        if (!m_showing)
            keepCurrentColors();
    });

    theme.setValue(s->value(Constants::C_SETTINGS_COLORSETTINGS_CURRENTCOLORTHEME).toString());
    showTheme(theme.value());
}

void ColorSettingsAspects::showTheme(const QString &name)
{
    m_showing = true;
    if (name.isEmpty() || !m_colorThemes.contains(name)) {
        colors.reset();
        colors.setEnabled(false);
    } else {
        colors.showSavedColors(m_colorThemes.value(name).toMap());
        colors.setEnabled(true);
    }
    m_showing = false;
}

void ColorSettingsAspects::keepCurrentColors()
{
    const QString name = theme.value();
    if (!name.isEmpty())
        m_colorThemes[name] = colors.savedColors();
}

void ColorSettingsAspects::createTheme()
{
    const QString name = QInputDialog::getText(Core::ICore::dialogParent(),
                                               Tr::tr("Create New Color Theme"),
                                               Tr::tr("Theme ID"));
    if (name.isEmpty())
        return;
    if (m_colorThemes.contains(name)) {
        QMessageBox::warning(Core::ICore::dialogParent(),
                             Tr::tr("Cannot Create Theme"),
                             Tr::tr("Theme %1 is already available.").arg(name));
        return;
    }

    m_colorThemes[name] = QVariantMap();
    m_names = m_colorThemes.keys();
    theme.refill();
    theme.setValue(name);
    showTheme(name);
}

void ColorSettingsAspects::deleteTheme()
{
    const QString name = theme.value();
    if (name.isEmpty())
        return;
    const QMessageBox::StandardButton result
        = QMessageBox::question(Core::ICore::dialogParent(),
                                Tr::tr("Remove Color Theme"),
                                Tr::tr("Are you sure you want to delete color theme %1?").arg(name),
                                QMessageBox::Yes | QMessageBox::No,
                                QMessageBox::No);
    if (result != QMessageBox::Yes)
        return;

    m_colorThemes.remove(name);
    m_names = m_colorThemes.keys();
    theme.refill();
    theme.setValue(m_names.value(0));
    showTheme(theme.value());
}

void ColorSettingsAspects::save()
{
    keepCurrentColors();
    QtcSettings *s = Core::ICore::settings();
    s->setValue(Constants::C_SETTINGS_COLORSETTINGS_COLORTHEMES, m_colorThemes);
    s->setValue(Constants::C_SETTINGS_COLORSETTINGS_CURRENTCOLORTHEME, theme.value());
}

ColorSettings::ColorSettings(QWidget *parent)
    : QFrame(parent)
    , d(new ColorSettingsAspects)
{
    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    if (QWidget *form = Utils::AspectWidgets::createAspectForm(d.get()))
        layout->addWidget(form);
}

ColorSettings::~ColorSettings() = default;

void ColorSettings::save()
{
    d->save();
}

#ifdef WITH_TESTS

class ColorSettingsTest final : public QObject
{
    Q_OBJECT

private slots:
    void testTheFormDrawsWithTheQmlItNames()
    {
        ColorSettingsAspects aspects;
        const Result<> rendered = Core::aspectFormRenders(&aspects, "ColorSettings.qml");
        QVERIFY2(rendered, qPrintable(rendered ? QString() : rendered.error()));
    }

    void testThereIsOneColourPerSwatch()
    {
        // The aspect hands the colours to QML as names, one per swatch the
        // widget painted.
        ColorSettingsAspects aspects;
        const QVariantList names = aspects.colors.volatileVariantValue().toList();
        QCOMPARE(names.size(), defaultThemeColors().size());
        QCOMPARE(QColor(names.at(0).toString()), defaultThemeColors().at(0));
    }

    void testNoColourCanBePickedWithNoThemeToPickItFor()
    {
        // Nothing is saved under a theme that is not there, so the swatches
        // are closed until one is chosen - which is what the widget's
        // setEnabled(false) came to.
        ColorSettingsAspects aspects;
        aspects.theme.setValue("not a theme");
        QVERIFY2(!aspects.colors.isEnabled(),
                 "colours could be picked for a theme that does not exist");
    }

    void testOnlyWhatDiffersFromTheDefaultIsKept()
    {
        // The settings hold the changed colours by index, so a theme left
        // alone takes no room at all.
        ColorSettingsAspects aspects;
        QVERIFY(aspects.colors.savedColors().isEmpty());

        aspects.colors.setColorAt(2, QColor("#123456"));
        const QVariantMap kept = aspects.colors.savedColors();
        QCOMPARE(kept.size(), 1);
        QCOMPARE(kept.value("2").toString(), QString("#123456"));

        // And putting it back leaves nothing again.
        aspects.colors.setColorAt(2, defaultThemeColors().at(2));
        QVERIFY(aspects.colors.savedColors().isEmpty());
    }

    void testAThemeIsShownAsItWasSaved()
    {
        ColorSettingsAspects aspects;
        aspects.colors.showSavedColors({{"1", "#abcdef"}});
        QCOMPARE(aspects.colors.colorAt(1), QColor("#abcdef"));
        // Everything else is the default, not whatever was there before.
        QCOMPARE(aspects.colors.colorAt(0), defaultThemeColors().at(0));

        aspects.colors.showSavedColors({});
        QCOMPARE(aspects.colors.colorAt(1), defaultThemeColors().at(1));
    }
};

QObject *createColorSettingsTest()
{
    return new ColorSettingsTest;
}

#endif // WITH_TESTS

} // ScxmlEditor::Common

#ifdef WITH_TESTS
#include "colorsettings.moc"
#endif
