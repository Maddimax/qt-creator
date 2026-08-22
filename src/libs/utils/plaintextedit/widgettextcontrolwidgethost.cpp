// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "texteditinghost.h"
#include "widgettextcontrol.h"

#include "../utilstr.h"

#include <QApplication>
#include <QLineEdit>
#include <QMenu>
#include <QPointer>
#include <QStyleHintReturnVariant>
#include <QStyleHints>
#include <QStyleOption>
#include <QToolTip>

namespace Utils {

class UnicodeControlCharacterMenu : public QMenu
{
    Q_OBJECT
public:
    UnicodeControlCharacterMenu(QObject *editWidget, QWidget *parent);

private Q_SLOTS:
    void menuActionTriggered();

private:
    QObject *editWidget;
};

#ifndef QT_NO_CONTEXTMENU
#define NUM_CONTROL_CHARACTERS 14
const struct QUnicodeControlCharacter {
    const char *text;
    ushort character;
} qt_controlCharacters[NUM_CONTROL_CHARACTERS] = {
    { QT_TRANSLATE_NOOP("QUnicodeControlCharacterMenu", "LRM Left-to-right mark"), 0x200e },
    { QT_TRANSLATE_NOOP("QUnicodeControlCharacterMenu", "RLM Right-to-left mark"), 0x200f },
    { QT_TRANSLATE_NOOP("QUnicodeControlCharacterMenu", "ZWJ Zero width joiner"), 0x200d },
    { QT_TRANSLATE_NOOP("QUnicodeControlCharacterMenu", "ZWNJ Zero width non-joiner"), 0x200c },
    { QT_TRANSLATE_NOOP("QUnicodeControlCharacterMenu", "ZWSP Zero width space"), 0x200b },
    { QT_TRANSLATE_NOOP("QUnicodeControlCharacterMenu", "LRE Start of left-to-right embedding"), 0x202a },
    { QT_TRANSLATE_NOOP("QUnicodeControlCharacterMenu", "RLE Start of right-to-left embedding"), 0x202b },
    { QT_TRANSLATE_NOOP("QUnicodeControlCharacterMenu", "LRO Start of left-to-right override"), 0x202d },
    { QT_TRANSLATE_NOOP("QUnicodeControlCharacterMenu", "RLO Start of right-to-left override"), 0x202e },
    { QT_TRANSLATE_NOOP("QUnicodeControlCharacterMenu", "PDF Pop directional formatting"), 0x202c },
    { QT_TRANSLATE_NOOP("QUnicodeControlCharacterMenu", "LRI Left-to-right isolate"), 0x2066 },
    { QT_TRANSLATE_NOOP("QUnicodeControlCharacterMenu", "RLI Right-to-left isolate"), 0x2067 },
    { QT_TRANSLATE_NOOP("QUnicodeControlCharacterMenu", "FSI First strong isolate"), 0x2068 },
    { QT_TRANSLATE_NOOP("QUnicodeControlCharacterMenu", "PDI Pop directional isolate"), 0x2069 }
};

UnicodeControlCharacterMenu::UnicodeControlCharacterMenu(QObject *_editWidget, QWidget *parent)
    : QMenu(parent), editWidget(_editWidget)
{
    setTitle(Tr::tr("Insert Unicode Control Character"));
    for (const QUnicodeControlCharacter &qt_controlCharacter : qt_controlCharacters)
        addAction(Tr::tr(qt_controlCharacter.text), this, SLOT(menuActionTriggered()));
}

void UnicodeControlCharacterMenu::menuActionTriggered()
{
    QAction *a = qobject_cast<QAction *>(sender());
    int idx = actions().indexOf(a);
    if (idx < 0 || idx >= NUM_CONTROL_CHARACTERS)
        return;
    QChar c(qt_controlCharacters[idx].character);
    QString str(c);

#if QT_CONFIG(textedit)
    if (QTextEdit *edit = qobject_cast<QTextEdit *>(editWidget)) {
        edit->insertPlainText(str);
        return;
    }
#endif
    if (WidgetTextControl *control = qobject_cast<WidgetTextControl *>(editWidget)) {
        control->insertPlainText(str);
    }
#if QT_CONFIG(lineedit)
    if (QLineEdit *edit = qobject_cast<QLineEdit *>(editWidget)) {
        edit->insert(str);
        return;
    }
#endif
}

QMenu *WidgetTextControl::createStandardContextMenu(const QPointF &pos, QWidget *parent)
{
    const QList<QAction *> actions = createStandardContextMenuActions(pos, nullptr);
    if (actions.isEmpty())
        return nullptr;

    QMenu *menu = new QMenu(parent);
    for (QAction *action : actions)
        action->setParent(menu);
    menu->addActions(actions);

    if ((textInteractionFlags() & Qt::TextEditable)
            && QGuiApplication::styleHints()->useRtlExtensions()) {
        menu->addSeparator();
        UnicodeControlCharacterMenu *ctrlCharacterMenu = new UnicodeControlCharacterMenu(this, menu);
        menu->addMenu(ctrlCharacterMenu);
    }

    return menu;
}
#endif // QT_NO_CONTEXTMENU

namespace {

class WidgetTextEditingHost final : public TextEditingHost
{
public:
    WidgetTextEditingHost(WidgetTextControl *control, QWidget *widget, QWidget *eventWidget)
        : m_control(control), m_widget(widget), m_eventWidget(eventWidget)
    {}

