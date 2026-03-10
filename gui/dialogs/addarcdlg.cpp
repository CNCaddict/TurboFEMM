// FEMM Qt 6 GUI — Add Arc Segment Dialog
#include "addarcdlg.h"
#include "../document.h"
#include "../femm_types.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>

// Static defaults — persist between dialog invocations
double AddArcDialog::s_lastArcAngle = 90.0;
double AddArcDialog::s_lastMaxSeg = 10.0;

AddArcDialog::AddArcDialog(FemmeDocument *doc, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Add Arc Segment"));
    setMinimumWidth(320);

    auto *mainLayout = new QVBoxLayout(this);
    auto *formLayout = new QFormLayout;

    // Arc angle
    m_arcAngle = new QDoubleSpinBox;
    m_arcAngle->setRange(1.0, 180.0);
    m_arcAngle->setDecimals(2);
    m_arcAngle->setSuffix(tr(" deg"));
    m_arcAngle->setValue(s_lastArcAngle);
    m_arcAngle->setToolTip(tr("Arc angle in degrees (1-180). "
        "Arc goes counterclockwise from first node to second node."));
    formLayout->addRow(tr("Arc angle:"), m_arcAngle);

    // Max segment angle
    m_maxSeg = new QDoubleSpinBox;
    m_maxSeg->setRange(0.01, 10.0);
    m_maxSeg->setDecimals(2);
    m_maxSeg->setSuffix(tr(" deg"));
    m_maxSeg->setValue(s_lastMaxSeg);
    m_maxSeg->setToolTip(tr("Maximum segment angle for mesh discretization of the arc."));
    formLayout->addRow(tr("Max segment:"), m_maxSeg);

    // Boundary condition
    m_boundary = new QComboBox;
    m_boundary->addItem("<None>");
    for (const auto &bp : doc->boundaryProps)
        m_boundary->addItem(bp.bdryName);
    formLayout->addRow(tr("Boundary:"), m_boundary);

    mainLayout->addLayout(formLayout);

    // Info label
    auto *infoLabel = new QLabel(tr(
        "The arc is drawn counterclockwise from the first\n"
        "selected node to the second selected node.\n"
        "Reverse the node selection order to flip direction."));
    infoLabel->setStyleSheet("color: gray; font-size: 11px;");
    mainLayout->addWidget(infoLabel);

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

    connect(okBtn, &QPushButton::clicked, this, &AddArcDialog::onAccept);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
}

double AddArcDialog::arcAngle() const
{
    return m_arcAngle->value();
}

double AddArcDialog::maxSegment() const
{
    return m_maxSeg->value();
}

QString AddArcDialog::boundaryMarker() const
{
    return m_boundary->currentText();
}

void AddArcDialog::onAccept()
{
    // Remember for next time
    s_lastArcAngle = m_arcAngle->value();
    s_lastMaxSeg = m_maxSeg->value();
    accept();
}
