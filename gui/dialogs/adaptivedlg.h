// FEMM Qt 6 GUI — H-Adaptive Refinement Settings Dialog
#ifndef ADAPTIVEDLG_H
#define ADAPTIVEDLG_H

#include <QDialog>

class QSlider;
class QSpinBox;
class QDoubleSpinBox;
class QLabel;

struct AdaptiveConfig;

class AdaptiveDialog : public QDialog
{
    Q_OBJECT

public:
    explicit AdaptiveDialog(QWidget *parent = nullptr);
    AdaptiveConfig config() const;

private slots:
    void onSliderChanged(int value);
    void onToleranceChanged(double percent);
    void onAccept();

private:
    double sliderToTolerance(int value) const;
    int    toleranceToSlider(double tolerance) const;

    QSlider        *m_precisionSlider;
    QLabel         *m_precisionLabel;
    QDoubleSpinBox *m_toleranceSpinBox;
    QSpinBox       *m_maxIterations;
};

#endif // ADAPTIVEDLG_H
