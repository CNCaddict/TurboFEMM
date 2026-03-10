// FEMM Qt 6 GUI — Block Label Properties Dialog
#ifndef BLOCKPROPSDLG_H
#define BLOCKPROPSDLG_H

#include <QDialog>

class QComboBox;
class QDoubleSpinBox;
class QSpinBox;
class QCheckBox;
class QLineEdit;
class FemmeDocument;
struct FBlockLabel;

class BlockPropsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit BlockPropsDialog(FemmeDocument *doc, FBlockLabel *label,
                              QWidget *parent = nullptr);

private slots:
    void onAccept();

private:
    FemmeDocument *m_doc;
    FBlockLabel *m_label;

    QLineEdit *m_name;
    QComboBox *m_blockType;
    QDoubleSpinBox *m_maxArea;
    QComboBox *m_inCircuit;
    QDoubleSpinBox *m_magDir;
    QSpinBox *m_turns;
    QCheckBox *m_isExternal;
};

#endif // BLOCKPROPSDLG_H
