// FEMM Qt 6 GUI — Add Arc Segment Dialog
#ifndef ADDARCDLG_H
#define ADDARCDLG_H

#include <QDialog>

class QDoubleSpinBox;
class QComboBox;
class FemmeDocument;

class AddArcDialog : public QDialog
{
    Q_OBJECT

public:
    explicit AddArcDialog(FemmeDocument *doc, QWidget *parent = nullptr);

    double arcAngle() const;
    double maxSegment() const;
    QString boundaryMarker() const;

    // Remember last-used values across invocations
    static double s_lastArcAngle;
    static double s_lastMaxSeg;

private slots:
    void onAccept();

private:
    QDoubleSpinBox *m_arcAngle;
    QDoubleSpinBox *m_maxSeg;
    QComboBox *m_boundary;
};

#endif // ADDARCDLG_H
