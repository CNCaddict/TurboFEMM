// FEMM Qt 6 GUI — Circuit Properties Dialog
#include "circuitdlg.h"
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

CircuitDialog::CircuitDialog(FemmeDocument *doc, FCircuit *circ,
                             QWidget *parent)
    : QDialog(parent), m_doc(doc), m_circ(circ)
{
    setWindowTitle(tr("Circuit Properties"));
    setMinimumWidth(350);

    auto *mainLayout = new QVBoxLayout(this);

    auto *formLayout = new QFormLayout;

    m_name = new QLineEdit(circ->circName);
    formLayout->addRow(tr("Name:"), m_name);

    m_amps_re = new QDoubleSpinBox;
    m_amps_re->setRange(-1e12, 1e12);
    m_amps_re->setDecimals(6);
    m_amps_re->setSuffix(tr(" A"));
    m_amps_re->setValue(circ->amps.re);
    m_amps_re->setToolTip(tr("Real part of the circuit current in amperes.\n"
                              "For DC problems, this is the total current.\n"
                              "For the motor module, this is set automatically\n"
                              "by the commutation algorithm during motion sweeps."));
    formLayout->addRow(tr("Current (real):"), m_amps_re);

    m_amps_im = new QDoubleSpinBox;
    m_amps_im->setRange(-1e12, 1e12);
    m_amps_im->setDecimals(6);
    m_amps_im->setSuffix(tr(" A"));
    m_amps_im->setValue(circ->amps.im);
    m_amps_im->setToolTip(tr("Imaginary part of the circuit current.\n"
                              "Non-zero for AC problems with phase-shifted excitation.\n"
                              "For DC magnetostatic problems, leave at 0."));
    formLayout->addRow(tr("Current (imag):"), m_amps_im);

    m_circType = new QComboBox;
    m_circType->addItem(tr("Parallel"));
    m_circType->addItem(tr("Series"));
    m_circType->setCurrentIndex(circ->circType);
    m_circType->setToolTip(tr("Series: all turns in this circuit carry the same current.\n"
                               "  Total MMF = current * turns (set on block labels).\n"
                               "  Use for motor windings with known phase current.\n"
                               "Parallel: current divides equally among turns.\n"
                               "  Each conductor carries current / N_turns."));
    formLayout->addRow(tr("Circuit type:"), m_circType);

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

    connect(okBtn, &QPushButton::clicked, this, &CircuitDialog::onAccept);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
}

void CircuitDialog::onAccept()
{
    QString newName = m_name->text().trimmed();
    if (newName.isEmpty()) {
        QMessageBox::warning(this, tr("Error"), tr("Circuit name cannot be empty."));
        return;
    }

    // Check uniqueness
    if (newName != m_circ->circName) {
        for (const auto &cp : m_doc->circuitProps) {
            if (&cp != m_circ && cp.circName == newName) {
                QMessageBox::warning(this, tr("Error"),
                    tr("A circuit with this name already exists."));
                return;
            }
        }
    }

    m_circ->circName = newName;
    m_circ->amps = FemmComplex(m_amps_re->value(), m_amps_im->value());
    m_circ->circType = m_circType->currentIndex();
    m_doc->isModified = true;
    accept();
}
