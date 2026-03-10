// FEMM Qt 6 GUI — Problem Definition Dialog
#ifndef PROBLEMDLG_H
#define PROBLEMDLG_H

#include <QDialog>

class QComboBox;
class QDoubleSpinBox;
class QLineEdit;
class QPlainTextEdit;
class FemmeDocument;

class ProblemDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ProblemDialog(FemmeDocument *doc, QWidget *parent = nullptr);

private slots:
    void onProbTypeChanged(int index);
    void onAccept();

private:
    FemmeDocument *m_doc;

    QComboBox *m_probType;
    QComboBox *m_lengthUnits;
    QDoubleSpinBox *m_frequency;
    QDoubleSpinBox *m_depth;
    QDoubleSpinBox *m_precision;
    QDoubleSpinBox *m_minAngle;
    QComboBox *m_smartMesh;
    QComboBox *m_acSolver;
    QComboBox *m_prevType;
    QLineEdit *m_prevSoln;
    QPlainTextEdit *m_comment;
};

#endif // PROBLEMDLG_H
