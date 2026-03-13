// FEMM Qt 6 GUI — Model Data Table Dialog
#include "modeldatadlg.h"
#include "../document.h"
#include "../drawingwidget.h"
#include "../femm_types.h"

#include <QTabWidget>
#include <QTableWidget>
#include <QHeaderView>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QComboBox>
#include <QCheckBox>
#include <QLabel>
#include <cmath>

ModelDataDialog::ModelDataDialog(FemmeDocument *doc, DrawingWidget *drawing,
                                 QWidget *parent)
    : QDialog(parent), m_doc(doc), m_drawing(drawing)
{
    setWindowTitle(tr("Model Data"));
    setMinimumSize(900, 500);
    resize(1000, 600);

    auto *layout = new QVBoxLayout(this);

    m_tabs = new QTabWidget;
    layout->addWidget(m_tabs);

    setupTabs();

    // Bottom buttons
    auto *btnLayout = new QHBoxLayout;
    auto *deleteBtn = new QPushButton(tr("Delete Selected"));
    auto *refreshBtn = new QPushButton(tr("Refresh"));
    auto *closeBtn = new QPushButton(tr("Close"));
    btnLayout->addWidget(deleteBtn);
    btnLayout->addStretch();
    btnLayout->addWidget(refreshBtn);
    btnLayout->addWidget(closeBtn);
    layout->addLayout(btnLayout);

    connect(deleteBtn, &QPushButton::clicked, this, &ModelDataDialog::onDeleteSelected);
    connect(refreshBtn, &QPushButton::clicked, this, &ModelDataDialog::refresh);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::close);

    refresh();
}

void ModelDataDialog::setupTabs()
{
    m_blockTable = new QTableWidget;
    m_nodeTable = new QTableWidget;
    m_segTable = new QTableWidget;
    m_arcTable = new QTableWidget;
    m_matTable = new QTableWidget;
    m_bdryTable = new QTableWidget;
    m_circTable = new QTableWidget;

    m_tabs->addTab(m_blockTable, tr("Block Labels"));
    m_tabs->addTab(m_nodeTable, tr("Nodes"));
    m_tabs->addTab(m_segTable, tr("Segments"));
    m_tabs->addTab(m_arcTable, tr("Arc Segments"));
    m_tabs->addTab(m_matTable, tr("Materials"));
    m_tabs->addTab(m_bdryTable, tr("Boundaries"));
    m_tabs->addTab(m_circTable, tr("Circuits"));

    for (auto *t : {m_blockTable, m_nodeTable, m_segTable, m_arcTable,
                    m_matTable, m_bdryTable, m_circTable}) {
        t->setSelectionBehavior(QAbstractItemView::SelectRows);
        t->setSelectionMode(QAbstractItemView::ExtendedSelection);
        t->horizontalHeader()->setStretchLastSection(true);
        t->verticalHeader()->setVisible(false);
    }

    connect(m_blockTable, &QTableWidget::cellChanged, this, &ModelDataDialog::onBlockLabelCellChanged);
    connect(m_nodeTable, &QTableWidget::cellChanged, this, &ModelDataDialog::onNodeCellChanged);
    connect(m_segTable, &QTableWidget::cellChanged, this, &ModelDataDialog::onSegmentCellChanged);
    connect(m_arcTable, &QTableWidget::cellChanged, this, &ModelDataDialog::onArcCellChanged);
    connect(m_matTable, &QTableWidget::cellChanged, this, &ModelDataDialog::onMaterialCellChanged);
    connect(m_bdryTable, &QTableWidget::cellChanged, this, &ModelDataDialog::onBoundaryCellChanged);
    connect(m_circTable, &QTableWidget::cellChanged, this, &ModelDataDialog::onCircuitCellChanged);

    // Selection changes in geometry tabs sync to canvas
    connect(m_blockTable, &QTableWidget::itemSelectionChanged, this, &ModelDataDialog::onTabSelectionChanged);
    connect(m_nodeTable, &QTableWidget::itemSelectionChanged, this, &ModelDataDialog::onTabSelectionChanged);
    connect(m_segTable, &QTableWidget::itemSelectionChanged, this, &ModelDataDialog::onTabSelectionChanged);
    connect(m_arcTable, &QTableWidget::itemSelectionChanged, this, &ModelDataDialog::onTabSelectionChanged);
}

