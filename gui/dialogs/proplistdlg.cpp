// FEMM Qt 6 GUI — Property List Manager Dialog
#include "proplistdlg.h"
#include "materialdlg.h"
#include "boundarydlg.h"
#include "circuitdlg.h"
#include "../document.h"
#include "../femm_types.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QListWidget>
#include <QPushButton>
#include <QLabel>
#include <QMessageBox>

PropertyListDialog::PropertyListDialog(FemmeDocument *doc, PropType type,
                                       QWidget *parent)
    : QDialog(parent), m_doc(doc), m_type(type)
{
    QString title;
    switch (type) {
    case Materials:  title = tr("Material Properties"); break;
    case Boundaries: title = tr("Boundary Conditions"); break;
    case Circuits:   title = tr("Circuit Properties"); break;
    case PointProps: title = tr("Point Properties"); break;
    }
    setWindowTitle(title);
    setMinimumSize(350, 300);

    auto *mainLayout = new QVBoxLayout(this);

    m_list = new QListWidget;
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    mainLayout->addWidget(m_list);

    // Button row
    auto *btnLayout = new QHBoxLayout;
    m_addBtn = new QPushButton(tr("Add New"));
    m_editBtn = new QPushButton(tr("Edit"));
    m_deleteBtn = new QPushButton(tr("Delete"));
    auto *closeBtn = new QPushButton(tr("Close"));

    btnLayout->addWidget(m_addBtn);
    btnLayout->addWidget(m_editBtn);
    btnLayout->addWidget(m_deleteBtn);
    btnLayout->addStretch();
    btnLayout->addWidget(closeBtn);
    mainLayout->addLayout(btnLayout);

    connect(m_addBtn, &QPushButton::clicked, this, &PropertyListDialog::onAdd);
    connect(m_editBtn, &QPushButton::clicked, this, &PropertyListDialog::onEdit);
    connect(m_deleteBtn, &QPushButton::clicked, this, &PropertyListDialog::onDelete);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    connect(m_list, &QListWidget::itemDoubleClicked, this, &PropertyListDialog::onEdit);

    refreshList();
}

void PropertyListDialog::refreshList()
{
    m_list->clear();
    switch (m_type) {
    case Materials:
        for (const auto &mp : m_doc->materialProps)
            m_list->addItem(mp.blockName);
        break;
    case Boundaries:
        for (const auto &bp : m_doc->boundaryProps)
            m_list->addItem(bp.bdryName);
        break;
    case Circuits:
        for (const auto &cp : m_doc->circuitProps)
            m_list->addItem(cp.circName);
        break;
    case PointProps:
        for (const auto &pp : m_doc->pointProps)
            m_list->addItem(pp.pointName);
        break;
    }
}

void PropertyListDialog::onAdd()
{
    switch (m_type) {
    case Materials: {
        FMaterialProp newMat;
        newMat.blockName = QString("New Material %1").arg(m_doc->materialProps.size() + 1);
        m_doc->materialProps.push_back(newMat);
        MaterialDialog dlg(m_doc, &m_doc->materialProps.back(), this);
        if (dlg.exec() != QDialog::Accepted) {
            m_doc->materialProps.pop_back();
        }
        break;
    }
    case Boundaries: {
        FBoundaryProp newBdry;
        newBdry.bdryName = QString("New Boundary %1").arg(m_doc->boundaryProps.size() + 1);
        m_doc->boundaryProps.push_back(newBdry);
        BoundaryDialog dlg(m_doc, &m_doc->boundaryProps.back(), this);
        if (dlg.exec() != QDialog::Accepted) {
            m_doc->boundaryProps.pop_back();
        }
        break;
    }
    case Circuits: {
        FCircuit newCirc;
        newCirc.circName = QString("New Circuit %1").arg(m_doc->circuitProps.size() + 1);
        m_doc->circuitProps.push_back(newCirc);
        CircuitDialog dlg(m_doc, &m_doc->circuitProps.back(), this);
        if (dlg.exec() != QDialog::Accepted) {
            m_doc->circuitProps.pop_back();
        }
        break;
    }
    case PointProps: {
        // Point properties not yet implemented as a dialog
        FPointProp newPP;
        newPP.pointName = QString("New Point %1").arg(m_doc->pointProps.size() + 1);
        m_doc->pointProps.push_back(newPP);
        m_doc->isModified = true;
        break;
    }
    }
    refreshList();
}

void PropertyListDialog::onEdit()
{
    int row = m_list->currentRow();
    if (row < 0) return;

    switch (m_type) {
    case Materials: {
        if (row >= (int)m_doc->materialProps.size()) return;
        MaterialDialog dlg(m_doc, &m_doc->materialProps[row], this);
        dlg.exec();
        break;
    }
    case Boundaries: {
        if (row >= (int)m_doc->boundaryProps.size()) return;
        BoundaryDialog dlg(m_doc, &m_doc->boundaryProps[row], this);
        dlg.exec();
        break;
    }
    case Circuits: {
        if (row >= (int)m_doc->circuitProps.size()) return;
        CircuitDialog dlg(m_doc, &m_doc->circuitProps[row], this);
        dlg.exec();
        break;
    }
    case PointProps:
        // Point properties editing not yet implemented
        break;
    }
    refreshList();
}

void PropertyListDialog::onDelete()
{
    int row = m_list->currentRow();
    if (row < 0) return;

    QString name;
    switch (m_type) {
    case Materials:
        if (row >= (int)m_doc->materialProps.size()) return;
        name = m_doc->materialProps[row].blockName;
        break;
    case Boundaries:
        if (row >= (int)m_doc->boundaryProps.size()) return;
        name = m_doc->boundaryProps[row].bdryName;
        break;
    case Circuits:
        if (row >= (int)m_doc->circuitProps.size()) return;
        name = m_doc->circuitProps[row].circName;
        break;
    case PointProps:
        if (row >= (int)m_doc->pointProps.size()) return;
        name = m_doc->pointProps[row].pointName;
        break;
    }

    int ret = QMessageBox::question(this, tr("Confirm Delete"),
        tr("Delete \"%1\"? Any geometry referencing it will be reset to <None>.").arg(name),
        QMessageBox::Yes | QMessageBox::No);
    if (ret != QMessageBox::Yes) return;

    switch (m_type) {
    case Materials:
        // Clear references in block labels
        for (auto &bl : m_doc->blockLabels) {
            if (bl.blockType == name) bl.blockType = "<None>";
        }
        m_doc->materialProps.erase(m_doc->materialProps.begin() + row);
        break;
    case Boundaries:
        // Clear references in segments and arcs
        for (auto &seg : m_doc->segments) {
            if (seg.boundaryMarker == name) seg.boundaryMarker = "<None>";
        }
        for (auto &arc : m_doc->arcSegments) {
            if (arc.boundaryMarker == name) arc.boundaryMarker = "<None>";
        }
        m_doc->boundaryProps.erase(m_doc->boundaryProps.begin() + row);
        break;
    case Circuits:
        // Clear references in block labels
        for (auto &bl : m_doc->blockLabels) {
            if (bl.inCircuit == name) bl.inCircuit = "<None>";
        }
        m_doc->circuitProps.erase(m_doc->circuitProps.begin() + row);
        break;
    case PointProps:
        // Clear references in nodes
        for (auto &nd : m_doc->nodes) {
            if (nd.boundaryMarker == name) nd.boundaryMarker = "<None>";
        }
        m_doc->pointProps.erase(m_doc->pointProps.begin() + row);
        break;
    }

    m_doc->isModified = true;
    m_doc->hasMesh = false;
    refreshList();
}
