// FEMM Qt 6 GUI — Boundary Condition Dialog
#include "boundarydlg.h"
#include "../document.h"
#include "../femm_types.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QMessageBox>

BoundaryDialog::BoundaryDialog(FemmeDocument *doc, FBoundaryProp *bprop,
                               QWidget *parent)
    : QDialog(parent), m_doc(doc), m_bprop(bprop)
{
    setWindowTitle(tr("Boundary Condition"));
    setMinimumWidth(400);

    auto *mainLayout = new QVBoxLayout(this);

    // --- Name & type ---
    auto *topLayout = new QFormLayout;
    m_name = new QLineEdit(bprop->bdryName);
    topLayout->addRow(tr("Name:"), m_name);

    m_format = new QComboBox;
    m_format->addItem(tr("Prescribed A"));
    m_format->addItem(tr("Small Skin Depth"));
    m_format->addItem(tr("Mixed"));
    m_format->addItem(tr("Strategic Dual Image"));
    m_format->addItem(tr("Periodic"));
    m_format->addItem(tr("Anti-Periodic"));
    m_format->addItem(tr("Periodic Air Gap"));
    m_format->addItem(tr("Anti-Periodic Air Gap"));
    m_format->setCurrentIndex(bprop->bdryFormat);
    topLayout->addRow(tr("BC type:"), m_format);

    mainLayout->addLayout(topLayout);

    // --- Prescribed A group ---
    auto *prescGroup = new QGroupBox(tr("Prescribed A (A = A0 + A1*r + A2*r^2)"));
    auto *prescLayout = new QFormLayout(prescGroup);

    m_A0 = new QDoubleSpinBox;
    m_A0->setRange(-1e12, 1e12);
    m_A0->setDecimals(6);
    m_A0->setValue(bprop->A0);
    prescLayout->addRow(tr("A0:"), m_A0);

    m_A1 = new QDoubleSpinBox;
    m_A1->setRange(-1e12, 1e12);
    m_A1->setDecimals(6);
    m_A1->setValue(bprop->A1);
    prescLayout->addRow(tr("A1:"), m_A1);

    m_A2 = new QDoubleSpinBox;
    m_A2->setRange(-1e12, 1e12);
    m_A2->setDecimals(6);
    m_A2->setValue(bprop->A2);
    prescLayout->addRow(tr("A2:"), m_A2);

    m_phi = new QDoubleSpinBox;
    m_phi->setRange(-360.0, 360.0);
    m_phi->setDecimals(2);
    m_phi->setSuffix(tr(" deg"));
    m_phi->setValue(bprop->phi);
    prescLayout->addRow(tr("Phase (phi):"), m_phi);

    mainLayout->addWidget(prescGroup);

    // --- Small skin depth group ---
    auto *skinGroup = new QGroupBox(tr("Small Skin Depth"));
    auto *skinLayout = new QFormLayout(skinGroup);

    m_mu = new QDoubleSpinBox;
    m_mu->setRange(0.0, 1e12);
    m_mu->setDecimals(6);
    m_mu->setValue(bprop->Mu);
    skinLayout->addRow(tr("Relative \u03bc:"), m_mu);

    m_sig = new QDoubleSpinBox;
    m_sig->setRange(0.0, 1e12);
    m_sig->setDecimals(6);
    m_sig->setSuffix(tr(" MS/m"));
    m_sig->setValue(bprop->Sig);
    skinLayout->addRow(tr("Conductivity \u03c3:"), m_sig);

    mainLayout->addWidget(skinGroup);

    // --- Mixed BC group ---
    auto *mixedGroup = new QGroupBox(tr("Mixed BC (dA/dn + c0*A = c1)"));
    auto *mixedLayout = new QFormLayout(mixedGroup);

    m_c0_re = new QDoubleSpinBox;
    m_c0_re->setRange(-1e12, 1e12);
    m_c0_re->setDecimals(6);
    m_c0_re->setValue(bprop->c0.re);
    mixedLayout->addRow(tr("c0 (real):"), m_c0_re);

    m_c0_im = new QDoubleSpinBox;
    m_c0_im->setRange(-1e12, 1e12);
    m_c0_im->setDecimals(6);
    m_c0_im->setValue(bprop->c0.im);
    mixedLayout->addRow(tr("c0 (imag):"), m_c0_im);

    m_c1_re = new QDoubleSpinBox;
    m_c1_re->setRange(-1e12, 1e12);
    m_c1_re->setDecimals(6);
    m_c1_re->setValue(bprop->c1.re);
    mixedLayout->addRow(tr("c1 (real):"), m_c1_re);

    m_c1_im = new QDoubleSpinBox;
    m_c1_im->setRange(-1e12, 1e12);
    m_c1_im->setDecimals(6);
    m_c1_im->setValue(bprop->c1.im);
    mixedLayout->addRow(tr("c1 (imag):"), m_c1_im);

    mainLayout->addWidget(mixedGroup);

    // --- Air gap group ---
    auto *gapGroup = new QGroupBox(tr("Air Gap Angles"));
    auto *gapLayout = new QFormLayout(gapGroup);

    m_innerAngle = new QDoubleSpinBox;
    m_innerAngle->setRange(-360.0, 360.0);
    m_innerAngle->setDecimals(4);
    m_innerAngle->setSuffix(tr(" deg"));
    m_innerAngle->setValue(bprop->innerAngle);
    gapLayout->addRow(tr("Inner angle:"), m_innerAngle);

    m_outerAngle = new QDoubleSpinBox;
    m_outerAngle->setRange(-360.0, 360.0);
    m_outerAngle->setDecimals(4);
    m_outerAngle->setSuffix(tr(" deg"));
    m_outerAngle->setValue(bprop->outerAngle);
    gapLayout->addRow(tr("Outer angle:"), m_outerAngle);

    mainLayout->addWidget(gapGroup);

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

    connect(m_format, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &BoundaryDialog::onFormatChanged);
    connect(okBtn, &QPushButton::clicked, this, &BoundaryDialog::onAccept);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);

    updateFieldStates();
}

