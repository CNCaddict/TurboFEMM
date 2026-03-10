// FEMM Qt 6 GUI — Arc Segment Properties Dialog
#ifndef ARCPROPSDLG_H
#define ARCPROPSDLG_H

#include <QDialog>

class QComboBox;
class QDoubleSpinBox;
class QSpinBox;
class QCheckBox;
class FemmeDocument;
struct FArcSegment;

class ArcPropsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ArcPropsDialog(FemmeDocument *doc, FArcSegment *arc,
                            QWidget *parent = nullptr);

private slots:
    void onAccept();

private:
    FemmeDocument *m_doc;
    FArcSegment *m_arc;

    QComboBox *m_boundary;
    QDoubleSpinBox *m_arcLength;
    QDoubleSpinBox *m_maxSeg;
    QCheckBox *m_hide;
    QSpinBox *m_inGroup;
};

#endif // ARCPROPSDLG_H
