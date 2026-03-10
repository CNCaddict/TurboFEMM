// FEMM Qt 6 GUI — Parametric Motion Dialog
#ifndef MOTIONDIALOG_H
#define MOTIONDIALOG_H

#include <QDialog>
#include <QString>
#include <QStringList>
#include <functional>

class QSpinBox;
class QDoubleSpinBox;
class QComboBox;
class QLineEdit;
class QStackedWidget;
class QCheckBox;
class QGroupBox;
class QPushButton;

struct MotionConfig {
    int groupNumber = 1;
    bool isRotation = false;      // false=translate, true=rotate

    // Translation (computed from totalDistance + directionAngle)
    double dx = 0.0, dy = 0.0;   // translate per step (computed)
    double totalDistance = 0.0;   // total sweep distance (mm)
    double directionAngle = 0.0;  // direction of travel (degrees, 0=+X)

    // Rotation
    double cx = 0.0, cy = 0.0;   // rotation center
    double angle = 0.0;           // rotation angle per step (degrees)

    int numSteps = 10;
    QString outputDir;
    bool saveImages = true;       // save individual PNG frames
    bool saveVideo = true;        // save animated GIF
    bool loopPlayback = false;    // skip last frame for seamless looping
    bool saveCSV = true;          // save CSV results file
    bool csvFluxDensity = true;   // include B_max, B_min, B_avg
    bool csvVectorPotential = true; // include A_max, A_min
    bool csvEnergy = true;        // include Energy_J, Area_m2
    bool csvForceTorque = true;   // include Force_X, Force_Y, Torque

    // --- Motor module ---
    bool motorEnabled = false;
    double motorRmsCurrent = 0.0;       // RMS phase current (A)
    double motorInitialAngle = 0.0;     // initial electrical angle (degrees)
    QString motorPhaseA;                 // circuit name for Phase A
    QString motorPhaseB;                 // circuit name for Phase B
    QString motorPhaseC;                 // circuit name for Phase C
    int motorPolePairs = 1;             // pole pairs (rotation)
    double motorPolePitch = 10.0;       // pole pitch in mm (translation)
    double motorAngleStep = 1.0;        // optimisation step (degrees)
    double motorOptimalAngle = 0.0;     // result (degrees, read-only)
    bool motorOptimized = false;
    bool motorReversePhase = false;     // reverse phase tracking direction
};

// Callback type for phase angle optimisation
// Returns true on success, filling outOptimalAngle.
using OptimizeCallback = std::function<bool(
    double rmsCurrent, double initialAngle, double angleStep,
    int polePairs, const QString &phaseA, const QString &phaseB,
    const QString &phaseC, double &outOptimalAngle, QString &outError)>;

class MotionDialog : public QDialog
{
    Q_OBJECT

public:
    explicit MotionDialog(const QStringList &circuitNames,
                          OptimizeCallback optimizeCallback,
                          QWidget *parent = nullptr);

    MotionConfig config() const;

private slots:
    void onMotionTypeChanged(int index);
    void onBrowseOutput();
    void onAccept();
    void onCSVOptions();
    void onMotorEnabledChanged(bool checked);
    void onMotorOptimise();

    void saveSettings();

private:
    QSpinBox *m_groupNumber;
    QComboBox *m_motionType;
    QStackedWidget *m_paramStack;

    // Translate params
    QDoubleSpinBox *m_totalDistance;
    QDoubleSpinBox *m_directionAngle;

    // Rotate params
    QDoubleSpinBox *m_cx;
    QDoubleSpinBox *m_cy;
    QDoubleSpinBox *m_angle;

    QSpinBox *m_numSteps;
    QLineEdit *m_outputDir;
    QCheckBox *m_saveImages;
    QCheckBox *m_saveVideo;
    QCheckBox *m_loopPlayback;
    QCheckBox *m_saveCSV;

    // CSV column options (persisted but not shown inline)
    bool m_csvFluxDensity = true;
    bool m_csvVectorPotential = true;
    bool m_csvEnergy = true;
    bool m_csvForceTorque = true;

    // --- Motor module widgets ---
    QCheckBox *m_motorEnabled;
    QGroupBox *m_motorGroup;
    QDoubleSpinBox *m_motorRmsCurrent;
    QDoubleSpinBox *m_motorInitialAngle;
    QComboBox *m_motorPhaseA;
    QComboBox *m_motorPhaseB;
    QComboBox *m_motorPhaseC;
    QSpinBox *m_motorPolePairs;
    QDoubleSpinBox *m_motorPolePitch;
    QDoubleSpinBox *m_motorAngleStep;
    QLineEdit *m_motorOptimalAngle;    // read-only display
    QCheckBox *m_motorReversePhase;
    QPushButton *m_motorOptimiseBtn;
    bool m_motorOptimized = false;
    double m_motorOptimalAngleValue = 0.0;

    OptimizeCallback m_optimizeCallback;
};

#endif // MOTIONDIALOG_H
