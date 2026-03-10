// FEMM Qt 6 GUI — Material Properties Dialog
#include "materialdlg.h"
#include "../document.h"
#include "../femm_types.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QMessageBox>

MaterialDialog::MaterialDialog(FemmeDocument *doc, FMaterialProp *mat,
                               QWidget *parent)
    : QDialog(parent), m_doc(doc), m_mat(mat)
{
    setWindowTitle(tr("Material Properties"));
    setMinimumWidth(450);

    auto *mainLayout = new QVBoxLayout(this);

    // --- Name ---
    auto *nameLayout = new QFormLayout;
    m_name = new QLineEdit(mat->blockName);
    nameLayout->addRow(tr("Name:"), m_name);
    mainLayout->addLayout(nameLayout);

    // --- Magnetic properties ---
    auto *magGroup = new QGroupBox(tr("Magnetic Properties"));
    auto *magLayout = new QFormLayout(magGroup);

    m_muX = new QDoubleSpinBox;
    m_muX->setRange(0.0, 1e12);
    m_muX->setDecimals(6);
    m_muX->setValue(mat->mu_x);
    magLayout->addRow(tr("Relative \u03bc_x:"), m_muX);

    m_muY = new QDoubleSpinBox;
    m_muY->setRange(0.0, 1e12);
    m_muY->setDecimals(6);
    m_muY->setValue(mat->mu_y);
    magLayout->addRow(tr("Relative \u03bc_y:"), m_muY);

    m_Hc = new QDoubleSpinBox;
    m_Hc->setRange(0.0, 1e12);
    m_Hc->setDecimals(2);
    m_Hc->setSuffix(tr(" A/m"));
    m_Hc->setValue(mat->H_c);
    magLayout->addRow(tr("Coercivity H_c:"), m_Hc);

    mainLayout->addWidget(magGroup);

    // --- Electrical properties ---
    auto *elecGroup = new QGroupBox(tr("Electrical Properties"));
    auto *elecLayout = new QFormLayout(elecGroup);

    m_Jsrc_re = new QDoubleSpinBox;
    m_Jsrc_re->setRange(-1e12, 1e12);
    m_Jsrc_re->setDecimals(6);
    m_Jsrc_re->setSuffix(tr(" MA/m\u00b2"));
    m_Jsrc_re->setValue(mat->Jsrc.re);
    elecLayout->addRow(tr("J_src (real):"), m_Jsrc_re);

    m_Jsrc_im = new QDoubleSpinBox;
    m_Jsrc_im->setRange(-1e12, 1e12);
    m_Jsrc_im->setDecimals(6);
    m_Jsrc_im->setSuffix(tr(" MA/m\u00b2"));
    m_Jsrc_im->setValue(mat->Jsrc.im);
    elecLayout->addRow(tr("J_src (imag):"), m_Jsrc_im);

    m_Cduct = new QDoubleSpinBox;
    m_Cduct->setRange(0.0, 1e12);
    m_Cduct->setDecimals(6);
    m_Cduct->setSuffix(tr(" MS/m"));
    m_Cduct->setValue(mat->Cduct);
    elecLayout->addRow(tr("Conductivity \u03c3:"), m_Cduct);

    mainLayout->addWidget(elecGroup);

    // --- Lamination / Wire ---
    auto *lamGroup = new QGroupBox(tr("Lamination / Wire"));
    auto *lamLayout = new QFormLayout(lamGroup);

    m_lamType = new QComboBox;
    m_lamType->addItem(tr("Not laminated or stranded"));
    m_lamType->addItem(tr("Laminated in-plane (x)"));
    m_lamType->addItem(tr("Laminated in-plane (y)"));
    m_lamType->addItem(tr("Laminated axially"));
    m_lamType->addItem(tr("Magnet wire (square)"));
    m_lamType->addItem(tr("Stranded wire (circular)"));
    m_lamType->addItem(tr("Litz wire"));
    m_lamType->addItem(tr("Magnet wire (round)"));
    m_lamType->addItem(tr("Stranded wire (rect)"));
    m_lamType->addItem(tr("10-mil wire (CW/CCW)"));
    m_lamType->setCurrentIndex(mat->lamType);
    lamLayout->addRow(tr("Type:"), m_lamType);

    m_lamD = new QDoubleSpinBox;
    m_lamD->setRange(0.0, 1e6);
    m_lamD->setDecimals(4);
    m_lamD->setSuffix(tr(" mm"));
    m_lamD->setValue(mat->Lam_d);
    lamLayout->addRow(tr("Lam. thickness:"), m_lamD);

    m_lamFill = new QDoubleSpinBox;
    m_lamFill->setRange(0.0, 1.0);
    m_lamFill->setDecimals(4);
    m_lamFill->setValue(mat->lamFill);
    lamLayout->addRow(tr("Fill factor:"), m_lamFill);

    m_nStrands = new QSpinBox;
    m_nStrands->setRange(0, 10000);
    m_nStrands->setValue(mat->nStrands);
    lamLayout->addRow(tr("Number of strands:"), m_nStrands);

    m_wireD = new QDoubleSpinBox;
    m_wireD->setRange(0.0, 1e6);
    m_wireD->setDecimals(6);
    m_wireD->setSuffix(tr(" mm"));
    m_wireD->setValue(mat->wireD);
    lamLayout->addRow(tr("Strand diameter:"), m_wireD);

    mainLayout->addWidget(lamGroup);

    // --- Hysteresis angles ---
    auto *hystGroup = new QGroupBox(tr("Hysteresis Lag Angles"));
    auto *hystLayout = new QFormLayout(hystGroup);

    m_thetaHn = new QDoubleSpinBox;
    m_thetaHn->setRange(0.0, 90.0);
    m_thetaHn->setDecimals(2);
    m_thetaHn->setSuffix(tr(" deg"));
    m_thetaHn->setValue(mat->Theta_hn);
    hystLayout->addRow(tr("Theta_hn:"), m_thetaHn);

    m_thetaHx = new QDoubleSpinBox;
    m_thetaHx->setRange(0.0, 90.0);
    m_thetaHx->setDecimals(2);
    m_thetaHx->setSuffix(tr(" deg"));
    m_thetaHx->setValue(mat->Theta_hx);
    hystLayout->addRow(tr("Theta_hx:"), m_thetaHx);

    m_thetaHy = new QDoubleSpinBox;
    m_thetaHy->setRange(0.0, 90.0);
    m_thetaHy->setDecimals(2);
    m_thetaHy->setSuffix(tr(" deg"));
    m_thetaHy->setValue(mat->Theta_hy);
    hystLayout->addRow(tr("Theta_hy:"), m_thetaHy);

    mainLayout->addWidget(hystGroup);

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

    connect(m_lamType, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MaterialDialog::onLamTypeChanged);
    connect(okBtn, &QPushButton::clicked, this, &MaterialDialog::onAccept);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);

    updateFieldStates();
}

