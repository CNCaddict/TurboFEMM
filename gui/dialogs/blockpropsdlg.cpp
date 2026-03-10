// FEMM Qt 6 GUI — Block Label Properties Dialog
#include "blockpropsdlg.h"
#include "../document.h"
#include "../femm_types.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QCheckBox>
#include <QPushButton>
#include <QLabel>
#include <QLineEdit>
#include <QGroupBox>

BlockPropsDialog::BlockPropsDialog(FemmeDocument *doc, FBlockLabel *label,
                                   QWidget *parent)
    : QDialog(parent), m_doc(doc), m_label(label)
{
    setWindowTitle(tr("Block Label Properties"));
    setMinimumWidth(350);

    auto *mainLayout = new QVBoxLayout(this);

    // --- Material / Block type ---
    auto *formLayout = new QFormLayout;

    // --- Name ---
    m_name = new QLineEdit;
    m_name->setText(label->name);
    m_name->setPlaceholderText(tr("Optional label name"));
    formLayout->addRow(tr("Name:"), m_name);

    m_blockType = new QComboBox;
    m_blockType->addItem("<None>");
    for (const auto &mp : doc->materialProps)
        m_blockType->addItem(mp.blockName);
    // Select current
    int idx = m_blockType->findText(label->blockType);
    if (idx >= 0) m_blockType->setCurrentIndex(idx);
    formLayout->addRow(tr("Block type (material):"), m_blockType);

    // --- Max mesh area ---
    m_maxArea = new QDoubleSpinBox;
    m_maxArea->setRange(0.0, 1e12);
    m_maxArea->setDecimals(6);
    m_maxArea->setSpecialValueText(tr("Auto (no constraint)"));
    m_maxArea->setValue(label->maxArea);
    m_maxArea->setToolTip(tr("Maximum triangle area. 0 = automatic."));
    formLayout->addRow(tr("Max mesh area:"), m_maxArea);

    // --- Circuit ---
    m_inCircuit = new QComboBox;
    m_inCircuit->addItem("<None>");
    for (const auto &cp : doc->circuitProps)
        m_inCircuit->addItem(cp.circName);
    idx = m_inCircuit->findText(label->inCircuit);
    if (idx >= 0) m_inCircuit->setCurrentIndex(idx);
    formLayout->addRow(tr("In circuit:"), m_inCircuit);

    // --- Turns ---
    m_turns = new QSpinBox;
    m_turns->setRange(-10000, 10000);
    m_turns->setValue(label->turns);
    formLayout->addRow(tr("Number of turns:"), m_turns);

    // --- Magnetization direction ---
    m_magDir = new QDoubleSpinBox;
    m_magDir->setRange(-360.0, 360.0);
    m_magDir->setDecimals(2);
    m_magDir->setSuffix(tr(" deg"));
    m_magDir->setValue(label->magDir);
    formLayout->addRow(tr("Magnetization dir:"), m_magDir);

    // --- External region ---
    m_isExternal = new QCheckBox(tr("Block is in external region"));
    m_isExternal->setChecked(label->isExternal);

    mainLayout->addLayout(formLayout);
    mainLayout->addWidget(m_isExternal);

    // --- Position display ---
    auto *posLabel = new QLabel(QString("Position: (%1, %2)")
        .arg(label->x, 0, 'f', 4).arg(label->y, 0, 'f', 4));
    posLabel->setStyleSheet("color: gray;");
    mainLayout->addWidget(posLabel);

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

    connect(okBtn, &QPushButton::clicked, this, &BlockPropsDialog::onAccept);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
}

void BlockPropsDialog::onAccept()
{
    m_label->name = m_name->text().trimmed();
    m_label->blockType = m_blockType->currentText();
    m_label->maxArea = m_maxArea->value();
    m_label->inCircuit = m_inCircuit->currentText();
    m_label->turns = m_turns->value();
    m_label->magDir = m_magDir->value();
    m_label->isExternal = m_isExternal->isChecked();
    m_doc->isModified = true;
    m_doc->hasMesh = false;  // mesh is now stale
    accept();
}