void ModelDataDialog::refresh()
{
    m_updatingTable = true;
    populateBlockLabels();
    populateNodes();
    populateSegments();
    populateArcs();
    populateMaterials();
    populateBoundaries();
    populateCircuits();
    m_updatingTable = false;
}

// ---- Helper: make a read-only item ----
static QTableWidgetItem *roItem(const QString &text)
{
    auto *item = new QTableWidgetItem(text);
    item->setFlags(item->flags() & ~Qt::ItemIsEditable);
    return item;
}

// ---- Block Labels ----
void ModelDataDialog::populateBlockLabels()
{
    m_blockTable->clear();
    m_blockTable->setColumnCount(11);
    m_blockTable->setHorizontalHeaderLabels({
        tr("#"), tr("Name"), tr("X"), tr("Y"), tr("Material"), tr("Circuit"),
        tr("Turns"), tr("Max Area"), tr("Mag Dir"), tr("Group"), tr("Calc Losses")
    });

    int n = (int)m_doc->blockLabels.size();
    m_blockTable->setRowCount(n);

    for (int i = 0; i < n; i++) {
        const auto &bl = m_doc->blockLabels[i];

        m_blockTable->setItem(i, 0, roItem(QString::number(i)));
        m_blockTable->setItem(i, 1, new QTableWidgetItem(bl.name));
        m_blockTable->setItem(i, 2, new QTableWidgetItem(QString::number(bl.x, 'f', 4)));
        m_blockTable->setItem(i, 3, new QTableWidgetItem(QString::number(bl.y, 'f', 4)));

        // Material combo
        auto *matCombo = new QComboBox;
        matCombo->addItem("<None>");
        for (const auto &mp : m_doc->materialProps)
            matCombo->addItem(mp.blockName);
        int idx = matCombo->findText(bl.blockType);
        if (idx >= 0) matCombo->setCurrentIndex(idx);
        connect(matCombo, &QComboBox::currentIndexChanged, this, [this, i](int) {
            if (m_updatingTable) return;
            auto *combo = qobject_cast<QComboBox*>(m_blockTable->cellWidget(i, 4));
            if (combo && i < (int)m_doc->blockLabels.size()) {
                m_doc->blockLabels[i].blockType = combo->currentText();
                markModified();
            }
        });
        m_blockTable->setCellWidget(i, 4, matCombo);

        // Circuit combo
        auto *circCombo = new QComboBox;
        circCombo->addItem("<None>");
        for (const auto &cp : m_doc->circuitProps)
            circCombo->addItem(cp.circName);
        idx = circCombo->findText(bl.inCircuit);
        if (idx >= 0) circCombo->setCurrentIndex(idx);
        connect(circCombo, &QComboBox::currentIndexChanged, this, [this, i](int) {
            if (m_updatingTable) return;
            auto *combo = qobject_cast<QComboBox*>(m_blockTable->cellWidget(i, 5));
            if (combo && i < (int)m_doc->blockLabels.size()) {
                m_doc->blockLabels[i].inCircuit = combo->currentText();
                markModified();
            }
        });
        m_blockTable->setCellWidget(i, 5, circCombo);

        m_blockTable->setItem(i, 6, new QTableWidgetItem(QString::number(bl.turns)));
        m_blockTable->setItem(i, 7, new QTableWidgetItem(QString::number(bl.maxArea, 'f', 4)));
        m_blockTable->setItem(i, 8, new QTableWidgetItem(QString::number(bl.magDir, 'f', 2)));
        m_blockTable->setItem(i, 9, new QTableWidgetItem(QString::number(bl.inGroup)));

        // Calc Losses checkbox
        auto *lossCb = new QCheckBox;
        lossCb->setChecked(bl.calculateLosses);
        connect(lossCb, &QCheckBox::toggled, this, [this, i](bool checked) {
            if (m_updatingTable) return;
            if (i < (int)m_doc->blockLabels.size()) {
                m_doc->blockLabels[i].calculateLosses = checked;
                markModified();
            }
        });
        m_blockTable->setCellWidget(i, 10, lossCb);
    }

    m_blockTable->resizeColumnsToContents();
}

