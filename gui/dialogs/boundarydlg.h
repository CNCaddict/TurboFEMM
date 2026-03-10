// FEMM Qt 6 GUI — Boundary Condition Dialog
#ifndef BOUNDARYDLG_H
#define BOUNDARYDLG_H

#include <QDialog>

class QComboBox;
class QDoubleSpinBox;
class QLineEdit;
class FemmeDocument;
struct FBoundaryProp;

class BoundaryDialog : public QDialog
{
    Q_OBJECT

public:
    explicit BoundaryDialog(FemmeDocument *doc, FBoundaryProp *bprop,
                            QWidget *parent = nullptr);

private slots:
    void onFormatChanged(int index);
    void onAccept();

private:
    void updateFieldStates();

    FemmeDocument *m_doc;
    FBoundaryProp *m_bprop;

    QLineEdit *m_name;
    QComboBox *m_format;

    // Prescribed A fields
    QDoubleSpinBox *m_A0;
    QDoubleSpinBox *m_A1;
    QDoubleSpinBox *m_A2;
    QDoubleSpinBox *m_phi;

    // Small skin depth fields
    QDoubleSpinBox *m_mu;
    QDoubleSpinBox *m_sig;

    // Mixed BC fields
    QDoubleSpinBox *m_c0_re;
    QDoubleSpinBox *m_c0_im;
    QDoubleSpinBox *m_c1_re;
    QDoubleSpinBox *m_c1_im;

    // Air gap fields
    QDoubleSpinBox *m_innerAngle;
    QDoubleSpinBox *m_outerAngle;
};

#endif // BOUNDARYDLG_H
