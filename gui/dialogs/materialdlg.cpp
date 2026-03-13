// FEMM Qt 6 GUI — Material Properties Dialog
#include "materialdlg.h"
#include "../document.h"
#include "../femm_types.h"
#include "../materialpresets.h"

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
#include <QScrollArea>

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
    m_muX->setToolTip(tr("Relative permeability in the x-direction.\n"
                          "1.0 = vacuum/air. Typical steel: 1000-8000.\n"
                          "For isotropic materials, set equal to mu_y.\n"
                          "If a B-H curve is defined, this is the initial value."));
    magLayout->addRow(tr("Relative \u03bc_x:"), m_muX);

    m_muY = new QDoubleSpinBox;
    m_muY->setRange(0.0, 1e12);
    m_muY->setDecimals(6);
    m_muY->setValue(mat->mu_y);
    m_muY->setToolTip(tr("Relative permeability in the y-direction.\n"
                          "For isotropic materials, set equal to mu_x."));
    magLayout->addRow(tr("Relative \u03bc_y:"), m_muY);

    m_Hc = new QDoubleSpinBox;
    m_Hc->setRange(0.0, 1e12);
    m_Hc->setDecimals(2);
    m_Hc->setSuffix(tr(" A/m"));
    m_Hc->setValue(mat->H_c);
    m_Hc->setToolTip(tr("Coercive field strength for permanent magnets.\n"
                          "NdFeB: ~900,000 A/m. Ferrite: ~250,000 A/m.\n"
                          "Set to 0 for non-magnetic or soft-magnetic materials.\n"
                          "Magnetization direction is set on the block label."));
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
    m_Jsrc_re->setToolTip(tr("Applied source current density (real part), in MA/m^2.\n"
                              "For circuit-driven coils, leave at 0 — current is\n"
                              "set via the circuit. Use this for volume current sources."));
    elecLayout->addRow(tr("J_src (real):"), m_Jsrc_re);

    m_Jsrc_im = new QDoubleSpinBox;
    m_Jsrc_im->setRange(-1e12, 1e12);
    m_Jsrc_im->setDecimals(6);
    m_Jsrc_im->setSuffix(tr(" MA/m\u00b2"));
    m_Jsrc_im->setValue(mat->Jsrc.im);
    m_Jsrc_im->setToolTip(tr("Applied source current density (imaginary part).\n"
                              "Non-zero for AC problems with phase-shifted sources."));
    elecLayout->addRow(tr("J_src (imag):"), m_Jsrc_im);

    m_Cduct = new QDoubleSpinBox;
    m_Cduct->setRange(0.0, 1e12);
    m_Cduct->setDecimals(6);
    m_Cduct->setSuffix(tr(" MS/m"));
    m_Cduct->setValue(mat->Cduct);
    m_Cduct->setToolTip(tr("Electrical conductivity in mega-siemens per meter.\n"
                            "Copper: 58 MS/m. Aluminum: 38 MS/m.\n"
                            "NdFeB magnets: ~0.667 MS/m.\n"
                            "Set > 0 to enable eddy current losses in solid regions."));
    elecLayout->addRow(tr("Conductivity \u03c3:"), m_Cduct);

    mainLayout->addWidget(elecGroup);

    // --- Lamination / Wire ---
    auto *lamGroup = new QGroupBox(tr("Lamination / Wire"));
    auto *lamLayout = new QFormLayout(lamGroup);

    m_lamType = new QComboBox;
    m_lamType->addItem(tr("Laminated parallel to page / solid"));    // 0
    m_lamType->addItem(tr("Laminated on-edge (\u22A5 to x)"));       // 1
    m_lamType->addItem(tr("Laminated on-edge (\u22A5 to y)"));       // 2
    m_lamType->addItem(tr("Magnet wire (square packing)"));          // 3
    m_lamType->addItem(tr("Magnet wire (square, no skin)"));         // 4
    m_lamType->addItem(tr("Stranded wire (circular)"));              // 5
    m_lamType->addItem(tr("Litz wire"));                             // 6
    m_lamType->addItem(tr("Magnet wire (round)"));                   // 7
    m_lamType->addItem(tr("Stranded wire (rect)"));                  // 8
    m_lamType->addItem(tr("10-mil wire (CW/CCW)"));                  // 9
    m_lamType->setCurrentIndex(mat->lamType);
    m_lamType->setToolTip(
        tr("Type 0 — Laminated parallel to page / solid:\n"
           "  Standard for motor stator/rotor laminations (M19, etc.).\n"
           "  Lams are stacked along the Z axis (out of screen).\n"
           "  Set lam. thickness > 0 for eddy current skin effect.\n"
           "  Set lam. thickness = 0 for solid (no skin correction).\n\n"
           "Types 1,2 — Laminated on-edge (AC problems not supported):\n"
           "  Lams stacked perpendicular to x or y within the page plane.\n\n"
           "Types 3+ — Wire types:\n"
           "  For windings: magnet wire, stranded, litz, etc.\n"
           "  Uses proximity effect model, not bulk permeability."));
    lamLayout->addRow(tr("Type:"), m_lamType);

    m_lamD = new QDoubleSpinBox;
    m_lamD->setRange(0.0, 1e6);
    m_lamD->setDecimals(4);
    m_lamD->setSuffix(tr(" mm"));
    m_lamD->setValue(mat->Lam_d);
    m_lamD->setToolTip(tr("Lamination sheet thickness in mm.\n"
                           "Typical motor steel: 0.35 mm (M-19) or 0.5 mm.\n"
                           "Set to 0 for solid (no skin depth correction)."));
    lamLayout->addRow(tr("Lam. thickness:"), m_lamD);

    m_lamFill = new QDoubleSpinBox;
    m_lamFill->setRange(0.0, 1.0);
    m_lamFill->setDecimals(4);
    m_lamFill->setValue(mat->lamFill);
    m_lamFill->setToolTip(tr("Lamination stacking factor (0 to 1).\n"
                              "Fraction of the volume occupied by steel.\n"
                              "Typical: 0.95-0.98. Accounts for insulation\n"
                              "between lamination sheets."));
    lamLayout->addRow(tr("Fill factor:"), m_lamFill);

    m_nStrands = new QSpinBox;
    m_nStrands->setRange(0, 10000);
    m_nStrands->setValue(mat->nStrands);
    m_nStrands->setToolTip(tr("Number of parallel strands per turn.\n"
                               "For Litz wire or multi-strand conductors.\n"
                               "Set to 1 for solid magnet wire."));
    lamLayout->addRow(tr("Number of strands:"), m_nStrands);

    m_wireD = new QDoubleSpinBox;
    m_wireD->setRange(0.0, 1e6);
    m_wireD->setDecimals(6);
    m_wireD->setSuffix(tr(" mm"));
    m_wireD->setValue(mat->wireD);
    m_wireD->setToolTip(tr("Individual strand/wire diameter in mm.\n"
                            "Used for proximity effect and skin effect calculations."));
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
    m_thetaHn->setToolTip(tr("Hysteresis lag angle for nonlinear B-H curve.\n"
                              "Models energy loss due to hysteresis in AC problems.\n"
                              "Typical: 0-5 deg. Set to 0 for lossless nonlinear."));
    hystLayout->addRow(tr("Theta_hn:"), m_thetaHn);

    m_thetaHx = new QDoubleSpinBox;
    m_thetaHx->setRange(0.0, 90.0);
    m_thetaHx->setDecimals(2);
    m_thetaHx->setSuffix(tr(" deg"));
    m_thetaHx->setValue(mat->Theta_hx);
    m_thetaHx->setToolTip(tr("Hysteresis lag angle for linear mu_x.\n"
                              "Used when no B-H curve is defined."));
    hystLayout->addRow(tr("Theta_hx:"), m_thetaHx);

    m_thetaHy = new QDoubleSpinBox;
    m_thetaHy->setRange(0.0, 90.0);
    m_thetaHy->setDecimals(2);
    m_thetaHy->setSuffix(tr(" deg"));
    m_thetaHy->setValue(mat->Theta_hy);
    m_thetaHy->setToolTip(tr("Hysteresis lag angle for linear mu_y.\n"
                              "Used when no B-H curve is defined."));
    hystLayout->addRow(tr("Theta_hy:"), m_thetaHy);

    mainLayout->addWidget(hystGroup);

    // --- Steinmetz Iron Loss ---
    auto *lossGroup = new QGroupBox(tr("Iron Loss (Steinmetz)"));
    auto *lossLayout = new QFormLayout(lossGroup);

    m_lossPreset = new QComboBox;
    m_lossPreset->addItem(tr("(Custom / None)"));
    for (const auto &p : materialPresets())
        m_lossPreset->addItem(p.name);
    m_lossPreset->setToolTip(tr("Select a standard steel grade to auto-fill\n"
                                 "Steinmetz coefficients and density.\n"
                                 "Choose '(Custom / None)' to enter values manually."));
    lossLayout->addRow(tr("Preset:"), m_lossPreset);

    m_Kh = new QDoubleSpinBox;
    m_Kh->setRange(0.0, 1e12);
    m_Kh->setDecimals(4);
    m_Kh->setValue(mat->Kh);
    m_Kh->setToolTip(tr("Hysteresis loss coefficient (W/m^3 units).\n"
                          "P_hyst = Kh * f * B^alpha.\n"
                          "Typical M-19 steel: ~179."));
    lossLayout->addRow(tr("K_h (hysteresis):"), m_Kh);

    m_Kc = new QDoubleSpinBox;
    m_Kc->setRange(0.0, 1e12);
    m_Kc->setDecimals(6);
    m_Kc->setValue(mat->Kc);
    m_Kc->setToolTip(tr("Classical eddy current loss coefficient (W/m^3 units).\n"
                          "P_eddy = Kc * f^2 * B^2.\n"
                          "Typical M-19 steel: ~0.53."));
    lossLayout->addRow(tr("K_c (eddy):"), m_Kc);

    m_Ke = new QDoubleSpinBox;
    m_Ke->setRange(0.0, 1e12);
    m_Ke->setDecimals(6);
    m_Ke->setValue(mat->Ke);
    m_Ke->setToolTip(tr("Excess (anomalous) loss coefficient (W/m^3 units).\n"
                          "P_excess = Ke * f^1.5 * B^1.5.\n"
                          "Often small or zero. Typical M-19: ~0."));
    lossLayout->addRow(tr("K_e (excess):"), m_Ke);

    m_alphaLoss = new QDoubleSpinBox;
    m_alphaLoss->setRange(1.0, 3.0);
    m_alphaLoss->setDecimals(2);
    m_alphaLoss->setSingleStep(0.1);
    m_alphaLoss->setValue(mat->alpha_loss);
    m_alphaLoss->setToolTip(tr("Steinmetz exponent for hysteresis loss.\n"
                                "P_hyst = Kh * f * B^alpha.\n"
                                "Typical range: 1.5-2.5. Standard value: 2.0."));
    lossLayout->addRow(tr("Alpha (exponent):"), m_alphaLoss);

    m_density = new QDoubleSpinBox;
    m_density->setRange(0.0, 1e6);
    m_density->setDecimals(1);
    m_density->setSuffix(tr(" kg/m\u00b3"));
    m_density->setValue(mat->density);
    m_density->setToolTip(tr("Material mass density in kg/m^3.\n"
                              "Steel: ~7650-7870. NdFeB: ~7500.\n"
                              "Used to convert iron loss from W/m^3 to W/kg."));
    lossLayout->addRow(tr("Density:"), m_density);

    mainLayout->addWidget(lossGroup);

    connect(m_lossPreset, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MaterialDialog::onPresetChanged);

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

    // Lamination fields enabled for lam types 0-2.
    // Type 0 ("parallel to page"): lam thickness > 0 enables skin depth
    // correction; thickness = 0 means solid (no correction). Fill factor
    // scales effective permeability.
    // Types 1-2: on-edge laminations (static only).
    bool isLam = (lt >= 0 && lt <= 2);
    m_lamD->setEnabled(isLam);
    m_lamFill->setEnabled(isLam);

    // Wire fields enabled for wire types 3-9
    bool isWire = (lt >= 3 && lt <= 9);
    m_nStrands->setEnabled(isWire && lt != 3 && lt != 7);  // magnet wire = 1 strand
    m_wireD->setEnabled(isWire);

    // Coercivity: always available (permanent magnets can be laminated)
    // m_Hc->setEnabled(true);
}

void MaterialDialog::onPresetChanged(int index)
{
    if (index <= 0) return;  // 0 = "Custom / None"
    const auto &presets = materialPresets();
    int pi = index - 1;
    if (pi < 0 || pi >= (int)presets.size()) return;

    const auto &p = presets[pi];
    m_Kh->setValue(p.Kh);
    m_Kc->setValue(p.Kc);
    m_Ke->setValue(p.Ke);
    m_alphaLoss->setValue(p.alpha);
    m_density->setValue(p.density);
    m_Cduct->setValue(p.sigma);
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
    m_mat->Kh = m_Kh->value();
    m_mat->Kc = m_Kc->value();
    m_mat->Ke = m_Ke->value();
    m_mat->alpha_loss = m_alphaLoss->value();
    m_mat->density = m_density->value();

    m_doc->isModified = true;
    accept();
}
