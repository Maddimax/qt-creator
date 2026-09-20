// Copyright (C) 2020 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "abstracteditordialog.h"

#include <designdocument.h>
#include <qmldesignerplugin.h>

#include <coreplugin/editormanager/ieditor.h>
#include <qmljseditor/qmljseditordocument.h>
#include <texteditor/texteditor.h>
#include <utils/qtcassert.h>

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QPushButton>
#include <QTextDocument>
#include <QVBoxLayout>

namespace QmlDesigner {

AbstractEditorDialog::AbstractEditorDialog(QWidget *parent, const QString &title)
    : QDialog(parent)
    , m_titleString(title)
{
    setWindowFlag(Qt::Tool, true);
    setWindowTitle(defaultTitle());
    setModal(true);

    setupJSEditor();
    setupUIComponents();

    QObject::connect(m_buttonBox, &QDialogButtonBox::accepted,
                     this, &AbstractEditorDialog::accepted);
    QObject::connect(m_buttonBox, &QDialogButtonBox::rejected,
                     this, &AbstractEditorDialog::rejected);
    QObject::connect(m_document, &BindingDocument::returnKeyClicked,
                     this, &AbstractEditorDialog::accepted);
    QObject::connect(m_document->document(), &QTextDocument::contentsChanged,
                     this, &AbstractEditorDialog::textChanged);
}

AbstractEditorDialog::~AbstractEditorDialog()
{
    delete m_editor; // the editor's widget goes with it
    delete m_buttonBox;
    delete m_comboBoxLayout;
    delete m_verticalLayout;
}

void AbstractEditorDialog::showWidget()
{
    this->show();
    this->raise();
    TextEditor::setFocusIn(m_editor);
}

void AbstractEditorDialog::showWidget(int x, int y)
{
    showWidget();
    move(QPoint(x, y));
}

QString AbstractEditorDialog::editorValue() const
{
    if (!m_document)
        return {};

    return m_document->plainText();
}

void AbstractEditorDialog::setEditorValue(const QString &text)
{
    if (m_document)
        m_document->setTextWithIndentation(text);
}

QString AbstractEditorDialog::defaultTitle() const
{
    return m_titleString;
}

void AbstractEditorDialog::setupJSEditor()
{
    static BindingEditorFactory f;
    m_editor = f.createEditor();
    QTC_ASSERT(m_editor, return);
    m_document = qobject_cast<BindingDocument *>(m_editor->document());
    QTC_ASSERT(m_document, return);

    // The design document's, for completion: asked of the document rather
    // than of the widget showing it, so it does not matter which view does.
    if (DesignDocument * const designDocument
        = QmlDesignerPlugin::instance()->currentDesignDocument()) {
        if (Core::IEditor * const designEditor = designDocument->editor()) {
            m_document->setDesignDocument(
                qobject_cast<QmlJSEditor::QmlJSEditorDocument *>(designEditor->document()));
        }
    }

    // In a dialog, Tab is for the buttons.
    TextEditor::setTabMovesFocusIn(m_editor, true);
}

void AbstractEditorDialog::setupUIComponents()
{
    m_verticalLayout = new QVBoxLayout(this);

    m_comboBoxLayout = new QHBoxLayout;

    QWidget * const editorWidget = m_editor->widget();
    editorWidget->setParent(this);
    editorWidget->show();

    m_buttonBox = new QDialogButtonBox(this);
    m_buttonBox->setOrientation(Qt::Horizontal);
    m_buttonBox->setStandardButtons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    m_buttonBox->button(QDialogButtonBox::Ok)->setDefault(true);

    m_verticalLayout->addLayout(m_comboBoxLayout);
    //editor widget has to stretch the most among the other siblings:
    m_verticalLayout->addWidget(editorWidget, 10);
    m_verticalLayout->addWidget(m_buttonBox);

    this->resize(660, 240);
}

bool AbstractEditorDialog::isNumeric(const TypeName &type)
{
    static QList<TypeName> numericTypes = {"double", "int", "real"};
    return numericTypes.contains(type);
}

bool AbstractEditorDialog::isColor(const TypeName &type)
{
    static QList<TypeName> colorTypes = {"QColor", "color"};
    return colorTypes.contains(type);
}

bool AbstractEditorDialog::isVariant(const TypeName &type)
{
    static QList<TypeName> variantTypes = {"alias", "unknown", "variant", "var"};
    return variantTypes.contains(type);
}

void AbstractEditorDialog::textChanged()
{
    if (m_lock)
        return;

    m_lock = true;
    adjustProperties();
    m_lock = false;
}

} // QmlDesigner namespace
