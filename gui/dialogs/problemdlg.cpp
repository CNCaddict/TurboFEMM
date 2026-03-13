// FEMM Qt 6 GUI — Problem Definition Dialog
#include "problemdlg.h"
#include "../document.h"
#include "../femm_types.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QLabel>

ProblemDialog::ProblemDialog(FemmeDocument *doc, QWidget *parent)
    : QDialog(parent), m_doc(doc)
{
    setWindowTitle(tr("Problem Definition"));
    setMinimumWidth(420);

    auto *mainLayout = new QVBoxLayout(this);

    // --- Problem type group ---
    auto *typeGroup = new QGroupBox(tr("Problem Type"));
    auto *typeLayout = new QFormLayout(typeGroup);

    m_probType = new QComboBox;
    m_probType->addItem(tr("Planar"));
    m_probType->addItem(tr("Axisymmetric"));
    m_probType->setCurrentIndex(static_cast<int>(doc->problemType));
    m_probType->setToolTip(tr("Planar: 2D cross-section with uniform depth (XY plane).\n"
                               "Axisymmetric: 2D cross-section revolved around the Y axis.\n"
                               "Most motors use Planar. Solenoids/actuators use Axisymmetric."));
    typeLayout->addRow(tr("Problem type:"), m_probType);

    m_lengthUnits = new QComboBox;
    m_lengthUnits->addItem(tr("Inches"));
    m_lengthUnits->addItem(tr("Millimeters"));
    m_lengthUnits->addItem(tr("Centimeters"));
    m_lengthUnits->addItem(tr("Meters"));
    m_lengthUnits->addItem(tr("Mils"));
    m_lengthUnits->addItem(tr("Microns"));
    m_lengthUnits->setCurrentIndex(static_cast<int>(doc->lengthUnits));
    m_lengthUnits->setToolTip(tr("Units for all geometric coordinates and dimensions.\n"
                                  "Also determines the Depth unit (planar problems).\n"
                                  "Most motor models use Millimeters."));
    typeLayout->addRow(tr("Length units:"), m_lengthUnits);

    m_frequency = new QDoubleSpinBox;
    m_frequency->setRange(0.0, 1e12);
    m_frequency->setDecimals(4);
    m_frequency->setSuffix(tr(" Hz"));
    m_frequency->setValue(doc->frequency);
    m_frequency->setToolTip(tr("Excitation frequency for AC (harmonic) problems.\n"
                                "Set to 0 for DC / magnetostatic analysis.\n"
                                "Non-zero enables eddy current and skin effects."));
    typeLayout->addRow(tr("Frequency:"), m_frequency);

    m_depth = new QDoubleSpinBox;
    m_depth->setRange(0.0, 1e12);
    m_depth->setDecimals(6);
    m_depth->setValue(doc->depth);
    m_depth->setToolTip(tr("Stack length (out-of-plane depth) for planar problems.\n"
                            "In the same units as Length Units above.\n"
                            "Example: 15 mm motor stack = 15 (if units are mm).\n"
                            "Affects computed force, torque, energy, and losses."));
    m_depth->setEnabled(doc->problemType == ProblemType::Planar);
    typeLayout->addRow(tr("Depth:"), m_depth);

    mainLayout->addWidget(typeGroup);

    // --- Solver settings group ---
    auto *solverGroup = new QGroupBox(tr("Solver Settings"));
    auto *solverLayout = new QFormLayout(solverGroup);

    m_precision = new QDoubleSpinBox;
    m_precision->setRange(1e-16, 1e-1);
    m_precision->setDecimals(16);
    m_precision->setValue(doc->precision);
    m_precision->setToolTip(tr("Solver convergence tolerance for the linear system.\n"
                                "Smaller = more accurate but slower.\n"
                                "1e-8 is good for most problems.\n"
                                "Use 1e-10 for high-precision force/torque."));
    solverLayout->addRow(tr("Precision:"), m_precision);

    m_minAngle = new QDoubleSpinBox;
    m_minAngle->setRange(1.0, 33.8);
    m_minAngle->setDecimals(1);
    m_minAngle->setSuffix(tr(" deg"));
    m_minAngle->setValue(doc->minAngle);
    m_minAngle->setToolTip(tr("Minimum interior angle for mesh triangles.\n"
                               "Higher = better quality elements but more nodes.\n"
                               "30 deg is the practical maximum (equilateral).\n"
                               "Default ~30 deg gives good quality meshes."));
    solverLayout->addRow(tr("Min mesh angle:"), m_minAngle);

    m_smartMesh = new QComboBox;
    m_smartMesh->addItem(tr("Off"));
    m_smartMesh->addItem(tr("On"));
    m_smartMesh->setCurrentIndex(doc->smartMesh ? 1 : 0);
    m_smartMesh->setToolTip(tr("Smart mesh automatically refines near corners,\n"
                                "boundaries, and high-gradient regions.\n"
                                "Recommended: On for most problems."));
    solverLayout->addRow(tr("Smart mesh:"), m_smartMesh);

    m_acSolver = new QComboBox;
    m_acSolver->addItem(tr("Successive Approximation"));
    m_acSolver->addItem(tr("Newton"));
    m_acSolver->setCurrentIndex(doc->acSolver);
    m_acSolver->setToolTip(tr("Nonlinear iteration method for AC problems.\n"
                               "Successive Approximation: robust, slower convergence.\n"
                               "Newton: faster but may not converge for some problems."));
    solverLayout->addRow(tr("AC solver:"), m_acSolver);

    mainLayout->addWidget(solverGroup);

    // --- Previous solution ---
    auto *prevGroup = new QGroupBox(tr("Previous Solution"));
    auto *prevLayout = new QFormLayout(prevGroup);

    m_prevType = new QComboBox;
    m_prevType->addItem(tr("None"));
    m_prevType->addItem(tr("Incremental Permeability"));
    m_prevType->addItem(tr("Frozen Permeability"));
    m_prevType->setCurrentIndex(doc->prevType);
    m_prevType->setToolTip(tr("Use a previous solution to linearize nonlinear materials.\n"
                               "None: standard nonlinear solve (default).\n"
                               "Incremental Permeability: linearize around previous B.\n"
                               "Frozen Permeability: use previous permeability as-is."));
    prevLayout->addRow(tr("Type:"), m_prevType);

    m_prevSoln = new QLineEdit(doc->prevSoln);
    m_prevSoln->setPlaceholderText(tr("Path to previous .ans file"));
    m_prevSoln->setToolTip(tr("Path to the .ans results file from a previous solve.\n"
                               "Required for Incremental or Frozen Permeability modes."));
    prevLayout->addRow(tr("Solution file:"), m_prevSoln);

    mainLayout->addWidget(prevGroup);

    // --- Comment ---
    auto *commentLabel = new QLabel(tr("Comment / Notes:"));
    m_comment = new QPlainTextEdit;
    m_comment->setMaximumHeight(80);
    m_comment->setPlainText(doc->comment);
    mainLayout->addWidget(commentLabel);
    mainLayout->addWidget(m_comment);

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

    connect(m_probType, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ProblemDialog::onProbTypeChanged);
    connect(okBtn, &QPushButton::clicked, this, &ProblemDialog::onAccept);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
}

void ProblemDialog::onProbTypeChanged(int index)
{
    // Depth only applies to planar problems
    m_depth->setEnabled(index == 0);
}

void ProblemDialog::onAccept()
{
    m_doc->problemType = static_cast<ProblemType>(m_probType->currentIndex());
    m_doc->lengthUnits = static_cast<LengthUnits>(m_lengthUnits->currentIndex());
    m_doc->frequency = std::fabs(m_frequency->value());
    m_doc->depth = m_depth->value();
    m_doc->precision = m_precision->value();
    m_doc->minAngle = m_minAngle->value();
    m_doc->smartMesh = m_smartMesh->currentIndex();
    m_doc->acSolver = m_acSolver->currentIndex();
    m_doc->prevType = m_prevType->currentIndex();
    m_doc->prevSoln = m_prevSoln->text();
    m_doc->comment = m_comment->toPlainText();
    m_doc->isModified = true;
    accept();
}
