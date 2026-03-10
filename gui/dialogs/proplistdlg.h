// FEMM Qt 6 GUI — Property List Manager Dialog
// Reusable dialog for managing lists of materials, boundaries, circuits, etc.
#ifndef PROPLISTDLG_H
#define PROPLISTDLG_H

#include <QDialog>
#include <functional>

class QListWidget;
class QPushButton;
class FemmeDocument;

class PropertyListDialog : public QDialog
{
    Q_OBJECT

public:
    enum PropType { Materials = 0, Boundaries, Circuits, PointProps };

    explicit PropertyListDialog(FemmeDocument *doc, PropType type,
                                QWidget *parent = nullptr);

private slots:
    void onAdd();
    void onEdit();
    void onDelete();

private:
    void refreshList();

    FemmeDocument *m_doc;
    PropType m_type;

    QListWidget *m_list;
    QPushButton *m_addBtn;
    QPushButton *m_editBtn;
    QPushButton *m_deleteBtn;
};

#endif // PROPLISTDLG_H