void BoundaryDialog::onFormatChanged(int /*index*/)
{
    updateFieldStates();
}

void BoundaryDialog::updateFieldStates()
{
    int fmt = m_format->currentIndex();

    // Prescribed A fields: only for format 0
    bool prescA = (fmt == 0);
    m_A0->setEnabled(prescA);
    m_A1->setEnabled(prescA);
    m_A2->setEnabled(prescA);
    m_phi->setEnabled(prescA);

    // Skin depth fields: only for format 1
    bool skin = (fmt == 1);
    m_mu->setEnabled(skin);
    m_sig->setEnabled(skin);

    // Mixed BC fields: only for format 2
    bool mixed = (fmt == 2);
    m_c0_re->setEnabled(mixed);
    m_c0_im->setEnabled(mixed);
    m_c1_re->setEnabled(mixed);
    m_c1_im->setEnabled(mixed);

    // Air gap fields: only for formats 6, 7
    bool gap = (fmt == 6 || fmt == 7);
    m_innerAngle->setEnabled(gap);
    m_outerAngle->setEnabled(gap);
}

void BoundaryDialog::onAccept()
{
    QString newName = m_name->text().trimmed();
    if (newName.isEmpty()) {
        QMessageBox::warning(this, tr("Error"), tr("Boundary name cannot be empty."));
        return;
    }

    // Check uniqueness
    if (newName != m_bprop->bdryName) {
        for (const auto &bp : m_doc->boundaryProps) {
            if (&bp != m_bprop && bp.bdryName == newName) {
                QMessageBox::warning(this, tr("Error"),
                    tr("A boundary condition with this name already exists."));
                return;
            }
        }
    }

    m_bprop->bdryName = newName;
    m_bprop->bdryFormat = m_format->currentIndex();
    m_bprop->A0 = m_A0->value();
    m_bprop->A1 = m_A1->value();
    m_bprop->A2 = m_A2->value();
    m_bprop->phi = m_phi->value();
    m_bprop->Mu = m_mu->value();
    m_bprop->Sig = m_sig->value();
    m_bprop->c0 = FemmComplex(m_c0_re->value(), m_c0_im->value());
    m_bprop->c1 = FemmComplex(m_c1_re->value(), m_c1_im->value());
    m_bprop->innerAngle = m_innerAngle->value();
    m_bprop->outerAngle = m_outerAngle->value();

    m_doc->isModified = true;
    accept();
}
