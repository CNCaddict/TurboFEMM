// FEMM Qt 6 GUI — Parametric Motion Dialog
#include "motiondialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QLineEdit>
#include <QPushButton>
#include <QStackedWidget>
#include <QFileDialog>
#include <QMessageBox>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QSettings>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

MotionDialog::MotionDialog(const QStringList &circuitNames,
                           OptimizeCallback optimizeCallback,
                           QWidget *parent)
    : QDialog(parent)
    , m_optimizeCallback(std::move(optimizeCallback))
{
    setWindowTitle(tr("Parametric Motion"));
    setMinimumWidth(700);

    auto *mainLayout = new QVBoxLayout(this);

    // Two-column layout: left = motion settings, right = motor + output
    auto *columnsLayout = new QHBoxLayout;

    // ===================== LEFT COLUMN =====================
    auto *leftCol = new QVBoxLayout;

    // --- Group selection ---
    auto *groupBox = new QGroupBox(tr("Group"));
    auto *groupLayout = new QFormLayout(groupBox);

    m_groupNumber = new QSpinBox;
    m_groupNumber->setRange(0, 99);
    m_groupNumber->setValue(1);
    m_groupNumber->setToolTip(tr("The group number of elements to move during the sweep.\n"
                                  "All nodes, segments, arcs, and block labels assigned to\n"
                                  "this group will be translated or rotated at each step.\n"
                                  "For motors, this is typically the rotor group."));
    groupLayout->addRow(tr("Group number:"), m_groupNumber);

    leftCol->addWidget(groupBox);

    // --- Motion type ---
    auto *motionBox = new QGroupBox(tr("Motion"));
    auto *motionLayout = new QVBoxLayout(motionBox);

    auto *typeLayout = new QFormLayout;
    m_motionType = new QComboBox;
    m_motionType->addItem(tr("Translate"));
    m_motionType->addItem(tr("Rotate"));
    m_motionType->setToolTip(tr("Translate: move the group linearly along a direction.\n"
                                 "Rotate: spin the group around a center point.\n"
                                 "For rotary motors, use Rotate with center at (0,0)."));
    typeLayout->addRow(tr("Type:"), m_motionType);
    motionLayout->addLayout(typeLayout);

    // Stacked widget for translate/rotate params
    m_paramStack = new QStackedWidget;

    // Page 0: Translate
    auto *translatePage = new QWidget;
    auto *tLayout = new QFormLayout(translatePage);
    m_totalDistance = new QDoubleSpinBox;
    m_totalDistance->setRange(0.0, 1e6);
    m_totalDistance->setDecimals(6);
    m_totalDistance->setValue(1.0);
    m_totalDistance->setSuffix(tr(" mm"));
    m_totalDistance->setToolTip(tr("Total distance to translate over all steps.\n"
                                   "Per-step distance = Total / Number of steps."));
    tLayout->addRow(tr("Total distance:"), m_totalDistance);

    m_directionAngle = new QDoubleSpinBox;
    m_directionAngle->setRange(-360.0, 360.0);
    m_directionAngle->setDecimals(2);
    m_directionAngle->setValue(0.0);
    m_directionAngle->setSuffix(tr(" deg"));
    m_directionAngle->setToolTip(tr("Direction of translation in degrees.\n"
                                     "0 = along +X axis, 90 = along +Y axis."));
    tLayout->addRow(tr("Direction angle:"), m_directionAngle);

    m_paramStack->addWidget(translatePage);

    // Page 1: Rotate
    auto *rotatePage = new QWidget;
    auto *rLayout = new QFormLayout(rotatePage);
    m_cx = new QDoubleSpinBox;
    m_cx->setRange(-1e6, 1e6);
    m_cx->setDecimals(6);
    m_cx->setValue(0.0);
    m_cx->setToolTip(tr("X coordinate of the rotation center (in model units).\n"
                         "For rotary motors, this is typically 0."));
    rLayout->addRow(tr("Center X:"), m_cx);

    m_cy = new QDoubleSpinBox;
    m_cy->setRange(-1e6, 1e6);
    m_cy->setDecimals(6);
    m_cy->setValue(0.0);
    m_cy->setToolTip(tr("Y coordinate of the rotation center (in model units).\n"
                         "For rotary motors, this is typically 0."));
    rLayout->addRow(tr("Center Y:"), m_cy);

    m_angle = new QDoubleSpinBox;
    m_angle->setRange(-36000.0, 36000.0);
    m_angle->setDecimals(2);
    m_angle->setValue(360.0);
    m_angle->setSuffix(tr(" deg"));
    m_angle->setToolTip(tr("Total mechanical angle to sweep over all steps.\n"
                            "For motors: one electrical period = 360 / (pole pairs) mechanical degrees.\n"
                            "Example: 14-pole motor (7 pairs) = 360/7 = 51.43 deg.\n"
                            "Per-step angle = Total / Number of steps."));
    rLayout->addRow(tr("Total angle:"), m_angle);

    m_paramStack->addWidget(rotatePage);

    motionLayout->addWidget(m_paramStack);
    leftCol->addWidget(motionBox);

    connect(m_motionType, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MotionDialog::onMotionTypeChanged);

    // --- Steps ---
    auto *stepsBox = new QGroupBox(tr("Sweep"));
    auto *stepsLayout = new QFormLayout(stepsBox);

    m_numSteps = new QSpinBox;
    m_numSteps->setRange(1, 10000);
    m_numSteps->setValue(10);
    m_numSteps->setToolTip(tr("Number of discrete positions to solve.\n"
                               "More steps = finer resolution but longer computation.\n"
                               "For motor torque ripple, use enough steps to capture\n"
                               "one cogging period (e.g., 12s/14p: period = 4.29 deg)."));
    stepsLayout->addRow(tr("Number of steps:"), m_numSteps);

    leftCol->addWidget(stepsBox);

    // --- Output ---
    auto *outputBox = new QGroupBox(tr("Output"));
    auto *outputVLayout = new QVBoxLayout(outputBox);

    auto *dirLayout = new QHBoxLayout;
    m_outputDir = new QLineEdit;
    m_outputDir->setPlaceholderText(tr("Select output directory..."));
    dirLayout->addWidget(m_outputDir);

    auto *browseBtn = new QPushButton(tr("Browse..."));
    connect(browseBtn, &QPushButton::clicked, this, &MotionDialog::onBrowseOutput);
    dirLayout->addWidget(browseBtn);
    outputVLayout->addLayout(dirLayout);

    m_saveImages = new QCheckBox(tr("Save PNG frames"));
    m_saveImages->setChecked(true);
    m_saveImages->setToolTip(tr("Save a PNG screenshot of the model at each step.\n"
                                 "Files are named step_001.png, step_002.png, etc."));
    outputVLayout->addWidget(m_saveImages);

    m_saveVideo = new QCheckBox(tr("Save animated GIF"));
    m_saveVideo->setChecked(true);
    m_saveVideo->setToolTip(tr("Combine PNG frames into an animated GIF at the end."));
    outputVLayout->addWidget(m_saveVideo);

    m_loopPlayback = new QCheckBox(tr("Loop playback (skip last frame)"));
    m_loopPlayback->setChecked(false);
    m_loopPlayback->setToolTip(tr("For seamless looping, omit the last frame\n"
                                   "(which duplicates the first position)."));
    outputVLayout->addWidget(m_loopPlayback);

    // CSV row: checkbox + options button
    auto *csvLayout = new QHBoxLayout;
    m_saveCSV = new QCheckBox(tr("Save CSV results"));
    m_saveCSV->setChecked(true);
    csvLayout->addWidget(m_saveCSV);

    auto *csvOptsBtn = new QPushButton(tr("CSV Options..."));
    csvOptsBtn->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    connect(csvOptsBtn, &QPushButton::clicked, this, &MotionDialog::onCSVOptions);
    csvLayout->addWidget(csvOptsBtn);
    csvLayout->addStretch();
    outputVLayout->addLayout(csvLayout);

    leftCol->addWidget(outputBox);
    leftCol->addStretch();

    columnsLayout->addLayout(leftCol);

    // ===================== RIGHT COLUMN =====================
    auto *rightCol = new QVBoxLayout;

    // --- Motor Module ---
    m_motorEnabled = new QCheckBox(tr("Enable Motor Module"));
    m_motorEnabled->setChecked(false);
    rightCol->addWidget(m_motorEnabled);

    m_motorGroup = new QGroupBox(tr("3-Phase Motor"));
    m_motorGroup->setEnabled(false);
    auto *motorFormLayout = new QFormLayout(m_motorGroup);

    m_motorRmsCurrent = new QDoubleSpinBox;
    m_motorRmsCurrent->setRange(0.0, 1e6);
    m_motorRmsCurrent->setDecimals(3);
    m_motorRmsCurrent->setSuffix(tr(" A"));
    m_motorRmsCurrent->setValue(1.0);
    m_motorRmsCurrent->setToolTip(tr("RMS phase current in amperes.\n"
                                      "Peak current = RMS * sqrt(2).\n"
                                      "Balanced 3-phase: Ia = Ipk*cos(theta),\n"
                                      "Ib = Ipk*cos(theta - 120), Ic = Ipk*cos(theta + 120)."));
    motorFormLayout->addRow(tr("RMS Current:"), m_motorRmsCurrent);

    m_motorInitialAngle = new QDoubleSpinBox;
    m_motorInitialAngle->setRange(-360.0, 360.0);
    m_motorInitialAngle->setDecimals(2);
    m_motorInitialAngle->setSuffix(tr(" deg"));
    m_motorInitialAngle->setValue(0.0);
    m_motorInitialAngle->setToolTip(tr("Starting electrical angle for the optimizer sweep.\n"
                                        "The optimizer searches from this angle to find\n"
                                        "the angle of maximum motoring torque.\n"
                                        "Usually 0 is fine — the sweep covers 360 degrees."));
    motorFormLayout->addRow(tr("Initial Phase Angle:"), m_motorInitialAngle);

    // Phase assignment combo boxes
    m_motorPhaseA = new QComboBox;
    m_motorPhaseB = new QComboBox;
    m_motorPhaseC = new QComboBox;
    m_motorPhaseA->addItem(tr("<None>"));
    m_motorPhaseB->addItem(tr("<None>"));
    m_motorPhaseC->addItem(tr("<None>"));
    for (const auto &name : circuitNames) {
        m_motorPhaseA->addItem(name);
        m_motorPhaseB->addItem(name);
        m_motorPhaseC->addItem(name);
    }
    m_motorPhaseA->setToolTip(tr("Circuit for Phase A winding.\n"
                                   "Must be a series circuit (CircType=1) in the model.\n"
                                   "The turns count and sign on block labels determine\n"
                                   "winding direction (positive/negative turns)."));
    motorFormLayout->addRow(tr("Phase A Circuit:"), m_motorPhaseA);
    m_motorPhaseB->setToolTip(tr("Circuit for Phase B winding (120 deg electrical lag from A)."));
    motorFormLayout->addRow(tr("Phase B Circuit:"), m_motorPhaseB);
    m_motorPhaseC->setToolTip(tr("Circuit for Phase C winding (120 deg electrical lead from A)."));
    motorFormLayout->addRow(tr("Phase C Circuit:"), m_motorPhaseC);

    m_motorPolePairs = new QSpinBox;
    m_motorPolePairs->setRange(1, 100);
    m_motorPolePairs->setValue(1);
    m_motorPolePairs->setToolTip(tr("Number of magnetic pole pairs on the rotor.\n"
                                     "Pole pairs = Total poles / 2.\n"
                                     "Example: 14-pole motor = 7 pole pairs.\n"
                                     "Determines the ratio between mechanical and\n"
                                     "electrical angle: elec = mech * pole_pairs."));
    motorFormLayout->addRow(tr("Pole Pairs (rotation):"), m_motorPolePairs);

    m_motorPolePitch = new QDoubleSpinBox;
    m_motorPolePitch->setRange(0.001, 1e6);
    m_motorPolePitch->setDecimals(4);
    m_motorPolePitch->setValue(10.0);
    m_motorPolePitch->setSuffix(tr(" mm"));
    m_motorPolePitch->setToolTip(tr("Distance between adjacent poles (for linear motors only).\n"
                                     "Used to compute electrical angle from linear displacement.\n"
                                     "Not used for rotary motors."));
    motorFormLayout->addRow(tr("Pole Pitch (translation):"), m_motorPolePitch);

    m_motorAngleStep = new QDoubleSpinBox;
    m_motorAngleStep->setRange(0.1, 30.0);
    m_motorAngleStep->setDecimals(1);
    m_motorAngleStep->setSuffix(tr(" deg"));
    m_motorAngleStep->setValue(1.0);
    m_motorAngleStep->setToolTip(tr("Electrical angle step for the optimizer sweep.\n"
                                     "Smaller = more precise but slower.\n"
                                     "1-5 deg is typical. The optimizer solves once per step."));
    motorFormLayout->addRow(tr("Optimisation Step:"), m_motorAngleStep);

    // Reverse phase direction
    m_motorReversePhase = new QCheckBox(tr("Reverse phase direction"));
    m_motorReversePhase->setChecked(false);
    m_motorReversePhase->setToolTip(tr("Reverse the commutation tracking direction.\n"
                                        "Try toggling this if the motor produces torque\n"
                                        "opposing the rotation direction.\n"
                                        "This flips the sign of the electrical angle\n"
                                        "advance as the rotor moves."));
    motorFormLayout->addRow(QString(), m_motorReversePhase);

    // Optimise row: button + read-only result
    auto *optLayout = new QHBoxLayout;
    m_motorOptimiseBtn = new QPushButton(tr("Optimise Angle"));
    m_motorOptimiseBtn->setToolTip(tr("Sweep the stator electrical angle at fixed rotor position\n"
                                       "to find the angle that produces maximum motoring torque.\n"
                                       "This solves the model many times — may take a few minutes."));
    connect(m_motorOptimiseBtn, &QPushButton::clicked,
            this, &MotionDialog::onMotorOptimise);
    optLayout->addWidget(m_motorOptimiseBtn);

    m_motorOptimalAngle = new QLineEdit;
    m_motorOptimalAngle->setReadOnly(true);
    m_motorOptimalAngle->setPlaceholderText(tr("Not optimised"));
    optLayout->addWidget(m_motorOptimalAngle);
    motorFormLayout->addRow(tr("Optimal Angle:"), optLayout);

    rightCol->addWidget(m_motorGroup);

    connect(m_motorEnabled, &QCheckBox::toggled,
            this, &MotionDialog::onMotorEnabledChanged);

    // --- Iron Loss ---
    auto *lossBox = new QGroupBox(tr("Iron Loss"));
    auto *lossLayout = new QVBoxLayout(lossBox);

    m_calculateLosses = new QCheckBox(tr("Calculate iron losses"));
    m_calculateLosses->setChecked(false);
    m_calculateLosses->setToolTip(tr("Compute per-element iron losses during motion sweep.\n"
                                      "Requires blocks with 'Calculate iron losses' enabled\n"
                                      "and materials with either loss data or conductivity + density."));
    lossLayout->addWidget(m_calculateLosses);

    m_accurateSolidLosses = new QCheckBox(tr("Accurate solid rotor losses (slower)"));
    m_accurateSolidLosses->setChecked(false);
    m_accurateSolidLosses->setToolTip(tr("Optional slower post-processor for rotating solid steel regions.\n"
                                         "Uses a boundary-driven diffusion model for rotor backiron.\n"
                                         "Leave off for normal fast sweeps."));
    lossLayout->addWidget(m_accurateSolidLosses);

    auto *lossFreqLayout = new QFormLayout;
    m_motorRPM = new QDoubleSpinBox;
    m_motorRPM->setRange(0.0, 1e6);
    m_motorRPM->setDecimals(1);
    m_motorRPM->setSuffix(tr(" RPM"));
    m_motorRPM->setValue(0.0);
    m_motorRPM->setToolTip(tr("Motor speed in RPM. Combined with pole pairs\n"
                               "to compute electrical frequency: f = RPM * polePairs / 60"));
    lossFreqLayout->addRow(tr("Motor RPM:"), m_motorRPM);

    m_operatingFreq = new QDoubleSpinBox;
    m_operatingFreq->setRange(0.0, 1e6);
    m_operatingFreq->setDecimals(2);
    m_operatingFreq->setSuffix(tr(" Hz"));
    m_operatingFreq->setValue(0.0);
    m_operatingFreq->setToolTip(tr("Electrical frequency for iron loss computation.\n"
                                    "Auto-computed from RPM and pole pairs if RPM > 0.\n"
                                    "Set manually for non-motor applications."));
    lossFreqLayout->addRow(tr("Operating frequency:"), m_operatingFreq);
    lossLayout->addLayout(lossFreqLayout);

    auto updateLossControls = [this](bool checked) {
        m_accurateSolidLosses->setEnabled(checked);
        m_motorRPM->setEnabled(checked);
        m_operatingFreq->setEnabled(checked);
    };
    connect(m_calculateLosses, &QCheckBox::toggled,
            this, updateLossControls);

    rightCol->addWidget(lossBox);

    // Auto-compute frequency from RPM and pole pairs
    auto updateFreqFromRPM = [this]() {
        double rpm = m_motorRPM->value();
        if (rpm > 0) {
            int pp = m_motorPolePairs->value();
            m_operatingFreq->setValue(rpm * (double)pp / 60.0);
        }
    };
    connect(m_motorRPM, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, updateFreqFromRPM);

    rightCol->addStretch();

    columnsLayout->addLayout(rightCol);

    mainLayout->addLayout(columnsLayout);

    // --- Buttons ---
    auto *btnLayout = new QHBoxLayout;
    btnLayout->addStretch();

    auto *runBtn = new QPushButton(tr("Run"));
    runBtn->setDefault(true);
    connect(runBtn, &QPushButton::clicked, this, &MotionDialog::onAccept);
    btnLayout->addWidget(runBtn);

    auto *cancelBtn = new QPushButton(tr("Cancel"));
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    btnLayout->addWidget(cancelBtn);

    mainLayout->addLayout(btnLayout);

    // --- Restore saved settings ---
    QSettings settings;
    settings.beginGroup("motion");
    m_groupNumber->setValue(settings.value("groupNumber", 1).toInt());
    m_motionType->setCurrentIndex(settings.value("motionType", 0).toInt());
    m_totalDistance->setValue(settings.value("totalDistance", 1.0).toDouble());
    m_directionAngle->setValue(settings.value("directionAngle", 0.0).toDouble());
    m_cx->setValue(settings.value("cx", 0.0).toDouble());
    m_cy->setValue(settings.value("cy", 0.0).toDouble());
    m_angle->setValue(settings.value("angle", 5.0).toDouble());
    m_numSteps->setValue(settings.value("numSteps", 10).toInt());
    m_outputDir->setText(settings.value("outputDir", QString()).toString());
    m_saveImages->setChecked(settings.value("saveImages", true).toBool());
    m_saveVideo->setChecked(settings.value("saveVideo", true).toBool());
    m_loopPlayback->setChecked(settings.value("loopPlayback", false).toBool());
    m_saveCSV->setChecked(settings.value("saveCSV", true).toBool());
    m_csvFluxDensity = settings.value("csvFluxDensity", true).toBool();
    m_csvVectorPotential = settings.value("csvVectorPotential", true).toBool();
    m_csvEnergy = settings.value("csvEnergy", true).toBool();
    m_csvForceTorque = settings.value("csvForceTorque", true).toBool();
    m_csvIronLoss = settings.value("csvIronLoss", false).toBool();
    m_calculateLosses->setChecked(settings.value("calculateLosses", false).toBool());
    m_accurateSolidLosses->setChecked(settings.value("accurateSolidLosses", false).toBool());
    m_motorRPM->setValue(settings.value("motorRPM", 0.0).toDouble());
    m_operatingFreq->setValue(settings.value("operatingFreqHz", 0.0).toDouble());

    // Motor settings
    m_motorEnabled->setChecked(settings.value("motorEnabled", false).toBool());
    m_motorRmsCurrent->setValue(settings.value("motorRmsCurrent", 1.0).toDouble());
    m_motorInitialAngle->setValue(settings.value("motorInitialAngle", 0.0).toDouble());
    // Phase circuit selection: try to restore by name
    QString savedA = settings.value("motorPhaseA", "<None>").toString();
    QString savedB = settings.value("motorPhaseB", "<None>").toString();
    QString savedC = settings.value("motorPhaseC", "<None>").toString();
    int idxA = m_motorPhaseA->findText(savedA);
    int idxB = m_motorPhaseB->findText(savedB);
    int idxC = m_motorPhaseC->findText(savedC);
    if (idxA >= 0) m_motorPhaseA->setCurrentIndex(idxA);
    if (idxB >= 0) m_motorPhaseB->setCurrentIndex(idxB);
    if (idxC >= 0) m_motorPhaseC->setCurrentIndex(idxC);
    m_motorPolePairs->setValue(settings.value("motorPolePairs", 1).toInt());
    m_motorPolePitch->setValue(settings.value("motorPolePitch", 10.0).toDouble());
    m_motorAngleStep->setValue(settings.value("motorAngleStep", 1.0).toDouble());
    m_motorReversePhase->setChecked(settings.value("motorReversePhase", false).toBool());
    // Restore optimisation result
    m_motorOptimized = settings.value("motorOptimized", false).toBool();
    m_motorOptimalAngleValue = settings.value("motorOptimalAngle", 0.0).toDouble();
    if (m_motorOptimized) {
        m_motorOptimalAngle->setText(
            QString::number(m_motorOptimalAngleValue, 'f', 2) + tr(" deg"));
    }
    settings.endGroup();

    // Sync stacked widget to restored motion type
    m_paramStack->setCurrentIndex(m_motionType->currentIndex());
    // Sync motor group enabled state
    m_motorGroup->setEnabled(m_motorEnabled->isChecked());
    updateLossControls(m_calculateLosses->isChecked());

    // Save settings on every close path (Run, Cancel, window close button)
    connect(this, &QDialog::finished, this, &MotionDialog::saveSettings);
}

