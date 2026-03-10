// FEMM Qt 6 GUI — Circuit Properties Dialog
#ifndef CIRCUITDLG_H
#define CIRCUITDLG_H

#include <QDialog>

class QComboBox;
class QDoubleSpinBox;
class QLineEdit;
class FemmeDocument;
struct FCircuit;

class CircuitDialog : public QDialog
{
    Q_OBJECT

public:
    explicit CircuitDialog(FemmeDocument *doc, FCircuit *circ,
                           QWidget *parent = nullptr);

private slots:
    void onAccept();

private:
    FemmeDocument *m_doc;
    FCircuit *m_circ;

    QLineEdit *m_name;
    QDoubleSpinBox *m_amps_re;
    QDoubleSpinBox *m_amps_im;
    QComboBox *m_circType;
};

#endif // CIRCUITDLG_H