// ---- Nodes ----
void ModelDataDialog::populateNodes()
{
    m_nodeTable->clear();
    m_nodeTable->setColumnCount(6);
    m_nodeTable->setHorizontalHeaderLabels({
        tr("#"), tr("Name"), tr("X"), tr("Y"), tr("Boundary"), tr("Group")
    });

    int n = (int)m_doc->nodes.size();
    m_nodeTable->setRowCount(n);

    for (int i = 0; i < n; i++) {
        const auto &nd = m_doc->nodes[i];

        m_nodeTable->setItem(i, 0, roItem(QString::number(i)));
        m_nodeTable->setItem(i, 1, new QTableWidgetItem(nd.name));
        m_nodeTable->setItem(i, 2, new QTableWidgetItem(QString::number(nd.x, 'f', 6)));
        m_nodeTable->setItem(i, 3, new QTableWidgetItem(QString::number(nd.y, 'f', 6)));

        // Boundary combo (from pointProps)
        auto *bdryCombo = new QComboBox;
        bdryCombo->addItem("<None>");
        for (const auto &pp : m_doc->pointProps)
            bdryCombo->addItem(pp.pointName);
        int idx = bdryCombo->findText(nd.boundaryMarker);
        if (idx >= 0) bdryCombo->setCurrentIndex(idx);
        connect(bdryCombo, &QComboBox::currentIndexChanged, this, [this, i](int) {
            if (m_updatingTable) return;
            auto *combo = qobject_cast<QComboBox*>(m_nodeTable->cellWidget(i, 4));
            if (combo && i < (int)m_doc->nodes.size()) {
                m_doc->nodes[i].boundaryMarker = combo->currentText();
                markModified();
            }
        });
        m_nodeTable->setCellWidget(i, 4, bdryCombo);

        m_nodeTable->setItem(i, 5, new QTableWidgetItem(QString::number(nd.inGroup)));
    }

    m_nodeTable->resizeColumnsToContents();
}

// ---- Segments ----
void ModelDataDialog::populateSegments()
{
    m_segTable->clear();
    m_segTable->setColumnCount(7);
    m_segTable->setHorizontalHeaderLabels({
        tr("#"), tr("Node 0"), tr("Node 1"), tr("Boundary"),
        tr("Max Side"), tr("Hidden"), tr("Group")
    });

    int n = (int)m_doc->segments.size();
    m_segTable->setRowCount(n);

    for (int i = 0; i < n; i++) {
        const auto &seg = m_doc->segments[i];

        m_segTable->setItem(i, 0, roItem(QString::number(i)));
        m_segTable->setItem(i, 1, roItem(QString::number(seg.n0)));
        m_segTable->setItem(i, 2, roItem(QString::number(seg.n1)));

        // Boundary combo
        auto *bdryCombo = new QComboBox;
        bdryCombo->addItem("<None>");
        for (const auto &bp : m_doc->boundaryProps)
            bdryCombo->addItem(bp.bdryName);
        int idx = bdryCombo->findText(seg.boundaryMarker);
        if (idx >= 0) bdryCombo->setCurrentIndex(idx);
        connect(bdryCombo, &QComboBox::currentIndexChanged, this, [this, i](int) {
            if (m_updatingTable) return;
            auto *combo = qobject_cast<QComboBox*>(m_segTable->cellWidget(i, 3));
            if (combo && i < (int)m_doc->segments.size()) {
                m_doc->segments[i].boundaryMarker = combo->currentText();
                markModified();
            }
        });
        m_segTable->setCellWidget(i, 3, bdryCombo);

        m_segTable->setItem(i, 4, new QTableWidgetItem(
            seg.maxSideLength < 0 ? tr("Auto") : QString::number(seg.maxSideLength, 'f', 4)));

        // Hidden checkbox
        auto *hiddenCb = new QCheckBox;
        hiddenCb->setChecked(seg.hidden);
        connect(hiddenCb, &QCheckBox::toggled, this, [this, i](bool checked) {
            if (m_updatingTable) return;
            if (i < (int)m_doc->segments.size()) {
                m_doc->segments[i].hidden = checked;
                markModified();
            }
        });
        m_segTable->setCellWidget(i, 5, hiddenCb);

        m_segTable->setItem(i, 6, new QTableWidgetItem(QString::number(seg.inGroup)));
    }

    m_segTable->resizeColumnsToContents();
}