void MotionDialog::onMotionTypeChanged(int index)
{
    m_paramStack->setCurrentIndex(index);
}

void MotionDialog::onMotorEnabledChanged(bool checked)
{
    m_motorGroup->setEnabled(checked);
}

void MotionDialog::onMotorOptimise()
{
    if (!m_optimizeCallback) {
        QMessageBox::warning(this, tr("Error"),
            tr("Optimisation not available — no callback configured."));
        return;
    }

    // Validate phase assignments
    QString phaseA = m_motorPhaseA->currentText();
    QString phaseB = m_motorPhaseB->currentText();
    QString phaseC = m_motorPhaseC->currentText();
    if (phaseA == tr("<None>") || phaseB == tr("<None>") || phaseC == tr("<None>")) {
        QMessageBox::warning(this, tr("Missing Phase"),
            tr("Please assign all three phases to circuits before optimising."));
        return;
    }

    // Disable button during optimisation
    m_motorOptimiseBtn->setEnabled(false);
    m_motorOptimiseBtn->setText(tr("Optimising..."));

    double optAngle = 0.0;
    QString error;

    bool ok = m_optimizeCallback(
        m_motorRmsCurrent->value(),
        m_motorInitialAngle->value(),
        m_motorAngleStep->value(),
        m_motorPolePairs->value(),
        phaseA, phaseB, phaseC,
        optAngle, error
    );

    m_motorOptimiseBtn->setEnabled(true);
    m_motorOptimiseBtn->setText(tr("Optimise Angle"));

    if (ok) {
        m_motorOptimalAngleValue = optAngle;
        m_motorOptimized = true;
        m_motorOptimalAngle->setText(
            QString::number(optAngle, 'f', 2) + tr(" deg"));
        // Persist immediately so the result survives Cancel / close
        saveSettings();
    } else {
        QMessageBox::warning(this, tr("Optimisation Failed"), error);
    }
}

