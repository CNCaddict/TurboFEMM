// FEMM Qt 6 GUI — Material Properties Dialog
#ifndef MATERIALDLG_H
#define MATERIALDLG_H

#include <QDialog>

class QComboBox;
class QDoubleSpinBox;
class QSpinBox;
class QLineEdit;
class QPushButton;
class FemmeDocument;
struct FMaterialProp;

class MaterialDialog : public QDialog
{
    Q_OBJECT

public:
    explicit MaterialDialog(FemmeDocument *doc, FMaterialProp *mat,
                            QWidget *parent = nullptr);

private slots:
    void onLamTypeChanged(int index);
    void onPresetChanged(int index);
    void onAccept();

private:
    void updateFieldStates();

    FemmeDocument *m_doc;
    FMaterialProp *m_mat;

    QLineEdit *m_name;
    QDoubleSpinBox *m_muX;
    QDoubleSpinBox *m_muY;
    QDoubleSpinBox *m_Hc;
    QDoubleSpinBox *m_Jsrc_re;
    QDoubleSpinBox *m_Jsrc_im;
    QDoubleSpinBox *m_Cduct;
    QComboBox *m_lamType;
    QDoubleSpinBox *m_lamD;
    QDoubleSpinBox *m_lamFill;
    QDoubleSpinBox *m_thetaHn;
    QDoubleSpinBox *m_thetaHx;
    QDoubleSpinBox *m_thetaHy;
    QSpinBox *m_nStrands;
    QDoubleSpinBox *m_wireD;

    // Steinmetz iron loss
    QComboBox *m_lossPreset;
    QDoubleSpinBox *m_Kh;
    QDoubleSpinBox *m_Kc;
    QDoubleSpinBox *m_Ke;
    QDoubleSpinBox *m_alphaLoss;
    QDoubleSpinBox *m_density;
};

#endif // MATERIALDLG_H
