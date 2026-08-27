// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "marginsettings.h"

#include "fontsettings.h"
#include "indenter.h"
#include "texteditortr.h"

using namespace Utils;

namespace TextEditor {

static QColor blendColors(const QColor &a, const QColor &b, int alpha)
{
    return QColor((a.red()   * (256 - alpha) + b.red()   * alpha) / 256,
                  (a.green() * (256 - alpha) + b.green() * alpha) / 256,
                  (a.blue()  * (256 - alpha) + b.blue()  * alpha) / 256);
}

int visibleMarginColumn(const MarginSettingsData &settings, const Indenter *indenter)
{
    if (!settings.m_showMargin)
        return 0;
    if (settings.m_useIndenter && indenter) {
        if (const std::optional<int> margin = indenter->margin())
            return *margin;
    }
    return settings.m_marginColumn;
}

QColor rightMarginColor(const FontSettingsData &fontSettings, bool areaColor)
{
    const QColor baseColor = fontSettings.toTextCharFormat(C_TEXT).background().color();
    const QColor towards = baseColor.value() > 128 ? QColor(Qt::black) : QColor(Qt::white);
    return blendColors(baseColor, towards, areaColor ? 16 : 32);
}

MarginSettings &marginSettings()
{
    static MarginSettings theMarginSettings;
    return theMarginSettings;
}

MarginSettings::MarginSettings(const Key &keyPrefix)
{
    const bool isGlobal = keyPrefix.isEmpty();
    setAutoApply(!isGlobal);

    setSettingsGroup("textMarginSettings");

    showMargin.setSettingsKey(keyPrefix + "ShowMargin");
    showMargin.setDefaultValue(false);
    showMargin.setLabelText(Tr::tr("Display right &margin after column:"));

    tintMarginArea.setSettingsKey(keyPrefix + "tintMarginArea");
    tintMarginArea.setDefaultValue(true);
    tintMarginArea.setLabelText(Tr::tr("Tint whole margin area"));

    useIndenter.setSettingsKey(keyPrefix + "UseIndenter");
    useIndenter.setDefaultValue(false);
    useIndenter.setLabelText(Tr::tr("Use context-specific margin"));
    useIndenter.setToolTip(Tr::tr("If available, use a different margin. "
                                  "For example, the ColumnLimit from the ClangFormat plugin."));

    marginColumn.setSettingsKey(keyPrefix + "MarginColumn");
    marginColumn.setDefaultValue(80);
    marginColumn.setRange(0, 999);

    centerEditorContentWidthPercent.setSettingsKey(keyPrefix + "centeredEditorContentWidthPercent");
    centerEditorContentWidthPercent.setRange(50, 100);
    centerEditorContentWidthPercent.setDefaultValue(100);
    centerEditorContentWidthPercent.setSuffix("%");
    centerEditorContentWidthPercent.setLabelText(Tr::tr("Editor content width:"));
    centerEditorContentWidthPercent.setToolTip(
        Tr::tr(
            "100% means that whole width of editor window is used to display text"
            " content (default).<br>50% means that half of editor width is used to display"
            " text content.<br>Remaining 50% is divided as left and right margins while"
            " centering the content."));

    if (isGlobal)
        readSettings();

    marginColumn.setEnabler(&showMargin);
    tintMarginArea.setEnabler(&showMargin);
}

void MarginSettings::apply()
{
    AspectContainer::apply();
    AspectContainer::writeSettings();
}

MarginSettingsData MarginSettings::data() const
{
    MarginSettingsData d;
    d.m_showMargin = showMargin();
    d.m_tintMarginArea = tintMarginArea();
    d.m_useIndenter = useIndenter();
    d.m_marginColumn = marginColumn();
    d.m_centerEditorContentWidthPercent = centerEditorContentWidthPercent();
    return d;
}

void MarginSettings::setData(const MarginSettingsData &data)
{
    showMargin.setValue(data.m_showMargin);
    tintMarginArea.setValue(data.m_tintMarginArea);
    useIndenter.setValue(data.m_useIndenter);
    marginColumn.setValue(data.m_marginColumn);
    centerEditorContentWidthPercent.setValue(data.m_centerEditorContentWidthPercent);
}

} // TextEditor