    QPalette palette() const override
    {
        return QApplication::palette("WidgetTextControl");
    }

    int cursorWidthHint() const override
    {
        return QApplication::style()->pixelMetric(QStyle::PM_TextCursorWidth, nullptr,
                                                  qobject_cast<QWidget *>(m_control->parent()));
    }

    bool blinkCursorWhenTextSelected() const override
    {
        return QApplication::style()->styleHint(QStyle::SH_BlinkCursorWhenTextSelected) != 0;
    }

    int startDragDistance() const override
    {
        return QApplication::startDragDistance();
    }

    int doubleClickInterval() const override
    {
        return QApplication::doubleClickInterval();
    }

    QObject *dragSource() const override
    {
        return m_eventWidget;
    }

    void updateInputMethod() override
    {
#ifndef QT_NO_IM
        if (m_eventWidget)
            QGuiApplication::inputMethod()->update(Qt::ImQueryInput);
#endif
    }

    void showToolTip(const QPoint &globalPos, const QString &text) override
    {
#if QT_CONFIG(tooltip)
        QToolTip::showText(globalPos, text, m_widget);
#else
        Q_UNUSED(globalPos)
        Q_UNUSED(text)
#endif
    }

    void showContextMenu(const QPoint &screenPos, const QPointF &docPos) override
    {
#ifdef QT_NO_CONTEXTMENU
        Q_UNUSED(screenPos)
        Q_UNUSED(docPos)
#else
        QMenu *menu = m_control->createStandardContextMenu(docPos, m_widget);
        if (!menu)
            return;
        menu->setAttribute(Qt::WA_DeleteOnClose);
        menu->popup(screenPos);
#endif
    }

    QTextCharFormat focusIndicatorFormat(const QPalette &palette) const override
    {
        QStyleOption opt;
        opt.palette = palette;
        QStyleHintReturnVariant ret;
        QStyle *style = QApplication::style();
        if (m_widget)
            style = m_widget->style();
        style->styleHint(QStyle::SH_TextControl_FocusIndicatorTextCharFormat, &opt, m_widget, &ret);
        return qvariant_cast<QTextFormat>(ret.variant).toCharFormat();
    }

    bool fullWidthSelection() const override
    {
        QStyleOption opt;
        QStyle *style = QApplication::style();
        if (m_widget) {
            opt.initFrom(m_widget);
            style = m_widget->style();
        }
        return style->styleHint(QStyle::SH_RichText_FullWidthSelection, &opt, m_widget) != 0;
    }

private:
    WidgetTextControl *const m_control;
    QPointer<QWidget> m_widget;
    QPointer<QWidget> m_eventWidget;
};

} // namespace

std::unique_ptr<TextEditingHost> createWidgetTextEditingHost(WidgetTextControl *control,
                                                             QWidget *widget,
                                                             QWidget *eventWidget)
{
    return std::make_unique<WidgetTextEditingHost>(control, widget, eventWidget);
}

} // namespace Utils

#include "widgettextcontrolwidgethost.moc"