// ---- Arc Segments ----
void ModelDataDialog::populateArcs()
{
    m_arcTable->clear();
    m_arcTable->setColumnCount(8);
    m_arcTable->setHorizontalHeaderLabels({
        tr("#"), tr("Node 0"), tr("Node 1"), tr("Arc Angle"),
        tr("Max Seg"), tr("Boundary"), tr("Hidden"), tr("Group")
    });

    int n = (int)m_doc->arcSegments.size();
    m_arcTable->setRowCount(n);

    for (int i = 0; i < n; i++) {
        const auto &arc = m_doc->arcSegments[i];

        m_arcTable->setItem(i, 0, roItem(QString::number(i)));
        m_arcTable->setItem(i, 1, roItem(QString::number(arc.n0)));
        m_arcTable->setItem(i, 2, roItem(QString::number(arc.n1)));
        m_arcTable->setItem(i, 3, new QTableWidgetItem(QString::number(arc.arcLength, 'f', 2)));
        m_arcTable->setItem(i, 4, new QTableWidgetItem(QString::number(arc.maxSideLength, 'f', 2)));

        // Boundary combo
        auto *bdryCombo = new QComboBox;
        bdryCombo->addItem("<None>");
        for (const auto &bp : m_doc->boundaryProps)
            bdryCombo->addItem(bp.bdryName);
        int idx = bdryCombo->findText(arc.boundaryMarker);
        if (idx >= 0) bdryCombo->setCurrentIndex(idx);
        connect(bdryCombo, &QComboBox::currentIndexChanged, this, [this, i](int) {
            if (m_updatingTable) return;
            auto *combo = qobject_cast<QComboBox*>(m_arcTable->cellWidget(i, 5));
            if (combo && i < (int)m_doc->arcSegments.size()) {
                m_doc->arcSegments[i].boundaryMarker = combo->currentText();
                markModified();
            }
        });
        m_arcTable->setCellWidget(i, 5, bdryCombo);

        // Hidden checkbox
        auto *hiddenCb = new QCheckBox;
        hiddenCb->setChecked(arc.hidden);
        connect(hiddenCb, &QCheckBox::toggled, this, [this, i](bool checked) {
            if (m_updatingTable) return;
            if (i < (int)m_doc->arcSegments.size()) {
                m_doc->arcSegments[i].hidden = checked;
                markModified();
            }
        });
        m_arcTable->setCellWidget(i, 6, hiddenCb);

        m_arcTable->setItem(i, 7, new QTableWidgetItem(QString::number(arc.inGroup)));
    }

    m_arcTable->resizeColumnsToContents();
}

