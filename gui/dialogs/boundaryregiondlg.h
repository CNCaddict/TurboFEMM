// FEMM Qt 6 GUI — Add Boundary Region Dialog
#ifndef BOUNDARYREGIONDLG_H
#define BOUNDARYREGIONDLG_H

#include <QDialog>

class QComboBox;
class QDoubleSpinBox;

struct BoundaryRegionConfig {
    enum Shape { Circle, Rectangle };
    Shape shape = Circle;
    double distance = 10.0;    // mm from model bounding box
    double meshSize = 0.0;     // 0 = no constraint
};

class BoundaryRegionDialog : public QDialog
{
    Q_OBJECT

public:
    explicit BoundaryRegionDialog(QWidget *parent = nullptr);
    BoundaryRegionConfig config() const;

private slots:
    void onAccept();

private:
    QComboBox      *m_shape;
    QDoubleSpinBox *m_distance;
    QDoubleSpinBox *m_meshSize;
};

#endif // BOUNDARYREGIONDLG_H
