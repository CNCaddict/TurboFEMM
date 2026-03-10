// FEMM Qt 6 GUI — Node Properties Dialog
#include "nodepropsdlg.h"
#include "../document.h"
#include "../femm_types.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QComboBox>
#include <QSpinBox>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>

NodePropsDialog::NodePropsDialog(FemmeDocument *doc, FNode *node,
                                 QWidget *parent)
    : QDialog(parent), m_doc(doc), m_node(node)
{
    setWindowTitle(tr("Node Properties"));
    setMinimumWidth(300);

    auto *mainLayout = new QVBoxLayout(this);
    auto *formLayout = new QFormLayout;

    // Position (read-only info)
    auto *posLabel = new QLabel(QString("Position: (%1, %2)")
        .arg(node->x, 0, 'f', 4).arg(node->y, 0, 'f', 4));
    posLabel->setStyleSheet("color: gray;");
    mainLayout->addWidget(posLabel);

    // Name
    m_name = new QLineEdit;
    m_name->setText(node->name);
    m_name->setPlaceholderText(tr("Optional node name"));
    formLayout->addRow(tr("Name:"), m_name);

    // Point property
    m_pointProp = new QComboBox;
    m_pointProp->addItem("<None>");
    for (const auto &pp : doc->pointProps)
        m_pointProp->addItem(pp.pointName);
    int idx = m_pointProp->findText(node->boundaryMarker);
    if (idx >= 0) m_pointProp->setCurrentIndex(idx);
    formLayout->addRow(tr("Point property:"), m_pointProp);

    // Group
    m_inGroup = new QSpinBox;
    m_inGroup->setRange(0, 10000);
    m_inGroup->setValue(node->inGroup);
    formLayout->addRow(tr("Group:"), m_inGroup);

    mainLayout->addLayout(formLayout);

    // --- Buttons ---
    mainLayout->addSpacing(10);
    auto *buttonLayout = new QHBoxLayout;
    auto *okBtn = new QPushButton(tr("OK"));
    auto *cancelBtn = new QPushButton(tr("Cancel"));
    okBtn->setDefault(true);
    buttonLayout->addStretch();
    buttonLayout->addWidget(okBtn);
    buttonLayout->addWidget(cancelBtn);
    mainLayout->addLayout(buttonLayout);

    connect(okBtn, &QPushButton::clicked, this, &NodePropsDialog::onAccept);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
}

void NodePropsDialog::onAccept()
{
    m_node->name = m_name->text().trimmed();
    m_node->boundaryMarker = m_pointProp->currentText();
    m_node->inGroup = m_inGroup->value();
    m_doc->isModified = true;
    accept();
}
