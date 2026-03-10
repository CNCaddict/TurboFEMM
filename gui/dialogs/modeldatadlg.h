// FEMM Qt 6 GUI — Model Data Table Dialog
// Non-modal dialog with tabs for viewing/editing all model geometry and properties
#ifndef MODELDATADLG_H
#define MODELDATADLG_H

#include <QDialog>

class QTabWidget;
class QTableWidget;
class FemmeDocument;
class DrawingWidget;

class ModelDataDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ModelDataDialog(FemmeDocument *doc, DrawingWidget *drawing,
                             QWidget *parent = nullptr);

    void refresh();

private slots:
    void onDeleteSelected();
    void onBlockLabelCellChanged(int row, int col);
    void onNodeCellChanged(int row, int col);
    void onSegmentCellChanged(int row, int col);
    void onArcCellChanged(int row, int col);
    void onMaterialCellChanged(int row, int col);
    void onBoundaryCellChanged(int row, int col);
    void onCircuitCellChanged(int row, int col);
    void onTabSelectionChanged();

private:
    void setupTabs();
    void populateBlockLabels();
    void populateNodes();
    void populateSegments();
    void populateArcs();
    void populateMaterials();
    void populateBoundaries();
    void populateCircuits();

    void markModified();

    FemmeDocument *m_doc;
    DrawingWidget *m_drawing;
    QTabWidget *m_tabs;

    QTableWidget *m_blockTable;
    QTableWidget *m_nodeTable;
    QTableWidget *m_segTable;
    QTableWidget *m_arcTable;
    QTableWidget *m_matTable;
    QTableWidget *m_bdryTable;
    QTableWidget *m_circTable;

    bool m_updatingTable = false;
};

#endif // MODELDATADLG_H
