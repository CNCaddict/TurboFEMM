// FEMM Qt 6 GUI — Node Properties Dialog
#ifndef NODEPROPSDLG_H
#define NODEPROPSDLG_H

#include <QDialog>

class QComboBox;
class QSpinBox;
class QLineEdit;
class FemmeDocument;
struct FNode;

class NodePropsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit NodePropsDialog(FemmeDocument *doc, FNode *node,
                             QWidget *parent = nullptr);

private slots:
    void onAccept();

private:
    FemmeDocument *m_doc;
    FNode *m_node;

    QLineEdit *m_name;
    QComboBox *m_pointProp;
    QSpinBox *m_inGroup;
};

#endif // NODEPROPSDLG_H