void MotionDialog::onBrowseOutput()
{
    QString dir = QFileDialog::getExistingDirectory(
        this, tr("Select Output Directory"),
        m_outputDir->text().isEmpty() ? QDir::homePath() : m_outputDir->text());
    if (!dir.isEmpty())
        m_outputDir->setText(dir);
}

void MotionDialog::onAccept()
{
    if (m_outputDir->text().isEmpty()) {
        QMessageBox::warning(this, tr("Missing Output"),
            tr("Please select an output directory."));
        return;
    }

    QDir dir(m_outputDir->text());
    if (!dir.exists()) {
        if (!dir.mkpath(".")) {
            QMessageBox::warning(this, tr("Error"),
                tr("Could not create output directory."));
            return;
        }
    }

    saveSettings();
    accept();
}

void MotionDialog::onCSVOptions()
{
    // Build a small popup dialog with checkboxes for CSV column categories
    QDialog dlg(this);
    dlg.setWindowTitle(tr("CSV Options"));

    auto *layout = new QVBoxLayout(&dlg);

    auto *label = new QLabel(tr("Select data columns to include in CSV:"));
    layout->addWidget(label);

    auto *chkFlux = new QCheckBox(tr("Flux density (B_max, B_min, B_avg)"));
    chkFlux->setChecked(m_csvFluxDensity);
    layout->addWidget(chkFlux);

    auto *chkPotential = new QCheckBox(tr("Vector potential (A_max, A_min)"));
    chkPotential->setChecked(m_csvVectorPotential);
    layout->addWidget(chkPotential);

    auto *chkEnergy = new QCheckBox(tr("Energy && area"));
    chkEnergy->setChecked(m_csvEnergy);
    layout->addWidget(chkEnergy);

    auto *chkForce = new QCheckBox(tr("Force / torque"));
    chkForce->setChecked(m_csvForceTorque);
    layout->addWidget(chkForce);

    auto *note = new QLabel(tr("Position columns (Step, Displacement, Angle) are always included."));
    note->setWordWrap(true);
    note->setStyleSheet("color: gray; font-size: 10px;");
    layout->addWidget(note);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    layout->addWidget(buttons);

    if (dlg.exec() == QDialog::Accepted) {
        m_csvFluxDensity = chkFlux->isChecked();
        m_csvVectorPotential = chkPotential->isChecked();
        m_csvEnergy = chkEnergy->isChecked();
        m_csvForceTorque = chkForce->isChecked();
    }
}

