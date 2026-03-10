// FEMM Qt 6 GUI — Segment Properties Dialog
#ifndef SEGPROPSDLG_H
#define SEGPROPSDLG_H

#include <QDialog>

class QComboBox;
class QDoubleSpinBox;
class QSpinBox;
class QCheckBox;
class FemmeDocument;
struct FSegment;

class SegmentPropsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SegmentPropsDialog(FemmeDocument *doc, FSegment *seg,
                                QWidget *parent = nullptr);

private slots:
    void onAutoMeshChanged(int state);
    void onAccept();

private:
    FemmeDocument *m_doc;
    FSegment *m_seg;

    QComboBox *m_boundary;
    QDoubleSpinBox *m_meshSize;
    QCheckBox *m_autoMesh;
    QCheckBox *m_hide;
    QSpinBox *m_inGroup;
};

#endif // SEGPROPSDLG_H