// ---- Materials ----
void ModelDataDialog::populateMaterials()
{
    m_matTable->clear();
    m_matTable->setColumnCount(9);
    m_matTable->setHorizontalHeaderLabels({
        tr("#"), tr("Name"), tr("mu_x"), tr("mu_y"), tr("H_c"),
        tr("J_src"), tr("Conductivity"), tr("Lam Type"), tr("Lam Fill")
    });

    int n = (int)m_doc->materialProps.size();
    m_matTable->setRowCount(n);

    for (int i = 0; i < n; i++) {
        const auto &mp = m_doc->materialProps[i];

        m_matTable->setItem(i, 0, roItem(QString::number(i)));
        m_matTable->setItem(i, 1, new QTableWidgetItem(mp.blockName));
        m_matTable->setItem(i, 2, new QTableWidgetItem(QString::number(mp.mu_x, 'g', 6)));
        m_matTable->setItem(i, 3, new QTableWidgetItem(QString::number(mp.mu_y, 'g', 6)));
        m_matTable->setItem(i, 4, new QTableWidgetItem(QString::number(mp.H_c, 'g', 6)));
        m_matTable->setItem(i, 5, new QTableWidgetItem(QString::number(mp.Jsrc.re, 'g', 6)));
        m_matTable->setItem(i, 6, new QTableWidgetItem(QString::number(mp.Cduct, 'g', 6)));
        m_matTable->setItem(i, 7, new QTableWidgetItem(QString::number(mp.lamType)));
        m_matTable->setItem(i, 8, new QTableWidgetItem(QString::number(mp.lamFill, 'f', 4)));
    }

    m_matTable->resizeColumnsToContents();
}

// ---- Boundaries ----
void ModelDataDialog::populateBoundaries()
{
    m_bdryTable->clear();
    m_bdryTable->setColumnCount(7);
    m_bdryTable->setHorizontalHeaderLabels({
        tr("#"), tr("Name"), tr("Type"), tr("A0"), tr("A1"), tr("A2"), tr("phi")
    });

    int n = (int)m_doc->boundaryProps.size();
    m_bdryTable->setRowCount(n);

    for (int i = 0; i < n; i++) {
        const auto &bp = m_doc->boundaryProps[i];

        m_bdryTable->setItem(i, 0, roItem(QString::number(i)));
        m_bdryTable->setItem(i, 1, new QTableWidgetItem(bp.bdryName));
        m_bdryTable->setItem(i, 2, new QTableWidgetItem(QString::number(bp.bdryFormat)));
        m_bdryTable->setItem(i, 3, new QTableWidgetItem(QString::number(bp.A0, 'g', 6)));
        m_bdryTable->setItem(i, 4, new QTableWidgetItem(QString::number(bp.A1, 'g', 6)));
        m_bdryTable->setItem(i, 5, new QTableWidgetItem(QString::number(bp.A2, 'g', 6)));
        m_bdryTable->setItem(i, 6, new QTableWidgetItem(QString::number(bp.phi, 'g', 6)));
    }

    m_bdryTable->resizeColumnsToContents();
}

// ---- Circuits ----
void ModelDataDialog::populateCircuits()
{
    m_circTable->clear();
    m_circTable->setColumnCount(5);
    m_circTable->setHorizontalHeaderLabels({
        tr("#"), tr("Name"), tr("Amps (re)"), tr("Amps (im)"), tr("Type")
    });

    int n = (int)m_doc->circuitProps.size();
    m_circTable->setRowCount(n);

    for (int i = 0; i < n; i++) {
        const auto &cp = m_doc->circuitProps[i];

        m_circTable->setItem(i, 0, roItem(QString::number(i)));
        m_circTable->setItem(i, 1, new QTableWidgetItem(cp.circName));
        m_circTable->setItem(i, 2, new QTableWidgetItem(QString::number(cp.amps.re, 'g', 6)));
        m_circTable->setItem(i, 3, new QTableWidgetItem(QString::number(cp.amps.im, 'g', 6)));

        // Type combo
        auto *typeCombo = new QComboBox;
        typeCombo->addItem(tr("Parallel"));
        typeCombo->addItem(tr("Series"));
        typeCombo->setCurrentIndex(cp.circType);
        connect(typeCombo, &QComboBox::currentIndexChanged, this, [this, i](int newIdx) {
            if (m_updatingTable) return;
            if (i < (int)m_doc->circuitProps.size()) {
                m_doc->circuitProps[i].circType = newIdx;
                markModified();
            }
        });
        m_circTable->setCellWidget(i, 4, typeCombo);
    }

    m_circTable->resizeColumnsToContents();
}

// ---- Cell changed handlers ----