void MaterialDialog::onLamTypeChanged(int /*index*/)
{
    updateFieldStates();
}

void MaterialDialog::updateFieldStates()
{
    int lt = m_lamType->currentIndex();

    // Lamination fields enabled for lam types 1-3
    bool isLam = (lt >= 1 && lt <= 3);
    m_lamD->setEnabled(isLam);
    m_lamFill->setEnabled(isLam);

    // Wire fields enabled for wire types 4-9
    bool isWire = (lt >= 4 && lt <= 9);
    m_nStrands->setEnabled(isWire && lt != 4 && lt != 7);  // magnet wire = 1 strand
    m_wireD->setEnabled(isWire);

    // Coercivity only for non-laminated, non-wire
    m_Hc->setEnabled(lt == 0);
}

void MaterialDialog::onAccept()
{
    QString newName = m_name->text().trimmed();
    if (newName.isEmpty()) {
        QMessageBox::warning(this, tr("Error"), tr("Material name cannot be empty."));
        return;
    }

    // Check uniqueness (skip if name hasn't changed)
    if (newName != m_mat->blockName) {
        for (const auto &mp : m_doc->materialProps) {
            if (&mp != m_mat && mp.blockName == newName) {
                QMessageBox::warning(this, tr("Error"),
                    tr("A material with this name already exists."));
                return;
            }
        }
    }

    m_mat->blockName = newName;
    m_mat->mu_x = m_muX->value();
    m_mat->mu_y = m_muY->value();
    m_mat->H_c = m_Hc->value();
    m_mat->Jsrc = FemmComplex(m_Jsrc_re->value(), m_Jsrc_im->value());
    m_mat->Cduct = m_Cduct->value();
    m_mat->lamType = m_lamType->currentIndex();
    m_mat->Lam_d = m_lamD->value();
    m_mat->lamFill = m_lamFill->value();
    m_mat->nStrands = m_nStrands->value();
    m_mat->wireD = m_wireD->value();
    m_mat->Theta_hn = m_thetaHn->value();
    m_mat->Theta_hx = m_thetaHx->value();
    m_mat->Theta_hy = m_thetaHy->value();

    m_doc->isModified = true;
    accept();
}