MotionConfig MotionDialog::config() const
{
    MotionConfig c;
    c.groupNumber = m_groupNumber->value();
    c.isRotation = (m_motionType->currentIndex() == 1);
    c.numSteps = m_numSteps->value();

    // Translation: compute per-step dx/dy from total distance + direction
    c.totalDistance = m_totalDistance->value();
    c.directionAngle = m_directionAngle->value();
    {
        double stepDist = c.totalDistance / (double)c.numSteps;
        double rad = c.directionAngle * M_PI / 180.0;
        c.dx = stepDist * std::cos(rad);
        c.dy = stepDist * std::sin(rad);
    }

    // Rotation: user enters total angle; compute per-step angle for the runner
    c.cx = m_cx->value();
    c.cy = m_cy->value();
    c.angle = m_angle->value() / (double)c.numSteps;
    c.outputDir = m_outputDir->text();
    c.saveImages = m_saveImages->isChecked();
    c.saveVideo = m_saveVideo->isChecked();
    c.loopPlayback = m_loopPlayback->isChecked();
    c.saveCSV = m_saveCSV->isChecked();
    c.csvFluxDensity = m_csvFluxDensity;
    c.csvVectorPotential = m_csvVectorPotential;
    c.csvEnergy = m_csvEnergy;
    c.csvForceTorque = m_csvForceTorque;
    c.csvIronLoss = m_csvIronLoss;
    c.calculateLosses = m_calculateLosses->isChecked();
    c.accurateSolidLosses = m_accurateSolidLosses->isChecked();
    c.motorRPM = m_motorRPM->value();
    c.operatingFreqHz = m_operatingFreq->value();

    // Motor module
    c.motorEnabled = m_motorEnabled->isChecked();
    c.motorRmsCurrent = m_motorRmsCurrent->value();
    c.motorInitialAngle = m_motorInitialAngle->value();
    c.motorPhaseA = m_motorPhaseA->currentText();
    c.motorPhaseB = m_motorPhaseB->currentText();
    c.motorPhaseC = m_motorPhaseC->currentText();
    c.motorPolePairs = m_motorPolePairs->value();
    c.motorPolePitch = m_motorPolePitch->value();
    c.motorAngleStep = m_motorAngleStep->value();
    c.motorOptimalAngle = m_motorOptimalAngleValue;
    c.motorOptimized = m_motorOptimized;
    c.motorReversePhase = m_motorReversePhase->isChecked();

    return c;
}