void ModelDataDialog::onBlockLabelCellChanged(int row, int col)
{
    if (m_updatingTable) return;
    if (row < 0 || row >= (int)m_doc->blockLabels.size()) return;

    auto &bl = m_doc->blockLabels[row];
    QString text = m_blockTable->item(row, col)->text();
    bool ok;

    switch (col) {
    case 1: bl.name = text.trimmed(); break;
    case 2: { double v = text.toDouble(&ok); if (ok) bl.x = v; } break;
    case 3: { double v = text.toDouble(&ok); if (ok) bl.y = v; } break;
    // cols 4,5 handled by combo signals
    case 6: { int v = text.toInt(&ok); if (ok) bl.turns = v; } break;
    case 7: { double v = text.toDouble(&ok); if (ok) bl.maxArea = v; } break;
    case 8: { double v = text.toDouble(&ok); if (ok) {
        v = std::fmod(v, 360.0); if (v < 0) v += 360.0;
        bl.magDir = v;
    } } break;
    case 9: { int v = text.toInt(&ok); if (ok) bl.inGroup = v; } break;
    default: return;
    }

    markModified();
}

void ModelDataDialog::onNodeCellChanged(int row, int col)
{
    if (m_updatingTable) return;
    if (row < 0 || row >= (int)m_doc->nodes.size()) return;

    auto &nd = m_doc->nodes[row];
    QString text = m_nodeTable->item(row, col)->text();
    bool ok;

    switch (col) {
    case 1: nd.name = text.trimmed(); break;
    case 2: { double v = text.toDouble(&ok); if (ok) nd.x = v; } break;
    case 3: { double v = text.toDouble(&ok); if (ok) nd.y = v; } break;
    // col 4 handled by combo signal
    case 5: { int v = text.toInt(&ok); if (ok) nd.inGroup = v; } break;
    default: return;
    }

    markModified();
}

void ModelDataDialog::onSegmentCellChanged(int row, int col)
{
    if (m_updatingTable) return;
    if (row < 0 || row >= (int)m_doc->segments.size()) return;

    auto &seg = m_doc->segments[row];
    QString text = m_segTable->item(row, col)->text();
    bool ok;

    switch (col) {
    // cols 0-2 read-only, col 3 combo, col 5 checkbox
    case 4: {
        if (text.compare(tr("Auto"), Qt::CaseInsensitive) == 0)
            seg.maxSideLength = -1.0;
        else {
            double v = text.toDouble(&ok);
            if (ok) seg.maxSideLength = v;
        }
    } break;
    case 6: { int v = text.toInt(&ok); if (ok) seg.inGroup = v; } break;
    default: return;
    }

    markModified();
}

void ModelDataDialog::onArcCellChanged(int row, int col)
{
    if (m_updatingTable) return;
    if (row < 0 || row >= (int)m_doc->arcSegments.size()) return;

    auto &arc = m_doc->arcSegments[row];
    QString text = m_arcTable->item(row, col)->text();
    bool ok;

    switch (col) {
    // cols 0-2 read-only, col 5 combo, col 6 checkbox
    case 3: { double v = text.toDouble(&ok); if (ok) arc.arcLength = v; } break;
    case 4: { double v = text.toDouble(&ok); if (ok) arc.maxSideLength = v; } break;
    case 7: { int v = text.toInt(&ok); if (ok) arc.inGroup = v; } break;
    default: return;
    }

    markModified();
}

void ModelDataDialog::onMaterialCellChanged(int row, int col)
{
    if (m_updatingTable) return;
    if (row < 0 || row >= (int)m_doc->materialProps.size()) return;

    auto &mp = m_doc->materialProps[row];
    QString text = m_matTable->item(row, col)->text();
    bool ok;

    switch (col) {
    case 1: mp.blockName = text; break;
    case 2: { double v = text.toDouble(&ok); if (ok) mp.mu_x = v; } break;
    case 3: { double v = text.toDouble(&ok); if (ok) mp.mu_y = v; } break;
    case 4: { double v = text.toDouble(&ok); if (ok) mp.H_c = v; } break;
    case 5: { double v = text.toDouble(&ok); if (ok) mp.Jsrc.re = v; } break;
    case 6: { double v = text.toDouble(&ok); if (ok) mp.Cduct = v; } break;
    case 7: { int v = text.toInt(&ok); if (ok) mp.lamType = v; } break;
    case 8: { double v = text.toDouble(&ok); if (ok) mp.lamFill = v; } break;
    default: return;
    }

    markModified();
}

