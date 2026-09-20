// Copyright (C) 2020 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#ifndef ABSTRACTEDITORDIALOG_H
#define ABSTRACTEDITORDIALOG_H

#include "nodeinstanceglobal.h"

#include <texteditor/texteditor.h>
#include <bindingeditor/bindingeditorwidget.h>

#include <QDialog>

QT_BEGIN_NAMESPACE
class QDialogButtonBox;
class QVBoxLayout;
class QHBoxLayout;
QT_END_NAMESPACE

namespace QmlDesigner {

class AbstractEditorDialog : public QDialog
{
    Q_OBJECT

public:
    AbstractEditorDialog(QWidget *parent = nullptr, const QString &title = tr("Untitled Editor"));
    ~AbstractEditorDialog() override;

    void showWidget();
    void showWidget(int x, int y);

    QString editorValue() const;
    void setEditorValue(const QString &text);

    virtual void adjustProperties()= 0;

    QString defaultTitle() const;

    // The binding's own document, in whichever view shows it.
    BindingDocument *bindingDocument() const
    {
        return m_document;
    }

protected:
    void setupJSEditor();
    void setupUIComponents();

    static bool isNumeric(const TypeName &type);
    static bool isColor(const TypeName &type);
    static bool isVariant(const TypeName &type);

public slots:
    void textChanged();

protected:
    Core::IEditor *m_editor = nullptr;
    BindingDocument *m_document = nullptr;
    QVBoxLayout *m_verticalLayout = nullptr;
    QDialogButtonBox *m_buttonBox = nullptr;
    QHBoxLayout *m_comboBoxLayout = nullptr;
    bool m_lock = false;
    QString m_titleString;

    const QString undefinedString = {"[Undefined]"};
};

}

#endif //ABSTRACTEDITORDIALOG_H
