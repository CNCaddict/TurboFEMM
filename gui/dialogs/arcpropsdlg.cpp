// FEMM Qt 6 GUI — Arc Segment Properties Dialog
#include "arcpropsdlg.h"
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

ArcPropsDialog::ArcPropsDialog(FemmeDocument *doc, FArcSegment *arc,
                               QWidget *parent)
    : QDialog(parent), m_doc(doc), m_arc(arc)
{
    setWindowTitle(tr("Arc Segment Properties"));
    setMinimumWidth(350);

    auto *mainLayout = new QVBoxLayout(this);
    auto *formLayout = new QFormLayout;

    // Arc length (degrees)
    m_arcLength = new QDoubleSpinBox;
    m_arcLength->setRange(0.01, 360.0);
    m_arcLength->setDecimals(2);
    m_arcLength->setSuffix(tr(" deg"));
    m_arcLength->setValue(arc->arcLength);
    formLayout->addRow(tr("Arc angle:"), m_arcLength);

    // Max segment angle
    m_maxSeg = new QDoubleSpinBox;
    m_maxSeg->setRange(0.01, 10.0);
    m_maxSeg->setDecimals(2);
    m_maxSeg->setSuffix(tr(" deg"));
    m_maxSeg->setValue(arc->maxSideLength);
    m_maxSeg->setToolTip(tr("Maximum segment angle for arc discretization"));
    formLayout->addRow(tr("Max segment:"), m_maxSeg);

    // Boundary condition
    m_boundary = new QComboBox;
    m_boundary->addItem("<None>");
    for (const auto &bp : doc->boundaryProps)
        m_boundary->addItem(bp.bdryName);
    int idx = m_boundary->findText(arc->boundaryMarker);
    if (idx >= 0) m_boundary->setCurrentIndex(idx);
    formLayout->addRow(tr("Boundary condition:"), m_boundary);

    // Hide
    m_hide = new QCheckBox(tr("Hidden in post-processor"));
    m_hide->setChecked(arc->hidden);
    formLayout->addRow(tr("Hide:"), m_hide);

    // Group
    m_inGroup = new QSpinBox;
    m_inGroup->setRange(0, 10000);
    m_inGroup->setValue(arc->inGroup);
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

    connect(okBtn, &QPushButton::clicked, this, &ArcPropsDialog::onAccept);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
}

void ArcPropsDialog::onAccept()
{
    m_arc->arcLength = m_arcLength->value();
    m_arc->maxSideLength = m_maxSeg->value();
    m_arc->boundaryMarker = m_boundary->currentText();
    m_arc->hidden = m_hide->isChecked();
    m_arc->inGroup = m_inGroup->value();
    m_doc->isModified = true;
    m_doc->hasMesh = false;
    accept();
}