void ModelDataDialog::onBoundaryCellChanged(int row, int col)
{
    if (m_updatingTable) return;
    if (row < 0 || row >= (int)m_doc->boundaryProps.size()) return;

    auto &bp = m_doc->boundaryProps[row];
    QString text = m_bdryTable->item(row, col)->text();
    bool ok;

    switch (col) {
    case 1: bp.bdryName = text; break;
    case 2: { int v = text.toInt(&ok); if (ok) bp.bdryFormat = v; } break;
    case 3: { double v = text.toDouble(&ok); if (ok) bp.A0 = v; } break;
    case 4: { double v = text.toDouble(&ok); if (ok) bp.A1 = v; } break;
    case 5: { double v = text.toDouble(&ok); if (ok) bp.A2 = v; } break;
    case 6: { double v = text.toDouble(&ok); if (ok) bp.phi = v; } break;
    default: return;
    }

    markModified();
}

void ModelDataDialog::onCircuitCellChanged(int row, int col)
{
    if (m_updatingTable) return;
    if (row < 0 || row >= (int)m_doc->circuitProps.size()) return;

    auto &cp = m_doc->circuitProps[row];
    QString text = m_circTable->item(row, col)->text();
    bool ok;

    switch (col) {
    case 1: cp.circName = text; break;
    case 2: { double v = text.toDouble(&ok); if (ok) cp.amps.re = v; } break;
    case 3: { double v = text.toDouble(&ok); if (ok) cp.amps.im = v; } break;
    // col 4 handled by combo signal
    default: return;
    }

    markModified();
}

// ---- Selection sync: table → canvas ----

void ModelDataDialog::onTabSelectionChanged()
{
    if (m_updatingTable) return;

    m_doc->deselectAll();

    int tabIdx = m_tabs->currentIndex();
    QTableWidget *table = nullptr;

    switch (tabIdx) {
    case 0: table = m_blockTable; break;
    case 1: table = m_nodeTable; break;
    case 2: table = m_segTable; break;
    case 3: table = m_arcTable; break;
    default: return;  // property tabs don't sync selection
    }

    auto selectedRows = table->selectionModel()->selectedRows();
    for (const auto &idx : selectedRows) {
        int row = idx.row();
        switch (tabIdx) {
        case 0:
            if (row < (int)m_doc->blockLabels.size())
                m_doc->blockLabels[row].isSelected = true;
            break;
        case 1:
            if (row < (int)m_doc->nodes.size())
                m_doc->nodes[row].isSelected = true;
            break;
        case 2:
            if (row < (int)m_doc->segments.size())
                m_doc->segments[row].isSelected = true;
            break;
        case 3:
            if (row < (int)m_doc->arcSegments.size())
                m_doc->arcSegments[row].isSelected = true;
            break;
        }
    }

    if (m_drawing) m_drawing->update();
}

// ---- Delete selected ----

void ModelDataDialog::onDeleteSelected()
{
    int tabIdx = m_tabs->currentIndex();

    // For geometry tabs, select items from table rows first
    if (tabIdx <= 3) {
        onTabSelectionChanged();  // sync selection to doc
    }

    int count = 0;
    switch (tabIdx) {
    case 0: count = m_doc->deleteSelectedBlockLabels(); break;
    case 1: count = m_doc->deleteSelectedNodes(); break;
    case 2: count = m_doc->deleteSelectedSegments(); break;
    case 3: count = m_doc->deleteSelectedArcSegments(); break;
    default: return;  // no delete for property tabs
    }

    if (count > 0)
        markModified();

    refresh();
}

// ---- Mark modified ----

void ModelDataDialog::markModified()
{
    m_doc->isModified = true;
    m_doc->hasMesh = false;
    if (m_drawing) m_drawing->update();
}