void MotionDialog::saveSettings()
{
    QSettings settings;
    settings.beginGroup("motion");
    settings.setValue("groupNumber", m_groupNumber->value());
    settings.setValue("motionType", m_motionType->currentIndex());
    settings.setValue("totalDistance", m_totalDistance->value());
    settings.setValue("directionAngle", m_directionAngle->value());
    settings.setValue("cx", m_cx->value());
    settings.setValue("cy", m_cy->value());
    settings.setValue("angle", m_angle->value());
    settings.setValue("numSteps", m_numSteps->value());
    settings.setValue("outputDir", m_outputDir->text());
    settings.setValue("saveImages", m_saveImages->isChecked());
    settings.setValue("saveVideo", m_saveVideo->isChecked());
    settings.setValue("loopPlayback", m_loopPlayback->isChecked());
    settings.setValue("saveCSV", m_saveCSV->isChecked());
    settings.setValue("csvFluxDensity", m_csvFluxDensity);
    settings.setValue("csvVectorPotential", m_csvVectorPotential);
    settings.setValue("csvEnergy", m_csvEnergy);
    settings.setValue("csvForceTorque", m_csvForceTorque);
    settings.setValue("csvIronLoss", m_csvIronLoss);
    settings.setValue("calculateLosses", m_calculateLosses->isChecked());
    settings.setValue("accurateSolidLosses", m_accurateSolidLosses->isChecked());
    settings.setValue("motorRPM", m_motorRPM->value());
    settings.setValue("operatingFreqHz", m_operatingFreq->value());
    // Motor settings
    settings.setValue("motorEnabled", m_motorEnabled->isChecked());
    settings.setValue("motorRmsCurrent", m_motorRmsCurrent->value());
    settings.setValue("motorInitialAngle", m_motorInitialAngle->value());
    settings.setValue("motorPhaseA", m_motorPhaseA->currentText());
    settings.setValue("motorPhaseB", m_motorPhaseB->currentText());
    settings.setValue("motorPhaseC", m_motorPhaseC->currentText());
    settings.setValue("motorPolePairs", m_motorPolePairs->value());
    settings.setValue("motorPolePitch", m_motorPolePitch->value());
    settings.setValue("motorAngleStep", m_motorAngleStep->value());
    settings.setValue("motorReversePhase", m_motorReversePhase->isChecked());
    settings.setValue("motorOptimized", m_motorOptimized);
    settings.setValue("motorOptimalAngle", m_motorOptimalAngleValue);
    settings.endGroup();
}
