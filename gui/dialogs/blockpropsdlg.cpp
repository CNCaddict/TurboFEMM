// FEMM Qt 6 GUI — Block Label Properties Dialog
#include "blockpropsdlg.h"
#include "../document.h"
#include "../femm_types.h"

#include <cmath>
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
    m_blockType->setToolTip(tr("Material assigned to this region.\n"
                                "Defines magnetic permeability, conductivity,\n"
                                "and loss properties for all elements in this block.\n"
                                "Create materials via Properties > Materials."));
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
    m_inCircuit->setToolTip(tr("Circuit this block belongs to (for coil regions).\n"
                                "The circuit sets the total current; the turns count\n"
                                "determines current direction per conductor.\n"
                                "Leave as <None> for non-coil regions."));
    formLayout->addRow(tr("In circuit:"), m_inCircuit);

    // --- Turns ---
    m_turns = new QSpinBox;
    m_turns->setRange(-10000, 10000);
    m_turns->setValue(label->turns);
    m_turns->setToolTip(tr("Number of conductor turns in this block.\n"
                            "Positive = current flows out of screen (+Z).\n"
                            "Negative = current flows into screen (-Z).\n"
                            "For series circuits, solver multiplies circuit\n"
                            "current by |turns| for this block's contribution."));
    formLayout->addRow(tr("Number of turns:"), m_turns);

    // --- Magnetization direction ---
    m_magDir = new QDoubleSpinBox;
    m_magDir->setRange(-360.0, 360.0);
    m_magDir->setDecimals(2);
    m_magDir->setSuffix(tr(" deg"));
    m_magDir->setValue(label->magDir);
    m_magDir->setToolTip(tr("Direction of permanent magnet magnetization in degrees.\n"
                             "0 = along +X, 90 = along +Y.\n"
                             "Only applies to materials with coercivity (H_c > 0).\n"
                             "For rotary motors, set per pole: e.g. 0, 180, 0, 180..."));
    formLayout->addRow(tr("Magnetization dir:"), m_magDir);

    // --- Group ---
    m_inGroup = new QSpinBox;
    m_inGroup->setRange(0, 99);
    m_inGroup->setValue(label->inGroup);
    m_inGroup->setToolTip(tr("Group number for motion sweeps and selection.\n"
                              "Elements with the same group move together.\n"
                              "For motors: rotor elements share one group number,\n"
                              "stator elements share another (or use 0 for static)."));
    formLayout->addRow(tr("Group:"), m_inGroup);

    // --- External region ---
    m_isExternal = new QCheckBox(tr("Block is in external region"));
    m_isExternal->setChecked(label->isExternal);
    m_isExternal->setToolTip(tr("Mark this block as part of the external (open boundary) region.\n"
                                 "Used with Kelvin transformation for open-boundary problems."));

    mainLayout->addLayout(formLayout);
    mainLayout->addWidget(m_isExternal);

    // --- Iron loss ---
    auto *lossGroup = new QGroupBox(tr("Iron Loss"));
    auto *lossLayout = new QFormLayout(lossGroup);

    m_calcLosses = new QCheckBox(tr("Calculate iron losses for this block"));
    m_calcLosses->setChecked(label->calculateLosses);
    m_calcLosses->setToolTip(tr("Enable Steinmetz iron loss computation for this block.\n"
                                 "Lamination thickness and stacking factor are set on the material\n"
                                 "(Properties → Materials → Loss tab)."));
    lossLayout->addRow(m_calcLosses);

    mainLayout->addWidget(lossGroup);

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
    double md = std::fmod(m_magDir->value(), 360.0);
    if (md < 0) md += 360.0;
    m_label->magDir = md;
    m_label->isExternal = m_isExternal->isChecked();
    m_label->inGroup = m_inGroup->value();
    m_label->calculateLosses = m_calcLosses->isChecked();
    m_doc->isModified = true;
    m_doc->hasMesh = false;  // mesh is now stale
    accept();
}
