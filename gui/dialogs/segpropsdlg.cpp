// FEMM Qt 6 GUI — Segment Properties Dialog
#include "segpropsdlg.h"
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

SegmentPropsDialog::SegmentPropsDialog(FemmeDocument *doc, FSegment *seg,
                                       QWidget *parent)
    : QDialog(parent), m_doc(doc), m_seg(seg)
{
    setWindowTitle(tr("Segment Properties"));
    setMinimumWidth(350);

    auto *mainLayout = new QVBoxLayout(this);
    auto *formLayout = new QFormLayout;

    // Boundary condition
    m_boundary = new QComboBox;
    m_boundary->addItem("<None>");
    for (const auto &bp : doc->boundaryProps)
        m_boundary->addItem(bp.bdryName);
    int idx = m_boundary->findText(seg->boundaryMarker);
    if (idx >= 0) m_boundary->setCurrentIndex(idx);
    formLayout->addRow(tr("Boundary condition:"), m_boundary);

    // Mesh size
    m_meshSize = new QDoubleSpinBox;
    m_meshSize->setRange(0.0, 1e12);
    m_meshSize->setDecimals(6);
    m_meshSize->setSpecialValueText(tr("Auto"));
    m_meshSize->setValue(seg->maxSideLength > 0 ? seg->maxSideLength : 0.0);
    formLayout->addRow(tr("Mesh element size:"), m_meshSize);

    // Auto mesh
    m_autoMesh = new QCheckBox(tr("Let mesher decide"));
    m_autoMesh->setChecked(seg->maxSideLength <= 0);
    formLayout->addRow(tr("Auto mesh:"), m_autoMesh);

    // Hide
    m_hide = new QCheckBox(tr("Hidden in post-processor"));
    m_hide->setChecked(seg->hidden);
    formLayout->addRow(tr("Hide:"), m_hide);

    // Group
    m_inGroup = new QSpinBox;
    m_inGroup->setRange(0, 10000);
    m_inGroup->setValue(seg->inGroup);
    formLayout->addRow(tr("Group:"), m_inGroup);

    mainLayout->addLayout(formLayout);

    // Update mesh size enabled state
    m_meshSize->setEnabled(!m_autoMesh->isChecked());

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

    connect(m_autoMesh, &QCheckBox::checkStateChanged,
            this, [this](Qt::CheckState state) { onAutoMeshChanged(static_cast<int>(state)); });
    connect(okBtn, &QPushButton::clicked, this, &SegmentPropsDialog::onAccept);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
}

void SegmentPropsDialog::onAutoMeshChanged(int state)
{
    m_meshSize->setEnabled(state != Qt::Checked);
    if (state == Qt::Checked)
        m_meshSize->setValue(0.0);
}

void SegmentPropsDialog::onAccept()
{
    m_seg->boundaryMarker = m_boundary->currentText();
    m_seg->maxSideLength = m_autoMesh->isChecked() ? -1.0 : m_meshSize->value();
    m_seg->hidden = m_hide->isChecked();
    m_seg->inGroup = m_inGroup->value();
    m_doc->isModified = true;
    m_doc->hasMesh = false;
    accept();
}
