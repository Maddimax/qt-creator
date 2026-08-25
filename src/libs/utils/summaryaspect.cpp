// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "summaryaspect.h"

namespace Utils {

SummaryAspect::SummaryAspect(AspectContainer *container,
                             const QMap<int, QString> &validationPoints,
                             const QString &validText,
                             const QString &invalidText)
    : AspectContainer(container)
    , m_validText(validText)
    , m_invalidText(invalidText)
{
    for (auto it = validationPoints.cbegin(); it != validationPoints.cend(); ++it) {
        Point point;
        point.validText = it.value();
        point.row = new TextDisplay;
        point.row->setText(it.value());
        point.row->setIconType(InfoType::NotOk);
        registerAspect(point.row, /*takeOwnership=*/true);
        m_points[it.key()] = point;
    }
    updateSummary();
}

void SummaryAspect::setPointValid(int key, bool valid, const QString &errorText)
{
    const auto it = m_points.find(key);
    if (it == m_points.end())
        return;

    it->valid = valid;
    it->row->setIconType(valid ? InfoType::Ok : InfoType::NotOk);
    it->row->setText(valid || errorText.isEmpty() ? it->validText : errorText);
    updateSummary();
}

bool SummaryAspect::rowsOk(const QList<int> &keys) const
{
    for (const int key : keys) {
        if (!m_points.value(key).valid)
            return false;
    }
    return true;
}

bool SummaryAspect::allRowsOk() const
{
    return rowsOk(m_points.keys());
}

void SummaryAspect::setInfoText(const QString &text)
{
    m_infoText = text;
    updateSummary();
}

void SummaryAspect::setInProgressText(const QString &text)
{
    m_inProgressText = text;
    updateSummary();
}

void SummaryAspect::updateSummary()
{
    if (!m_inProgressText.isEmpty()) {
        setLabelText(QString("%1...").arg(m_inProgressText));
        return;
    }

    const bool ok = allRowsOk();
    setLabelText(ok ? QString("%1 %2").arg(m_validText, m_infoText).trimmed() : m_invalidText);
    if (ok != m_lastAllOk) {
        m_lastAllOk = ok;
        emit allRowsOkChanged(ok);
    }
}

} // namespace Utils
